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
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <stdlib.h>
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
// host:port opens a TCP link instead of a device. FluidNC's telnet channel is
// the same byte stream as the UART one — GrblParserC cannot tell them apart —
// so this reaches a real controller on the network with no other changes.
//
// Everything downstream is fd-based and works unmodified on a socket.
static int connect_tcp(const char* host, int port) {
    struct addrinfo hints = {}, *res = nullptr;
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char portstr[8];
    snprintf(portstr, sizeof(portstr), "%d", port);
    if (getaddrinfo(host, portstr, &hints, &res) != 0 || !res) return -1;

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd >= 0) {
        // Connect blocking, then go non-blocking: a half-open connect would
        // otherwise look like an empty link and read as "controller silent".
        if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) { close(fd); fd = -1; }
        else {
            int one = 1;
            // Jog chunks are tiny and latency-critical; Nagle would coalesce
            // them into bursts and make motion stutter.
            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
            fcntl(fd, F_SETFL, O_NONBLOCK);
        }
    }
    freeaddrinfo(res);
    return fd;
}

bool sim_link_open(const char* path) {
    // "host:port" -> TCP, anything else -> device path.
    const char* colon = strrchr(path, ':');
    if (colon && colon[1] && strspn(colon + 1, "0123456789") == strlen(colon + 1)) {
        char host[128];
        size_t n = (size_t)(colon - path);
        if (n >= sizeof(host)) n = sizeof(host) - 1;
        memcpy(host, path, n);
        host[n] = 0;
        s_fd = connect_tcp(host, atoi(colon + 1));
        if (s_fd < 0) {
            snprintf(s_detail, sizeof(s_detail), "canned fake - %s unreachable", path);
            return false;
        }
        snprintf(s_detail, sizeof(s_detail), "live FluidNC on %s", path);
        return true;
    }

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
static uint32_t    s_home_until = 0;

void sim_set_state(const char* st) { s_state = st; }

// Put the fake into Alarm the way FluidNC does: the ALARM:N message, then the
// state reports say Alarm.
void sim_raise_alarm(int n) {
    char b[24];
    snprintf(b, sizeof(b), "ALARM:%d\n", n);
    q_push(b);
    s_state = "Alarm";
}
void sim_toggle_motion() { s_moving = !s_moving; }
// A job reaching "(MSG, Change to Tn: name)" + "M6 Tn" under atc_manual, timed
// like the real thing: travel to the change position, pause for the operator,
// then (on cycle start) measure on the toolsetter and go back to cutting.
// t = 0 takes the next tool of the Snowman job.
static uint32_t s_tc_at = 0;      // when the current phase ends; 0 = idle
static int      s_tc_phase = 0;   // 1 travelling, 2 waiting, 3 measuring
static int      s_tc_tool = 0;
static int      s_cur_tool = -1;  // what $G reports; FluidNC boots not knowing
static char     s_tc_msg_later[112];
static bool     s_tc_idle  = false; // started from Idle (the rack), so ends there

void sim_tool_change(int t) {
    static const int   TOOLS[] = { 2, 4, 12 };
    static const char* NAMES[] = { "2 HOG Roughing End Mill {1/4\"} UP CUT",
                                   "4 SpeTool W02003 SC Compression Spiral 1/4\" Dia",
                                   "12 SpeTool W01016 Tapered Ball Nose 1/64\" Radius" };
    static int next = 0;
    int k = next++ % 3;
    for (int i = 0; t && i < 3; i++) if (TOOLS[i] == t) k = i;   // a Snowman tool keeps its name
    s_tc_tool = t ? t : TOOLS[k];
    char b[160];
    snprintf(b, sizeof(b), "[MSG:INFO: MSG, Change to T%d: %s]\n", s_tc_tool,
             !t || TOOLS[k] == t ? NAMES[k] : "test tool");
    q_push(b);
    s_state = "Run";
    s_tc_phase = 1;
    s_tc_at = millis() + 1500;
}

// What openatc_m6.nc does, timed: rack tools go and come without a stop; a
// hand tool going out before a rack pickup is a "Remove" stop, a hand tool
// coming in is an "Install" stop. Then the toolsetter.
static bool sim_rack(int t) { return t >= 1 && t <= 5; }
void sim_atc_change(int to) {
    s_tc_tool = to;
    s_state = "Run";
    if (sim_rack(to)) {
        if (!sim_rack(s_cur_tool)) {
            char b[96];
            if (s_cur_tool < 0) snprintf(b, sizeof(b), "[MSG:INFO: PRINT,Remove the tool in the spindle then resume to continue]\n");
            else                snprintf(b, sizeof(b), "[MSG:INFO: PRINT,Remove tool T%d then resume to continue]\n", s_cur_tool);
            s_tc_msg_later[0] = 0;
            snprintf(s_tc_msg_later, sizeof(s_tc_msg_later), "%s", b);
            s_tc_phase = 1;                  // travel, then the removal stop
        } else {
            s_tc_phase = 3;                  // drop + pick up + measure, no stop
        }
        s_tc_at = millis() + 3000;
        return;
    }
    snprintf(s_tc_msg_later, sizeof(s_tc_msg_later),
             "[MSG:INFO: PRINT,Install tool T%d then resume to continue]\n", to);
    s_tc_phase = 1;
    s_tc_at = millis() + 1500;
}

static void tool_change_tick(uint32_t now) {
    if (!s_tc_phase || (int32_t)(now - s_tc_at) < 0) return;
    if (s_tc_phase == 1) {
        if (s_tc_msg_later[0]) { q_push(s_tc_msg_later); s_tc_msg_later[0] = 0; }
        else {
            char b[96];
            snprintf(b, sizeof(b), "[MSG:INFO: MSG, Install tool #%d then resume to continue]\n", s_tc_tool);
            q_push(b);
        }
        s_state = "Hold:0";
        s_tc_phase = 2;                     // until cycle start
    } else if (s_tc_phase == 3) {
        q_push("[MSG:INFO: TLO measured on the toolsetter (sim)]\n");
        s_cur_tool = s_tc_tool;
        s_state = s_tc_idle ? "Idle" : "Run";   // back to the cut, or to rest
        s_tc_idle = false;
        s_tc_phase = 0;
    }
}

static void axis_positions(float* out, int n) {
    float t = (millis() - s_started) / 1000.0f;
    if (!s_moving) t = 0.0f;
    const float span[6] = { 120.0f, 80.0f, 25.0f, 360.0f, 90.0f, 90.0f };
    const float rate[6] = { 0.31f, 0.21f, 0.13f, 0.07f, 0.05f, 0.03f };
    for (int i = 0; i < n; i++) out[i] = span[i] * sinf(t * rate[i] * 6.2831853f);
}

// Feed, rapid, spindle override, applied from the realtime bytes the way
// FluidNC does (Config.h limits) so the faders round-trip through Ov:.
static int s_ov[3] = { 100, 100, 100 };

static void apply_override(uint8_t c) {
    auto clamp = [](int v) { return v < 10 ? 10 : v > 200 ? 200 : v; };
    switch (c) {
        case 0x90: s_ov[0] = 100; break;
        case 0x91: s_ov[0] = clamp(s_ov[0] + 10); break;
        case 0x92: s_ov[0] = clamp(s_ov[0] - 10); break;
        case 0x93: s_ov[0] = clamp(s_ov[0] + 1); break;
        case 0x94: s_ov[0] = clamp(s_ov[0] - 1); break;
        case 0x95: s_ov[1] = 100; break;
        case 0x96: s_ov[1] = 50; break;
        case 0x97: s_ov[1] = 25; break;
        case 0x99: s_ov[2] = 100; break;
        case 0x9A: s_ov[2] = clamp(s_ov[2] + 10); break;
        case 0x9B: s_ov[2] = clamp(s_ov[2] - 10); break;
        case 0x9C: s_ov[2] = clamp(s_ov[2] + 1); break;
        case 0x9D: s_ov[2] = clamp(s_ov[2] - 1); break;
    }
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
    k += snprintf(rpt + k, sizeof(rpt) - k, "|Ov:%d,%d,%d", s_ov[0], s_ov[1], s_ov[2]);
    snprintf(rpt + k, sizeof(rpt) - k, ">\n");
    q_push(rpt);
}

static char   s_line[256];
static size_t s_len = 0;

static void canned_putchar(uint8_t c) {
    if (c == '?') { push_status_report(); return; }
    if (c == 0x11 || c == 0x13) return;
    if (c >= 0x80) { apply_override(c); return; }
    if (c == 0x18) { s_tc_phase = 0; return; }
    if (c == '~' && s_tc_phase == 2) {      // TOOL CHANGED / RESUME
        s_state = "Run";
        s_tc_phase = 3;
        s_tc_at = millis() + 2500;          // seek + slow probe + return
        return;
    }

    if (c == '\n' || c == '\r') {
        if (!s_len) return;
        s_line[s_len] = 0;
        if (strcmp(s_line, "$X") == 0 && strcmp(s_state, "Alarm") == 0) s_state = "Idle";
        // Just enough machine for the homing walk-through: a homing cycle,
        // a TLO macro, and a $H that takes a moment.
        if (strcmp(s_line, "$H") == 0) { s_state = "Home"; s_home_until = millis() + 1500; }
        // The tool rack: M61 Q<n> sets the tool, M6 T<n> plays the atc_manual
        // change (T0 just clears it), $G reports the T word back.
        int n;
        if (sscanf(s_line, "M61 Q%d", &n) == 1) s_cur_tool = n;
        if (sscanf(s_line, "M6 T%d", &n) == 1) {
            s_tc_idle = strncmp(s_state, "Idle", 4) == 0;
            sim_atc_change(n);
        }
        if (strcmp(s_line, "$G") == 0) {
            char g[96];
            snprintf(g, sizeof(g), "[GC:G0 G54 G17 G21 G90 G94 M5 M9 T%d F0 S0]\nok\n", s_cur_tool);
            q_push(g);
        }
        else if (strcmp(s_line, "$#") == 0) q_push("[G54:0.000,0.000,0.000]\nok\n");
        else if (strcmp(s_line, "$S") == 0) q_push("$Grbl/HomingCycleEnable=1\nok\n");
        else if (strcmp(s_line, "$CD") == 0)
            q_push("axes:\n  x:\n    motor0:\n      hard_limits: true\n"
                   "macros:\n  Macro0: G43.1Z0\n  Macro1: G43.1Z#<_my_tlo_z>\nok\n");
        else q_push("ok\n");
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
    if (s_home_until && (int32_t)(now - s_home_until) >= 0) { s_home_until = 0; s_state = "Idle"; }
    tool_change_tick(now);
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

bool init_fnc_uart() { return true; }
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
