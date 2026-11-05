// Copyright (c) 2023 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "model/console_log.h"
#include "model/caps.h"
#include "FluidNCModel.h"
#include "model/filelist.h"
#include <map>
#include <string>   // arduino-esp32 3.x no longer pulls this in transitively
#include "model/sys_compat.h"
#include "model/model_events.h"
#include "model/model_compat.h"
#include "transport/uart.h"
#include "e4math.h"
#include "model/sendq.h"
#include "model/homed.h"
#include "model/motors.h"
#include "model/linkwatch.h"
#include "model/path_loader.h"
#include "model/offsets.h"
#include "model/fnc_settings.h"
#include <stdarg.h>
#include <stdio.h>

#ifdef USE_WIFI
#    include "WiFiConnection.h"  // wifi_use_uart_mode()
#endif


// local copies of status items
const char*        my_state_string    = "N/C";
state_t            state              = Disconnected;  // correct: we are disconnected until FluidNC responds
int                n_axes             = 3;
pos_t              myAxes[6]          = { 0 };
pos_t              myWco[6]           = { 0 };
bool               myLimitSwitches[6] = { false };
bool               myProbeSwitch      = false;
const char*        myFile             = "";  // running SD filename
const char*        myCtrlPins         = "";
file_percent_t     myPercent          = 0.0;  // percent conplete of SD file
override_percent_t myFro              = 100;  // Feed rate override
override_percent_t myRro              = 100;  // Rapid override
override_percent_t mySro              = 100;  // Spindle Override
uint32_t           myFeed             = 0;
uint32_t           mySpeed            = 0;
uint32_t           mySelectedTool     = 0;

std::string myModes = "no data";

int      lastAlarm = 0;
int      lastError = 0;
bool     inInches  = false;
uint32_t errorExpire;

int num_digits() {
    return inInches ? 3 : 2;
}

// clang-format off
// Maps the state strings in status reports to internal state enum values
struct cmp_str {
   bool operator()(char const *a, char const *b) const    {
      return strcmp(a, b) < 0;
   }
};

std::map<const char *, state_t, cmp_str>  state_map = {
    { "Idle", Idle },
    { "Alarm", Alarm },
    { "Hold:0", Hold },
    { "Hold:1", Hold },
    { "Run", Cycle },
    { "Jog", Jog },
    { "Home", Homing },
    { "Door:0", DoorClosed },
    { "Door:1", DoorOpen },
    { "Check", CheckMode },
    { "Sleep", GrblSleep },
};
// clang-format on

bool decode_state_string(const char* state_string, state_t& state) {
    if (strcmp(my_state_string, state_string) != 0) {
        auto found = state_map.find(state_string);
        if (found != state_map.end()) {
            my_state_string = found->first;
            state           = found->second;
            return true;
        }
    }
    return false;
}

void set_disconnected_state() {
    state           = Disconnected;
    my_state_string = "N/C";
    homed_forget();
    jog_window_reset();
}

extern "C" void begin_status_report() {
    myPercent = 0;
}

extern "C" void show_file(const char* filename, file_percent_t percent) {
    myPercent = percent;
}

extern "C" void show_overrides(override_percent_t feed_ovr, override_percent_t rapid_ovr, override_percent_t spindle_ovr) {
    myFro = feed_ovr;
    myRro = rapid_ovr;
    mySro = spindle_ovr;
}

extern "C" void show_feed_spindle(uint32_t feedrate, uint32_t spindle_speed) {
    myFeed  = feedrate;
    mySpeed = spindle_speed;
};

extern "C" void show_limits(bool probe, const bool* limits, size_t n_axis) {
    myProbeSwitch = probe;
    memcpy(myLimitSwitches, limits, n_axis * sizeof(*limits));
}

extern "C" void show_control_pins(const char* pins) {
    //dbg_printf("show_control_pins:%s\r\n", pins);
    myCtrlPins = pins;
}

