// The simulator's link to a controller, for [env:sim].
//
// Preferred mode is LIVE: open the pty that simrun/ttybridge.py publishes
// (default /tmp/ttyFLUID) and move raw bytes between it and GrblParserC. That
// puts a real, stock FluidNC host build behind the real parser behind the real
// UI — the only fake left in the stack is the pixels' destination.
//
//   cd ~/Projects/FluidNC/simrun && ./ttybridge.py
//   .pio/build/sim/program --link /tmp/ttyFLUID
//
// Fallback mode is CANNED: if the pty is not there, a minimal fake controller
// answers so the UI is still inspectable on a machine with no rig running.
// Which mode is active is on the Setup page, because a simulator that silently
// degrades to fake data is worse than one that refuses to start.

#include "transport/transport.h"
#include "transport/uart.h"
#include "model/FluidNCModel.h"
#include "GrblParserC.h"

#include <Arduino.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

// ---------------- live pty link ----------------
static int  s_fd = -1;
static char s_detail[96] = "no link";

// Bytes arrive in blocks; fnc_poll() consumes them one at a time and drains up
// to 512 per pass. Reading per byte would be 512 syscalls a frame, so the read
// is buffered and the syscall happens only when the buffer runs dry.
static uint8_t s_rxbuf[4096];
static size_t  s_rxlen = 0, s_rxpos = 0;

bool sim_link_is_live() { return s_fd >= 0; }

// Returns true if the pty was opened. Non-blocking throughout: a read that
// would block must return "no byte" rather than stalling the UI thread, which
// is the same contract the real UART driver has.
bool sim_link_open(const char* path) {
    s_fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (s_fd < 0) {
        snprintf(s_detail, sizeof(s_detail), "canned fake - %s absent", path);
        return false;
    }

    // Raw mode. Without this the line discipline would eat the realtime bytes
    // ('?' is fine, but 0x11/0x13 are XON/XOFF to a cooked terminal) and
    // rewrite CR/LF, both of which corrupt the Grbl protocol.
    struct termios t;
    if (tcgetattr(s_fd, &t) == 0) {
        cfmakeraw(&t);
        t.c_cc[VMIN]  = 0;
        t.c_cc[VTIME] = 0;
        tcsetattr(s_fd, TCSANOW, &t);
    }
    snprintf(s_detail, sizeof(s_detail), "live FluidNC on %s", path);
    return true;
}

// ---------------- canned fallback controller ----------------
static const int      SIM_AXES       = 4;
static const uint32_t SIM_REPORT_MS  = 200;

static char   s_q[8192];
static size_t s_head = 0, s_tail = 0;

static void q_push(const char* s) {
    while (*s) {
        size_t next = (s_head + 1) % sizeof(s_q);
        if (next == s_tail) return;
        s_q[s_head] = *s++;
        s_head = next;
    }
}

static const char* s_state   = "Idle";
static uint32_t    s_started = 0;
static bool        s_moving  = true;

void sim_set_state(const char* st) { s_state = st; }
void sim_toggle_motion() { s_moving = !s_moving; }

static void axis_positions(float* out, int n) {
    float t = (millis() - s_started) / 1000.0f;
    if (!s_moving) t = 0.0f;
    const float span[6] = { 120.0f, 80.0f, 25.0f, 360.0f, 90.0f, 90.0f };
    const float rate[6] = { 0.31f, 0.21f, 0.13f, 0.07f, 0.05f, 0.03f };
    for (int i = 0; i < n; i++) out[i] = span[i] * sinf(t * rate[i] * 6.2831853f);
}

static void push_status_report() {
    float pos[6];
    axis_positions(pos, SIM_AXES);

    char rpt[256];
    int  k = snprintf(rpt, sizeof(rpt), "<%s|MPos:", s_state);
    for (int i = 0; i < SIM_AXES; i++)
        k += snprintf(rpt + k, sizeof(rpt) - k, "%s%.3f", i ? "," : "", pos[i]);

    bool running = strcmp(s_state, "Run") == 0;
    k += snprintf(rpt + k, sizeof(rpt) - k, "|FS:%d,%d", running ? 850 : 0, running ? 12000 : 0);
    k += snprintf(rpt + k, sizeof(rpt) - k, "|WCO:");
    for (int i = 0; i < SIM_AXES; i++)
        k += snprintf(rpt + k, sizeof(rpt) - k, "%s%.3f", i ? "," : "", i == 2 ? -5.0f : -10.0f);
    if (running) k += snprintf(rpt + k, sizeof(rpt) - k, "|Ov:100,100,100");
    snprintf(rpt + k, sizeof(rpt) - k, ">\n");
    q_push(rpt);
}

