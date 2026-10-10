#include <unity.h>
#include <string.h>
#include "model/motors.h"
#include "model/caps.h"
#include "../host_stubs.h"

// The live machine: hard_limits on X, Y (both motors) and Z.
static void feed_machine() {
    caps_request_config();
    const char* L[] = {
        "axes:", "  x:", "    motor0:", "      hard_limits: true",
        "  y:", "    motor0:", "      hard_limits: true", "    motor1:", "      hard_limits: true",
        "  a:", "    motor0:", "      hard_limits: false",
    };
    for (const char* l : L) caps_feed_line(l);
    caps_reply_end();
}

void setUp() {
    caps_reset();
    feed_machine();
    if (motors_released()) motors_hold();
    stub_sent_reset();
}
void tearDown() {}

void test_release_disables_then_disarms_only_hard_limited_motors() {
    motors_release();
    TEST_ASSERT_TRUE(motors_released());
    TEST_ASSERT_EQUAL(4, stub_sent_count());
    TEST_ASSERT_EQUAL_STRING("$MD", stub_sent(0));
    TEST_ASSERT_EQUAL_STRING("$/axes/x/motor0/hard_limits=false", stub_sent(1));
    TEST_ASSERT_EQUAL_STRING("$/axes/y/motor1/hard_limits=false", stub_sent(3));
}

void test_motion_rearms_before_it_goes_out() {
    motors_release();
    stub_sent_reset();
    motors_guard("$J=G91X10F1000");
    TEST_ASSERT_FALSE(motors_released());
    TEST_ASSERT_EQUAL_STRING("$/axes/x/motor0/hard_limits=true", stub_sent(0));
    TEST_ASSERT_EQUAL_STRING("$ME", stub_sent(stub_sent_count() - 1));
}

void test_queries_do_not_rearm() {
    motors_release();
    motors_guard("$G");
    motors_guard("$X");
    motors_guard("$/axes/x/motor0/hard_limits=false");
    TEST_ASSERT_TRUE(motors_released());
}

void test_line_classifier() {
    TEST_ASSERT_TRUE(motors_line_can_move("G0 X10"));
    TEST_ASSERT_TRUE(motors_line_can_move("$H"));
    TEST_ASSERT_TRUE(motors_line_can_move("$rm=Macro1"));
    TEST_ASSERT_TRUE(motors_line_can_move("$SD/Run=/sd/a.nc"));
    TEST_ASSERT_FALSE(motors_line_can_move("$MD"));
    TEST_ASSERT_FALSE(motors_line_can_move("$#"));
}

void settings_set_float(const char* key, float v);

// Panel rebooted while released: the controller's dump now says false, so the
// motors to re-arm come from what was saved, not from caps.
void test_saved_disarm_is_rearmed_on_link() {
    caps_reset();
    settings_set_float("hl_off", 4.0f);   // bit 2 = Y motor0
    motors_on_link();
    TEST_ASSERT_EQUAL(2, stub_sent_count());
    TEST_ASSERT_EQUAL_STRING("$/axes/y/motor0/hard_limits=true", stub_sent(0));
    TEST_ASSERT_EQUAL_STRING("$ME", stub_sent(1));
    stub_sent_reset();
    motors_on_link();                     // cleared: does not repeat
    TEST_ASSERT_EQUAL(0, stub_sent_count());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_release_disables_then_disarms_only_hard_limited_motors);
    RUN_TEST(test_motion_rearms_before_it_goes_out);
    RUN_TEST(test_queries_do_not_rearm);
    RUN_TEST(test_line_classifier);
    RUN_TEST(test_saved_disarm_is_rearmed_on_link);
    return UNITY_END();
}