// ---- stepper-driver health ------------------------------------------------
// Contract settled with the configurator session, 2026-09-07 (see
// /tmp/HANDOFF-driver-io-configurator.md): X/Y1/Y2/Z ride FluidNC macro
// pins, which report in Pn: as digit characters '0'-'3' while active —
// wired active-while-online, so digit present = driver powered and healthy.
// Deliberately NOT alarm-class. A's input is a user_input read via M66
// (poll-only; NOT in Pn:) — that path is not built yet, so A has no badge
// and never gates. The feature arms on the first digit sighting, so a
// machine without the wiring never grows indicators.
static bool s_drv_seen = false;

bool drv_pin(int n) {
    for (const char* p = myCtrlPins; *p; ++p)
        if (*p == '0' + n) return true;
    return false;
}

bool drv_feature_armed() {
    if (!s_drv_seen)
        for (const char* p = myCtrlPins; *p; ++p)
            if (*p >= '0' && *p <= '3') { s_drv_seen = true; break; }
    return s_drv_seen;
}

bool drv_axis_ok(char ax) {
    switch (ax) {
        case 'X': return drv_pin(0);
        case 'Y': return drv_pin(1) && drv_pin(2);   // both gantry drivers
        case 'Z': return drv_pin(3);
        case 'A': return true;   // no telemetry until the M66 path exists
    }
    return true;
}

// Pulse the driver's reset relay(s). M64/M65 P<n> addresses the config's
// user_outputs SLOT, not the Airedale pin: slots 0-4 map to Airedale
// outputs 8-12 (X Y1 Y2 Z A), each a normally-closed relay in that
// driver's power feed. ON -> dwell -> OFF power-cycles the driver, which
// is how a latched fault clears. The dwell runs on the controller as G4,
// so nothing here blocks; both Y relays pulse together — half a gantry
// with power is worse than none.
void drv_reset_axis(char ax) {
    int outs[2] = { -1, -1 };
    switch (ax) {
        case 'X': outs[0] = 0;              break;
        case 'Y': outs[0] = 1; outs[1] = 2; break;
        case 'Z': outs[0] = 3;              break;
        case 'A': outs[0] = 4;              break;
        default:  return;
    }
    // One sequence: a relay switched on must never be left on because the
    // line that switches it off was dropped.
    char off[32] = "";
    seq_begin("Driver reset");
    for (int i = 0; i < 2; i++)
        if (outs[i] >= 0) seq_linef("M64 P%d", outs[i]);
    seq_line("G4 P0.75");   // relay drop + driver discharge; tune on bench
    for (int i = 0; i < 2; i++)
        if (outs[i] >= 0) {
            seq_linef("M65 P%d", outs[i]);
            const size_t n = strlen(off);
            snprintf(off + n, sizeof(off) - n, "%sM65 P%d", n ? "\n" : "", outs[i]);
        }
    seq_cleanup(off);
    seq_send();
}

#ifdef E4_POS_T
extern "C" void show_dro(const pos_t* axes, const pos_t* wco, bool isMpos, bool* limits, size_t n_axis) {
    n_axes = (int)n_axis;
    for (int axis = 0; axis < n_axis; axis++) {
        e4_t axis_val = axes[axis];
        if (isMpos) {
            axis_val -= wco[axis];
        }
        myAxes[axis] = inInches ? e4_mm_to_inch(axis_val) : axis_val;
        // Keep the work coordinate offset too. myAxes is the WORK position with
        // wco already removed, so machine position = myAxes + myWco. Without
        // this the DRO can only ever show one of the two.
        myWco[axis] = inInches ? e4_mm_to_inch(wco[axis]) : wco[axis];
    }
}
#else
pos_t fromMm(pos_t position) {
    return inInches ? position / 25.4 : position;
}
pos_t toMm(pos_t position) {
    return inInches ? position * 25.4 : position;
}

extern "C" void show_dro(const pos_t* axes, const pos_t* wco, bool isMpos, bool* limits, size_t n_axis) {
    for (int axis = 0; axis < n_axis; axis++) {
        myAxes[axis] = fromMm(axes[axis]);
        if (isMpos) {
            myAxes[axis] -= fromMm(wco[axis]);
        }
    }
}
#endif

