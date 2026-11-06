// The panel's side of the controller link, end to end through GrblParserC:
// sequences, errors, alarms, RESET, link loss and controller restarts. The
// "controller" is a script: lines the panel writes are captured, replies are
// fed back through the real parser.
#include <unity.h>
#include <Arduino.h>
#include <string>
#include <vector>
#include <string.h>

#include "model/FluidNCModel.h"
#include "model/homed.h"
#include "model/model_events.h"
#include "model/console_log.h"
#include "model/model_compat.h"
#include "GrblParserC.h"

// ---- stand-ins for the hardware and the UI ------------------------------------
SimSerial Serial;
static int                      s_now = 1000;
static std::string              s_tx_line;
static std::vector<std::string> s_sent;     // complete lines, realtime bytes excluded
static std::vector<uint8_t>     s_realtime;
static std::string              s_rx;
static unsigned long            s_rx_count = 0;

extern "C" int milliseconds() { return s_now; }
extern "C" void fnc_putchar(uint8_t c) {
    if (c == '\n') { s_sent.push_back(s_tx_line); s_tx_line.clear(); return; }
    if (c < 0x20 || c == '?' || c >= 0x80) { s_realtime.push_back(c); return; }
    s_tx_line += (char)c;
}
extern "C" int fnc_getchar() {
    if (s_rx.empty()) return -1;
    int c = (uint8_t)s_rx[0];
    s_rx.erase(0, 1);
    ++s_rx_count;
    return c;
}
extern "C" bool fnc_rx_waiting() { return !s_rx.empty(); }
unsigned long fnc_rx_count() { return s_rx_count; }
void resetFlowControl() {}
bool page_diag_limits_feed(const char*) { return false; }
float settings_get_float(const char*, float def) { return def; }
void  settings_set_float(const char*, float) {}

// ---- driving it -------------------------------------------------------------
static void reply(const char* text) {
    s_rx += text;
    while (!s_rx.empty()) fnc_poll();
}
static void loop_once() { send_pump(); link_service(); }
static void advance(int ms) {
    for (int t = 0; t < ms; t += 10) { s_now += 10; loop_once(); }
}
// Answer "ok" to everything written so far, one at a time, the way FluidNC does.
static void ok_all() {
    for (int guard = 0; guard < 200; guard++) {
        size_t before = s_sent.size();
        loop_once();
        if (!send_busy()) { if (s_sent.size() == before) return; continue; }
        reply("ok\n");
    }
}
static int count_sent(const char* prefix) {
    int n = 0;
    for (auto& l : s_sent) if (l.rfind(prefix, 0) == 0) n++;
    return n;
}
static bool sent(const char* line) {
    for (auto& l : s_sent) if (l == line) return true;
    return false;
}
static bool console_has(const char* needle) {
    for (size_t i = 0; i < clog_count(); i++) if (strstr(clog_line(i)->text, needle)) return true;
    return false;
}

// Bring the link up: a status report, then ack the priming lines.
static void connect() {
    reply("<Idle|MPos:0.000,0.000,0.000|FS:0,0>\n");
    loop_once();
    ok_all();
}

void setUp() {
    s_sent.clear(); s_realtime.clear(); s_rx.clear(); s_tx_line.clear();
    send_flush(nullptr);
    clog_reset();
    if (state != Disconnected) {   // previous test left it up: drop it cleanly
        set_disconnected_state();
        loop_once();
    }
    advance(50);
    s_sent.clear();
}
void tearDown() {}

static void stage_seq(const char* name, int n) {
    seq_begin(name);
    seq_cleanup("G90");
    char b[32];
    for (int i = 0; i < n; i++) { snprintf(b, sizeof(b), "G0 X%d", i); seq_line(b); }
}

// ---- tests ------------------------------------------------------------------