static char   s_line[256];
static size_t s_len = 0;

static void canned_putchar(uint8_t c) {
    if (c == '?') { push_status_report(); return; }
    if (c == 0x11 || c == 0x13) return;
    if (c >= 0x80 || c == 0x18) return;

    if (c == '\n' || c == '\r') {
        if (!s_len) return;
        s_line[s_len] = 0;
        if (strcmp(s_line, "$#") != 0) q_push("ok\n");
        else                           q_push("[G54:0.000,0.000,0.000]\nok\n");
        s_len = 0;
        return;
    }
    if (s_len < sizeof(s_line) - 1) s_line[s_len++] = (char)c;
}

// ---------------- the parser's byte interface ----------------
static unsigned long s_rx = 0, s_tx = 0;

extern "C" void fnc_putchar(uint8_t c) {
    s_tx++;
    if (s_fd >= 0) {
        // A short write on a pty means the far end is not draining; dropping
        // the byte is wrong for a command, so retry briefly and then give up
        // rather than block the UI thread for ever.
        for (int i = 0; i < 8; i++) {
            ssize_t n = write(s_fd, &c, 1);
            if (n == 1) return;
            if (n < 0 && errno != EAGAIN && errno != EINTR) return;
        }
        return;
    }
    canned_putchar(c);
}

extern "C" int fnc_getchar() {
    if (s_fd >= 0) {
        if (s_rxpos >= s_rxlen) {
            ssize_t n = read(s_fd, s_rxbuf, sizeof(s_rxbuf));
            if (n <= 0) return -1;
            s_rxlen = (size_t)n;
            s_rxpos = 0;
        }
        s_rx++;
        return s_rxbuf[s_rxpos++];
    }
    if (s_tail == s_head) return -1;
    s_rx++;
    int c = (unsigned char)s_q[s_tail];
    s_tail = (s_tail + 1) % sizeof(s_q);
    return c;
}

extern "C" bool fnc_rx_waiting() {
    if (s_fd >= 0) return s_rxpos < s_rxlen;
    return s_tail != s_head;
}

// Called once per simulator loop. The canned controller needs a heartbeat to
// push reports; the live one sends its own, driven by the panel's own '?'
// pings in request_status_report().
void sim_transport_tick() {
    if (s_fd >= 0) return;
    static uint32_t next = 0;
    uint32_t now = millis();
    if (!s_started) s_started = now;
    if ((int32_t)(now - next) < 0) return;
    next = now + SIM_REPORT_MS;
    push_status_report();
}

// ---------------- the rest of the transport surface ----------------
void transport_init() { s_started = millis(); }
void transport_poll() {}
void transport_settings_changed() {}

const char* transport_label()  { return s_fd >= 0 ? "SIM LIVE" : "SIM FAKE"; }
const char* transport_detail() { return s_detail; }

void init_fnc_uart() {}
void reinit_fnc_uart() {}

// Both live in the real uart.cpp, which this file replaces. milliseconds() is
// GrblParserC's clock — it must share an origin with millis(), or the model's
// ack timeouts and the UI's timers would disagree about what "now" is.
extern "C" int milliseconds() { return (int)millis(); }

// The pty has no hardware flow control and the canned controller never
// throttles, so there is no XOFF state to clear in either mode.
void resetFlowControl() {}

unsigned long fnc_rx_count() { return s_rx; }
unsigned long fnc_tx_count() { return s_tx; }
uint32_t      fnc_get_baud() { return 115200; }

void uart_putc(uint8_t c) { fnc_putchar(c); }
int  uart_getc() { return fnc_getchar(); }
bool uart_rx_waiting() { return fnc_rx_waiting(); }