// ---- non-blocking send -----------------------------------------------------
// Every line to the controller goes through here, never straight from a UI
// callback: GrblParserC's fnc_send_line() spins in `while (_ackwait)` until the
// previous line is acked, and a UI callback runs inside lv_timer_handler(), so
// a slow controller froze the panel (8.4 s at every power-on, measured). Lines
// are queued and drained from loop() one at a time, each waiting for its ok or
// error. This layer writes the bytes and keeps the ack gate itself, so the
// parser's own gate is never armed and nothing here can spin.

static const uint32_t ACK_TIMEOUT_MS     = 2000;
// A sequence line's ok can be a long time coming: FluidNC answers G38.2 only
// when the probe move ends, which at F75 over 5 mm is 4 s. Timing out early
// would send the next line while the probe is still moving and misattribute
// every ok after it, so an error could no longer be traced to its sequence.
static const uint32_t SEQ_ACK_TIMEOUT_MS = 30000;

static sendq_t     s_sendq;
static uint32_t    s_ack_deadline = 0;   // 0 = nothing outstanding
static sendq_seq_t s_inflight_seq = 0;   // sequence of the line awaiting its ack
static sendq_seq_t s_next_seq     = 0;

// Each sequence's name and what to send if it is cut short, by seq id. Only the
// sequences that can be in the queue at once need a slot.
#define SEQ_META 8
static struct {
    sendq_seq_t seq;
    char        name[24];
    char        cleanup[48];
} s_meta[SEQ_META];

bool send_busy() {
    if (s_ack_deadline == 0) return false;
    if ((int32_t)(milliseconds() - s_ack_deadline) >= 0) {
        // No ok, no error. Carry on rather than wedge the queue for ever.
        s_ack_deadline = 0;
        s_inflight_seq = 0;
        clog_add("[MSG:WARN: no reply from the controller - sending the next line]");
        return false;
    }
    return true;
}

static void send_now(const char* line, sendq_seq_t seq) {
    for (const char* p = line; *p; ++p) fnc_putchar((uint8_t)*p);
    fnc_putchar('\n');
    uint32_t deadline = milliseconds() + (seq ? SEQ_ACK_TIMEOUT_MS : ACK_TIMEOUT_MS);
    s_ack_deadline = deadline ? deadline : 1;   // 0 means idle
    s_inflight_seq = seq;
}

static void line_acked() {
    s_ack_deadline = 0;
    s_inflight_seq = 0;
}

// The last line of a sequence was answered ok: it finished, and an alarm
// after this point is not its business.
static void seq_ok(sendq_seq_t seq) {
    if (!seq || sendq_count_seq(&s_sendq, seq)) return;
    auto& m = s_meta[seq % SEQ_META];
    if (m.seq == seq) m.seq = 0;
}

void send_pump() {
    if (send_busy()) return;
    char        line[SENDQ_LINE];
    sendq_seq_t seq = 0;
    if (!sendq_pop_seq(&s_sendq, line, sizeof(line), &seq)) return;
    send_now(line, seq);
}

void send_line(const char* s, int timeout) {
    (void)timeout;   // the queue owns the timing now
    motors_guard(s);   // re-arm hard limits ahead of anything that moves
    if (!sendq_push(&s_sendq, s)) {
        // A machine command has been thrown away. Say so on the screen; a
        // silently dropped G10 or $H gets blamed on the controller.
        char b[CLOG_LINE_MAX];
        snprintf(b, sizeof(b), "send queue full - dropped: %s", s);
        model_flag_error(b);
    }
    dbg_println(s);
    send_pump();   // straight out when the link is idle, which is the normal case
}

// ---- sequences --------------------------------------------------------------

static char        s_stage[SENDQ_SLOTS][SENDQ_LINE];
static int         s_stage_n    = 0;
static bool        s_stage_over = false;
static char        s_stage_name[24];
static char        s_stage_cleanup[48];

void seq_begin(const char* name) {
    s_stage_n = 0;
    s_stage_over = false;
    snprintf(s_stage_name, sizeof(s_stage_name), "%s", name ? name : "sequence");
    s_stage_cleanup[0] = 0;
}