void test_connect_primes_once() {
    connect();
    TEST_ASSERT_EQUAL(1, count_sent("$G"));
    TEST_ASSERT_EQUAL(1, count_sent("$RI=200"));
    TEST_ASSERT_EQUAL(1, count_sent("$#"));
    advance(500);
    ok_all();
    TEST_ASSERT_EQUAL(1, count_sent("$RI=200"));   // one priming path, not two
}

// The AutoZero bug: 28 lines through a 16-slot queue lost the last 11.
void test_long_sequence_arrives_whole_and_in_order() {
    connect();
    s_sent.clear();
    stage_seq("AutoZero", 28);
    TEST_ASSERT_TRUE(seq_send());
    ok_all();
    TEST_ASSERT_EQUAL(28, (int)s_sent.size());
    TEST_ASSERT_EQUAL_STRING("G0 X0", s_sent.front().c_str());
    TEST_ASSERT_EQUAL_STRING("G0 X27", s_sent.back().c_str());
    TEST_ASSERT_NULL(model_take_error());
}

void test_sequence_refused_without_link() {
    stage_seq("AutoZero", 3);
    TEST_ASSERT_FALSE(seq_send());
    TEST_ASSERT_NOT_NULL(strstr(model_take_error(), "no link"));
    TEST_ASSERT_EQUAL(0, (int)s_sent.size());
}

void test_error_stops_the_sequence_and_sends_cleanup() {
    connect();
    s_sent.clear();
    stage_seq("AutoZero", 10);
    seq_send();
    loop_once();
    reply("ok\n");        // G0 X0
    loop_once();
    reply("error:20\n");  // G0 X1 refused
    ok_all();
    TEST_ASSERT_TRUE(sent("G0 X1"));
    TEST_ASSERT_FALSE(sent("G0 X2"));
    TEST_ASSERT_TRUE(sent("G90"));
    const char* e = model_take_error();
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_NOT_NULL(strstr(e, "AutoZero stopped (error:20), 8 lines not sent"));
}

// A probe that misses is ALARM:4/5 while the G38.2 is still the line in flight.
void test_alarm_stops_the_sequence_but_keeps_plain_lines() {
    connect();
    s_sent.clear();
    stage_seq("AutoZero", 10);
    seq_send();
    loop_once();
    send_line("$A");   // a plain line queued behind it
    reply("ALARM:5\n");
    reply("error:9\n");
    ok_all();
    TEST_ASSERT_FALSE(sent("G0 X1"));
    TEST_ASSERT_TRUE(sent("$A"));
    TEST_ASSERT_TRUE(sent("G90"));
    TEST_ASSERT_EQUAL(1, count_sent("G90"));   // stopped once, not again on the error
}

// An error after a sequence has finished is not that sequence's business.
void test_finished_sequence_is_not_stopped_later() {
    connect();
    stage_seq("Probe", 2);
    seq_send();
    ok_all();
    model_take_error();
    s_sent.clear();
    reply("ALARM:1\n");
    ok_all();
    TEST_ASSERT_FALSE(sent("G90"));
    TEST_ASSERT_NULL(model_take_error());
}

void test_reset_discards_everything_queued() {
    connect();
    s_sent.clear();
    stage_seq("AutoZero", 10);
    seq_send();
    loop_once();               // G0 X0 written, 9 queued
    send_reset();
    TEST_ASSERT_EQUAL_UINT8(0x18, s_realtime.back());
    reply("Grbl 3.9 [FluidNC v3.9.1 (noradio) '$' for help]\n");
    ok_all();
    TEST_ASSERT_FALSE(sent("G0 X1"));
    TEST_ASSERT_TRUE(console_has("reset - 9 queued lines discarded"));
    // And the queue is usable straight away, no 2 s wait for a lost ack.
    s_sent.clear();
    send_line("$X");
    TEST_ASSERT_TRUE(sent("$X"));
}

