#include <unity.h>
#include <string.h>
#include <stdio.h>
#include "model/caps.h"

// The unit under test is compiled INTO this binary rather than added to
// [env:native]'s build_src_filter: that filter applies to every test, and
// caps.o's two external symbols would then be unresolved in the nine tests
// that have nothing to do with capabilities.
#include "../../src/model/caps.cpp"

// caps decides whether the HOME, PROBE and MACRO pages exist and whether the
// no-limits banner shows. Those are safety-relevant: offering $H on a machine
// with no endstops drives an axis into the frame at seek rate. Until now every
// path had only been exercised against one live config that happened to have a
// probe, homing cycles and limit pins — the negative cases, which are the ones
// that actually hide a dangerous control, were never proven.
//
// Feeding synthetic dumps here covers them without touching a running rig.

// caps.cpp queues "$CD" through the model and stamps times off GrblParserC's
// clock. Neither is what is under test, so both are stubbed. The fake clock is
// also what lets the quiescence timeout be tested without sleeping.
static int s_now = 0;
extern "C" int milliseconds() { return s_now; }
void send_line(const char* /*s*/, int /*timeout*/) {}

// Push a whole dump through, then let it settle the way the main loop would.
static void feed(const char* dump) {
    caps_request();
    char buf[4096];
    snprintf(buf, sizeof(buf), "%s", dump);
    for (char* line = strtok(buf, "\n"); line; line = strtok(nullptr, "\n")) {
        s_now += 1;
        caps_feed_line(line);
    }
    s_now += 1000;          // past the quiet window
    caps_tick();
}

void setUp() { s_now = 0; caps_reset(); }
void tearDown() {}

// A machine with everything: probe pin, homing cycles, limit switches.
static const char* FULL =
    "probe: \n"
    "  pin: 'gpio.39:low'\n"
    "macros: \n"
    "  Macro0: M8\n"
    "  Macro1: \n"
    "axes: \n"
    "  X: \n"
    "    soft_limits: true\n"
    "    homing: \n"
    "      cycle: 2\n"
    "    motor0: \n"
    "      limit_neg_pin: 'gpio.2:low:pu'\n";

// The bring-up shape: endstops physically absent, so every limit pin and the
// homing cycles are stripped, and no probe is wired.
static const char* STRIPPED =
    "probe: \n"
    "  pin: NO_PIN\n"
    "macros: \n"
    "  Macro0: \n"
    "  Macro1: \n"
    "axes: \n"
    "  X: \n"
    "    soft_limits: false\n"
    "    motor0: \n"
    "      limit_neg_pin: NO_PIN\n"
    "      limit_pos_pin: NO_PIN\n"
    "      hard_limits: false\n";

void test_full_machine_reports_everything_present() {
    feed(FULL);
    TEST_ASSERT_EQUAL(CAP_YES, caps_probe());
    TEST_ASSERT_EQUAL(CAP_YES, caps_homing());
    TEST_ASSERT_EQUAL(CAP_YES, caps_limits());
    TEST_ASSERT_EQUAL(CAP_YES, caps_macros());
    TEST_ASSERT_EQUAL_size_t(1, caps_macro_count());       // empty slots skipped
    TEST_ASSERT_EQUAL_STRING("Macro0", caps_macro_name(0));
    TEST_ASSERT_EQUAL_STRING("M8", caps_macro_body(0));
}

void test_stripped_machine_hides_every_optional_page() {
    feed(STRIPPED);
    TEST_ASSERT_EQUAL(CAP_NO, caps_probe());
    TEST_ASSERT_EQUAL(CAP_NO, caps_homing());
    TEST_ASSERT_EQUAL(CAP_NO, caps_limits());     // drives the warning banner
    TEST_ASSERT_EQUAL(CAP_NO, caps_macros());
    TEST_ASSERT_EQUAL_size_t(0, caps_macro_count());
}

// The bug this guards: homing was gated on the cycle alone, so a config that
// declares cycles but has had its endstops stripped still offered $H.
void test_homing_needs_a_switch_not_just_a_cycle() {
    feed("axes: \n"
         "  X: \n"
         "    homing: \n"
         "      cycle: 1\n"
         "    motor0: \n"
         "      limit_neg_pin: NO_PIN\n");
    TEST_ASSERT_EQUAL(CAP_NO, caps_homing());
}

// ...and the converse, so the check cannot be satisfied by switches alone.
void test_homing_needs_a_cycle_not_just_a_switch() {
    feed("axes: \n"
         "  X: \n"
         "    motor0: \n"
         "      limit_neg_pin: 'gpio.2:low'\n");
    TEST_ASSERT_EQUAL(CAP_NO, caps_homing());
}

// A dump that never arrives must leave everything UNKNOWN so the UI keeps its
// pages. Hiding a control on no evidence is the one failure an operator cannot
// diagnose by looking at the panel.
void test_no_dump_leaves_capabilities_unknown() {
    caps_request();
    TEST_ASSERT_EQUAL(CAP_UNKNOWN, caps_probe());
    s_now += 10000;
    caps_tick();
    TEST_ASSERT_TRUE(caps_settled());
    TEST_ASSERT_EQUAL(CAP_UNKNOWN, caps_probe());
    TEST_ASSERT_EQUAL(CAP_UNKNOWN, caps_homing());
    TEST_ASSERT_EQUAL(CAP_UNKNOWN, caps_limits());
}

// The $Limits/Show monitor emits ": ... P" twice a second. Matching it as
// config content held the quiet window open for ever and the boot gate never
// released; it must not count as dump content, nor be consumed from Diag.
void test_limit_monitor_stream_is_not_config() {
    caps_request();
    s_now += 1;
    TEST_ASSERT_FALSE(caps_feed_line(":                    P"));
    TEST_ASSERT_FALSE(caps_feed_line("<Idle|MPos:0.000,0.000,0.000>"));
    TEST_ASSERT_FALSE(caps_feed_line("[G54:0.000,0.000,0.000]"));
    s_now += 10000;
    caps_tick();
    // Nothing that looked like a tree arrived, so this is "no dump", not
    // "a dump that mentioned nothing".
    TEST_ASSERT_EQUAL(CAP_UNKNOWN, caps_probe());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_full_machine_reports_everything_present);
    RUN_TEST(test_stripped_machine_hides_every_optional_page);
    RUN_TEST(test_homing_needs_a_switch_not_just_a_cycle);
    RUN_TEST(test_homing_needs_a_cycle_not_just_a_switch);
    RUN_TEST(test_no_dump_leaves_capabilities_unknown);
    RUN_TEST(test_limit_monitor_stream_is_not_config);
    return UNITY_END();
}