void seq_cleanup(const char* line) {
    snprintf(s_stage_cleanup, sizeof(s_stage_cleanup), "%s", line ? line : "");
}

void seq_line(const char* s) {
    if (!s) return;
    if (s_stage_n >= SENDQ_SLOTS) { s_stage_over = true; return; }
    snprintf(s_stage[s_stage_n++], SENDQ_LINE, "%s", s);
}

void seq_linef(const char* fmt, ...) {
    char    b[SENDQ_LINE];
    va_list va;
    va_start(va, fmt);
    vsnprintf(b, sizeof(b), fmt, va);
    va_end(va);
    seq_line(b);
}

static void refuse(const char* why) {
    char b[CLOG_LINE_MAX];
    snprintf(b, sizeof(b), "%s not started: %s", s_stage_name, why);
    model_flag_error(b);
}

bool seq_send() {
    if (s_stage_n == 0) return false;
    if (s_stage_over) { refuse("too long for the send queue"); return false; }
    if (state == Disconnected) { refuse("no link"); return false; }

    // Re-arm released steppers first, as send_line() would for each line.
    for (int i = 0; i < s_stage_n; i++)
        if (motors_line_can_move(s_stage[i])) { motors_guard(s_stage[i]); break; }

    if (++s_next_seq == 0) s_next_seq = 1;
    const char* lines[SENDQ_SLOTS];
    for (int i = 0; i < s_stage_n; i++) lines[i] = s_stage[i];
    if (!sendq_push_all(&s_sendq, lines, s_stage_n, s_next_seq)) {
        refuse("the controller is still busy with earlier commands");
        return false;
    }
    auto& m = s_meta[s_next_seq % SEQ_META];
    m.seq = s_next_seq;
    snprintf(m.name, sizeof(m.name), "%s", s_stage_name);
    snprintf(m.cleanup, sizeof(m.cleanup), "%s", s_stage_cleanup);
    s_stage_n = 0;
    send_pump();
    return true;
}

// The controller refused a line, or alarmed. Whatever sequence was running
// must not carry on: its later lines assume the earlier ones happened.
static void abort_seq(sendq_seq_t seq, const char* why) {
    if (!seq) return;
    const int dropped = sendq_drop_seq(&s_sendq, seq);
    auto&     m       = s_meta[seq % SEQ_META];
    const bool known  = m.seq == seq;
    if (!known && dropped == 0) return;   // already stopped (an alarm, then its error)
    char b[CLOG_LINE_MAX];
    snprintf(b, sizeof(b), "%s stopped (%s), %d line%s not sent", known ? m.name : "sequence", why,
             dropped, dropped == 1 ? "" : "s");
    model_flag_error(b);
    if (known && m.cleanup[0]) {
        char  c[sizeof(m.cleanup)];
        char* save = nullptr;
        snprintf(c, sizeof(c), "%s", m.cleanup);
        for (char* l = strtok_r(c, "\n", &save); l; l = strtok_r(nullptr, "\n", &save))
            sendq_push(&s_sendq, l);
    }
    if (known) m.seq = 0;
}

void send_flush(const char* why) {
    const size_t n = sendq_count(&s_sendq);
    sendq_reset(&s_sendq);
    line_acked();   // nothing written before this point will be answered
    for (auto& m : s_meta) m.seq = 0;
    if (n && why) {
        char b[CLOG_LINE_MAX];
        snprintf(b, sizeof(b), "[MSG:WARN: %s - %u queued line%s discarded]", why, (unsigned)n,
                 n == 1 ? "" : "s");
        clog_add(b);
    }
}

static uint32_t s_reset_sent_ms = 0;

void send_reset() {
    fnc_realtime(Reset);
    s_reset_sent_ms = milliseconds() | 1;
    send_flush("reset");
}

bool send_reset_was_recent() {
    return s_reset_sent_ms && (int32_t)(milliseconds() - s_reset_sent_ms) < 3000;
}

// Count of jog lines streamed over Telnet but not yet acked by "ok""" to prevent outrunning FluidNC planner.
static int s_jog_window = 0;
int        jog_window_outstanding() { return s_jog_window; }
void       jog_window_reset() { s_jog_window = 0; }