void test_own_reset_keeps_homing_and_reprimes() {
    connect();
    reply("<Home|MPos:0.000,0.000,0.000|FS:0,0>\n");
    reply("<Idle|MPos:0.000,0.000,0.000|FS:0,0>\n");
    TEST_ASSERT_TRUE(homed());
    s_sent.clear();
    send_reset();
    reply("Grbl 3.9 [FluidNC v3.9.1 (noradio) '$' for help]\n");
    ok_all();
    TEST_ASSERT_TRUE(homed());
    TEST_ASSERT_EQUAL(1, count_sent("$RI=200"));
}

// The reboot bug: the banner went to handle_signon(), which nothing defined, so
// a restarted controller was never noticed. Position is no longer trusted.
void test_controller_restart_forgets_homing_and_reprimes() {
    connect();
    reply("<Home|MPos:0.000,0.000,0.000|FS:0,0>\n");
    reply("<Idle|MPos:0.000,0.000,0.000|FS:0,0>\n");
    TEST_ASSERT_TRUE(homed());
    stage_seq("AutoZero", 5);
    seq_send();
    loop_once();
    s_sent.clear();
    for (int i = 0; i < 5; i++) {   // well past the RESET window, link alive
        advance(1000);
        reply("<Idle|MPos:0.000,0.000,0.000|FS:0,0>\n");
    }
    reply("Grbl 3.9 [FluidNC v3.9.1 (noradio) '$' for help]\n");
    ok_all();
    TEST_ASSERT_FALSE(homed());
    TEST_ASSERT_FALSE(sent("G0 X1"));
    TEST_ASSERT_EQUAL(1, count_sent("$RI=200"));
    TEST_ASSERT_TRUE(console_has("controller restarted"));
}

// The link-loss bug: nothing ever called the liveness check, so a pulled cable
// left the last state and position on screen as if live.
void test_silence_means_no_link() {
    connect();
    reply("<Home|MPos:0.000,0.000,0.000|FS:0,0>\n");
    reply("<Idle|MPos:0.000,0.000,0.000|FS:0,0>\n");
    stage_seq("AutoZero", 5);
    seq_send();
    loop_once();
    s_realtime.clear();
    advance(3900);
    TEST_ASSERT_NOT_EQUAL(Disconnected, state);
    int pings = 0;
    for (uint8_t c : s_realtime) if (c == '?') pings++;
    TEST_ASSERT_TRUE(pings >= 2);
    advance(200);
    TEST_ASSERT_EQUAL(Disconnected, state);
    TEST_ASSERT_FALSE(homed());
    TEST_ASSERT_TRUE(console_has("link lost"));
    // Back again: a fresh priming, and nothing from before the drop is sent.
    s_sent.clear();
    connect();
    TEST_ASSERT_FALSE(sent("G0 X1"));
    TEST_ASSERT_EQUAL(1, count_sent("$RI=200"));
}

void test_quiet_but_answering_controller_stays_connected() {
    connect();
    for (int i = 0; i < 20; i++) {
        advance(1000);
        reply("<Idle|MPos:0.000,0.000,0.000|FS:0,0>\n");   // answers each ping
    }
    TEST_ASSERT_EQUAL(Idle, state);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_connect_primes_once);
    RUN_TEST(test_long_sequence_arrives_whole_and_in_order);
    RUN_TEST(test_sequence_refused_without_link);
    RUN_TEST(test_error_stops_the_sequence_and_sends_cleanup);
    RUN_TEST(test_alarm_stops_the_sequence_but_keeps_plain_lines);
    RUN_TEST(test_finished_sequence_is_not_stopped_later);
    RUN_TEST(test_reset_discards_everything_queued);
    RUN_TEST(test_own_reset_keeps_homing_and_reprimes);
    RUN_TEST(test_controller_restart_forgets_homing_and_reprimes);
    RUN_TEST(test_silence_means_no_link);
    RUN_TEST(test_quiet_but_answering_controller_stays_connected);
    return UNITY_END();
}
