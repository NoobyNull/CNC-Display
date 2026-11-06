#include "cmd/cmd.h"
#include "model/FluidNCModel.h"
#include "model/console_log.h"
#include "model/model_events.h"
#include "model/motors.h"
#include "model/sendq.h"
#include "model/sys_compat.h"
#include "GrblParserC.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

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

// Jog chunks are a real-time stream: one tick's worth of travel each,
// re-issued every 150 ms while a key is held. Deliberately NOT queued: backing
// them up would keep the axis moving after the finger lifts, which is the
// runaway the whole design avoids. If the controller has not acked the last
// one it is behind, so skip this chunk -- the axis pauses for a tick and
// catches up on the next.
void send_jog_line(const char* s) {
    // Steppers released: queue it behind the re-arm lines, and never let a
    // later chunk overtake lines still queued.
    if (motors_released()) { send_line(s); return; }
    if (s_sendq.count) return;
    if (send_busy()) return;
    send_now(s, 0);
    dbg_println(s);
}

void send_jog_cancel() {
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


// ---- the controller's answers (called from the parser callbacks) -------------

void cmd_ok() {
    seq_ok(s_inflight_seq);
    line_acked();
}

void cmd_error(int code) {
    const sendq_seq_t seq = s_inflight_seq;
    line_acked();
    char why[16];
    snprintf(why, sizeof(why), "error:%d", code);
    abort_seq(seq, why);
}

// An alarm stops the machine whatever line it came from. Every queued sequence
// is now running on wrong assumptions; plain lines ($A, $X, $G) stay, since
// they are how the alarm gets read and cleared.
void cmd_alarm(int code) {
    char why[16];
    snprintf(why, sizeof(why), "ALARM:%d", code);
    // Every sequence not yet finished: in flight, or with lines queued.
    for (int i = 0; i < SEQ_META; i++)
        if (s_meta[i].seq) abort_seq(s_meta[i].seq, why);
}