// Stream network jogs without blocking
void send_jog_line(const char* s) {
#ifdef USE_WIFI
    if (!wifi_use_uart_mode()) {  // WiFi / Telnet / ESP-NOW: stream, no ack-gate
        for (const char* p = s; *p; ++p) {
            fnc_putchar((uint8_t)*p);
        }
        fnc_putchar('\n');
        dbg_println(s);
        
        if (jog_flow_control() == JogFlowControl::Window) {
            ++s_jog_window;
        }
        return;
    }
#endif
    // Deliberately NOT queued. Jog chunks are a real-time stream: one tick's
    // worth of travel each, re-issued every 150ms while a key is held. Backing
    // them up in a queue would keep the axis moving after the finger lifts,
    // which is the runaway the whole design avoids. If the controller has not
    // acked the last one it is behind, so skip this chunk -- the axis pauses
    // for a tick and catches up on the next.
    // Steppers released: queue it behind the re-arm lines, and never let a
    // later chunk overtake lines still queued.
    if (motors_released()) { send_line(s); return; }
    if (s_sendq.count) return;
    if (send_busy()) return;
    send_now(s, 0);
    dbg_println(s);
}

JogFlowControl jog_flow_control() {
#ifdef USE_WIFI
    if (wifi_use_uart_mode()) {
        return JogFlowControl::Blocking;
    }
    if (wifi_use_espnow_mode()) {
        return JogFlowControl::Timed;
    }
    return JogFlowControl::Window;
#else
    return JogFlowControl::Blocking;
#endif
}

void send_jog_cancel() {
#ifdef USE_WIFI
    if (!wifi_use_uart_mode() && !wifi_use_espnow_mode()) {
        wifi_discard_pending_text();
        jog_window_reset();
    }
#endif
    fnc_realtime(JogCancel);
}

static void vsend_linef(const char* fmt, va_list va) {
    static char buf[128];
    vsnprintf(buf, 128, fmt, va);
    send_line(buf);
}
void send_linef(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsend_linef(fmt, args);
    va_end(args);
}

char axisNumToChar(int axis) {
    return "XYZABC"[axis];
}

const char* axisNumToCStr(int axis) {
    static char ret[2] = { '\0', '\0' };
    ret[0]             = axisNumToChar(axis);
    return ret;
}

const char* intToCStr(int val) {
    static char buffer[20];
    sprintf(buffer, "%d", val);
    return buffer;
}

const char* mode_string() {
    return myModes.c_str();
}

state_t previous_state;
bool    awaiting_alarm = false;

// ---- link lifecycle ---------------------------------------------------------
// One place decides when the link is up, down, or talking to a controller that
// has just restarted, and what the panel does about each.

static bool s_primed = false;

// Everything the panel asks the controller once per connection. A controller
// restart wipes all of it (auto-report, parser modes, the panel's view of the
// config), so it runs again after one.
static void prime() {
    s_primed = true;
    bootlog_printf("link up (%s): priming", my_state_string);
    resetFlowControl();                   // clear any stale XOFF on the link
    fnc_realtime((realtime_cmd_t)0x0c);   // Ctrl-L: echo off
    send_line("$G");                      // G-code modes
    send_line("$RI=200");                 // auto-report every 200 ms
    motors_resync();                      // a drop may have been the controller rebooting
    motors_on_link();
    // Config dump first: it decides which controls are even offered.
    caps_request_config();
    send_line("$#");
    filelist_request();
    // The full settings dump also feeds the Diag limit lamps.
    fncset_request_all();
}

static void link_lost() {
    clog_add("[MSG:WARN: link lost - no reply from the controller for 4 s]");
    send_flush("link lost");
    set_disconnected_state();
    s_primed = false;
    act_on_state_change();
}

void link_service() {
    // While XModem owns the wire a '?' would land in the middle of the
    // transfer, so the watch stands down (and starts a fresh window after).
    const bool watch = state != Disconnected && !pathload_busy();
    switch (linkwatch_tick(milliseconds(), fnc_rx_count(), watch)) {
        case LINKWATCH_PING: fnc_realtime(StatusReport); break;
        case LINKWATCH_LOST: link_lost(); break;
        default: break;
    }

    if (model_take_board_reset()) {
        // The banner follows a soft reset too. One the panel sent itself keeps
        // position (FluidNC alarms if it did not); anything else may have been
        // a power cycle, so homing is no longer trusted.
        const bool ours = send_reset_was_recent();
        send_flush(ours ? nullptr : "controller restarted");
        if (!ours) {
            clog_add("[MSG:WARN: controller restarted - home before moving]");
            homed_forget();
        }
        s_primed = false;
    }
    if (state == Disconnected) { s_primed = false; return; }
    if (!s_primed) prime();
}

extern "C" void show_state(const char* state_string) {
    previous_state = state;
    state_t new_state;
    if (decode_state_string(state_string, new_state) && state != new_state) {
        // Leaving Alarm retires its number: the next alarm must be fetched
        // or reported afresh, never shown with the last one's.
        if (state == Alarm) lastAlarm = 0;
        homed_note_state(state == Homing, new_state == Alarm, new_state == Disconnected);
        state = new_state;
        if (state == Alarm && lastAlarm == 0) {  // Unknown
            send_line("$A");                     // Get last alarm
            awaiting_alarm = true;
        }
        act_on_state_change();
    }
}

extern "C" void handle_other(char* line) {
    // Fan out to the offset table, the file list and the console BEFORE the
    // early returns below: the file-list reply is exactly the '{' case that
    // returns into handle_json(), so consumers placed after it never see it.
    handle_other_line(line);

    // $-responses are config, never JSON. If FluidNC tore down a JSON
    // document by sending a $-response (rare, but happens on some error
    // paths), drop the depth counter so the next document starts clean.
    if (*line == '$') {
        if (json_in_progress()) {
            json_reset_depth();
        }
        parse_dollar(line);
        return;
    }
    // Route to the streaming JSON parser if either (a) this line opens a
    // new JSON object, or (b) a previous chunk left the outer brace open.
    // FluidNC sends {"files":[...]} etc. without the [MSG:JSON:...] wrapper
    // and splits the body across multiple lines for large file/macro lists.
    if (*line == '{' || json_in_progress()) {
        handle_json(line);
        return;
    }
#ifdef FNC_RX_TRACE
    dbg_printf("[rx-other] %.120s%s\n", line, strlen(line) > 120 ? "..." : "");
#endif
    int alarmlen = strlen("Active alarm: ");
    if (strncmp(line, "Active alarm: ", alarmlen) == 0) {
        lastAlarm = atoi(line + alarmlen);
        homed_note_alarm(lastAlarm);
        if (awaiting_alarm) {
            dbg_printf("Got alarm %d\n", lastAlarm);
            awaiting_alarm = false;
            act_on_state_change();
        }
    }
}

extern "C" void show_error(int error) {
    {
        char b[16];
        snprintf(b, sizeof(b), "error:%d", error);
        clog_add(b);   // named by console_log
    }
    caps_reply_end();
#ifdef FNC_RX_TRACE
    dbg_printf("[rx-err] error:%d\n", error);
#endif
    {
        const sendq_seq_t seq = s_inflight_seq;
        line_acked();
        char why[16];
        snprintf(why, sizeof(why), "error:%d", error);
        abort_seq(seq, why);
    }
    errorExpire = milliseconds() + 1000;
    lastError   = error;
    if (s_jog_window > 0) {
        --s_jog_window;  // a rejected jog  ends 1 outstanding line
    }
    if (json_in_progress()) {
        // "error:N" without a JSON wrapper ends an in-flight document.
        json_reset_depth();
    }
    // Telnet returns bare "error:N" with no JSON wrapper when $File/SendJSON
    // is rejected (file not present, etc). Without this hook the macro chain
    // sits on "Reading Macros" forever because endDocument never fires.
    file_request_failed_advance();
    request_redisplay();
}

// Only fnc_send_line() raises this, and nothing calls it: send_now() keeps
// the ack gate here. Kept so the parser's weak default is never what runs.
extern "C" void show_timeout() {}
// The parser consumes ok / error:N / ALARM:N itself, so they never reached the
// console log as plain lines -- the console showed no replies at all, and the
// FluidNC names console_log gives error and alarm codes never appeared.
// [MSG:...] lines. GrblParserC's default handle_msg() is an empty weak stub,
// and nothing here overrode it, so every FluidNC message -- INFO, WARN, ERR,
// the $CI channel list, the Airedale's "BOARD:" banner -- was dropped unseen.
extern "C" void handle_msg(char* command, char* arguments) {
    model_flag_message(command, arguments);
}

extern "C" void show_ok() {
    clog_add("ok");
    caps_reply_end();
    seq_ok(s_inflight_seq);
    line_acked();
#ifdef FNC_RX_TRACE
    dbg_printf("[rx-ok]\n");
#endif
    if (s_jog_window > 0) {
        --s_jog_window;
    }
    if (json_in_progress()) {
        // "ok" ends a reply; if a torn JSON doc was in flight, clean up.
        json_reset_depth();
    }
}

extern "C" void end_status_report() {
    model_flag_dro();
}

extern "C" void show_alarm(int alarm) {
    {
        char b[16];
        snprintf(b, sizeof(b), "ALARM:%d", alarm);
        clog_add(b);
    }
    lastAlarm = alarm;
    homed_note_alarm(alarm);
    // An alarm stops the machine whatever line it came from. Every queued
    // sequence is now running on wrong assumptions; plain lines ($A, $X, $G)
    // stay, since they are how the alarm gets read and cleared.
    {
        char why[16];
        snprintf(why, sizeof(why), "ALARM:%d", alarm);
        // Every sequence not yet finished: in flight, or with lines queued.
        for (int i = 0; i < SEQ_META; i++)
            if (s_meta[i].seq) abort_seq(s_meta[i].seq, why);
    }
    request_redisplay();
}

extern "C" void show_gcode_modes(struct gcode_modes* modes) {
    inInches = strcmp(modes->units, "In") == 0 || strcmp(modes->units, "G20") == 0;

    myModes = modes->wcs;
    myModes += " ";
    myModes += modes->units;
    myModes += " ";
    myModes += modes->distance;
    myModes += " ";
    myModes += modes->spindle;
    if (strcmp(modes->mist, "On") == 0) {
        myModes += " Mist";
    }
    if (strcmp(modes->flood, "On") == 0) {
        myModes += " Flood";
    }

    mySelectedTool = modes->tool;
    request_redisplay();
}

int disconnect_ms = 0;
int next_ping_ms  = 0;

// If we haven't heard from FluidNC in 4 seconds for some other reason,
// send a status report request.
const int ping_interval_ms = 4000;

// If we haven't heard from FluidNC in 6 seconds for any reason, declare
// FluidNC unresponsive.  After a ping, FluidNC has 2 seconds to respond.
const int disconnect_interval_ms = 6000;

bool starting = true;

// Counts consecutive disconnect_ms timeouts without RX, for the recovery ladder.
static int s_consecutive_timeouts = 0;

// Set by pendant_wait_for_fluidnc_ready() so fnc_is_connected() does not
// immediately fire a redundant ping right after a successful handshake.
static bool s_skip_first_ping = false;

void request_status_report() {
    fnc_putchar(0x11);           // XON; request software flow control
    fnc_realtime(StatusReport);  // Request fresh status
    next_ping_ms = milliseconds() + ping_interval_ms;
}

// Drain bytes until the RX line has been quiet for `quiet_ms`, OR until
// `max_total_ms` of wall time has elapsed (safety cap so a chatty
// peer can never wedge us). Bytes are discarded, NOT fed to the parser.
int flush_fnc_rx(uint32_t quiet_ms) {
    const uint32_t max_total_ms = 500;
    int            drained      = 0;
    uint32_t       start        = milliseconds();
    uint32_t       quiet_until  = start + quiet_ms;
    while ((int32_t)(milliseconds() - quiet_until) < 0) {
        if ((milliseconds() - start) >= max_total_ms) {
            break;
        }
        int c = fnc_getchar();
        if (c >= 0) {
            drained++;
            quiet_until = milliseconds() + quiet_ms;
        }
    }
    return drained;
}

// Probe FluidNC over UART until it answers, OR `budget_ms` elapses.
//
// IMPORTANT: bytes read here are DISCARDED, not fed to the parser.
// This function runs from setup() before activate_scene() has set
// current_scene, and many vendor parser callbacks (show_state, show_dro,
// show_alarm, show_error) eventually call current_scene methods. Feeding
// bytes to the parser before current_scene is set would crash on the
// first valid status report.
bool pendant_wait_for_fluidnc_ready(uint32_t budget_ms) {
    const uint32_t probe_window_ms = 500;
    const uint32_t probe_gap_ms    = 200;

    bootlog_printf("wait_ready: start budget=%lu", (unsigned long)budget_ms);

    int drained = flush_fnc_rx(50);
    bootlog_printf("wait_ready: drained=%d", drained);

    uint32_t deadline = milliseconds() + budget_ms;
    int      probe    = 0;
    int      total_rx = 0;
    while ((int32_t)(milliseconds() - deadline) < 0) {
        probe++;
        fnc_putchar(0x11);           // XON to clear any stale XOFF on FluidNC
        fnc_realtime(StatusReport);  // '?' probe

        int      window_rx  = 0;
        uint32_t window_end = milliseconds() + probe_window_ms;
        while ((int32_t)(milliseconds() - window_end) < 0) {
            int c = fnc_getchar();
            if (c >= 0) {
                window_rx++;  // discard byte; do not feed parser yet
            } else {
                delay_ms(2);
            }
        }
        total_rx += window_rx;
        if (window_rx > 0) {
            bootlog_printf("wait_ready: alive probe=%d bytes=%d", probe, window_rx);
            s_skip_first_ping = true;
            return true;
        }
        bootlog_printf("wait_ready: probe %d silent", probe);
        delay_ms(probe_gap_ms);
    }
    bootlog_printf("wait_ready: timeout probes=%d rx=%d", probe, total_rx);
    return false;
}

// Escalating recovery when the link has gone silent. Counter ticks once
// per disconnect_interval_ms with no RX.
//   tick 1: drain stale RX and send XON + status request.
//   tick 3: re-initialize the UART driver from scratch, then re-probe.
//           UART re-init is a no-op when running in WiFi mode.
static void recover_link(int tick) {
    bootlog_printf("recover: tick=%d", tick);
    flush_fnc_rx(20);
    request_status_report();
    if (tick == 3) {
#ifdef USE_WIFI
        if (wifi_use_uart_mode()) {
            bootlog_printf("recover: re-init uart");
            reinit_fnc_uart();
            request_status_report();
        }
#else
        bootlog_printf("recover: re-init uart");
        reinit_fnc_uart();
        request_status_report();
#endif
    }
}

bool fnc_is_connected() {
#ifdef DEV_SIMULATED_CONNECT
    return true;
#endif
    int now = milliseconds();
    if (starting) {
        starting      = false;
        disconnect_ms = now + (disconnect_interval_ms - ping_interval_ms);
        if (s_skip_first_ping) {
            // Handshake already got a response; let the regular ping cadence resume.
            s_skip_first_ping = false;
            next_ping_ms      = now + ping_interval_ms;
        } else {
            request_status_report();  // sets next_ping_ms
        }
        return false;             // Do we need a value for "unknown"?
    }
    if ((now - disconnect_ms) >= 0) {
        s_consecutive_timeouts++;
        bootlog_printf("disconnected: timeout #%d", s_consecutive_timeouts);
        recover_link(s_consecutive_timeouts);
        next_ping_ms  = now + ping_interval_ms;
        disconnect_ms = now + disconnect_interval_ms;
        return false;
    }

    if ((now - next_ping_ms) >= 0) {
        request_status_report();
    }
    return true;
}

void update_rx_time() {
    int now       = milliseconds();
    next_ping_ms  = now + ping_interval_ms;
    disconnect_ms = now + disconnect_interval_ms;
    s_consecutive_timeouts = 0;
}
