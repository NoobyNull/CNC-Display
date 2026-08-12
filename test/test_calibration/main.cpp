#include <unity.h>
#include "model/calibration.h"

void setUp() { cal_reset(); }
void tearDown() {}

void test_scales_by_the_measured_error() {
    // Told it 10, it moved 9.87 -> the axis needs MORE steps per mm.
    float v = cal_new_steps(40.0f, 10.0f, 9.87f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 40.5268f, v);
    TEST_ASSERT_TRUE(v > 40.0f);
}

void test_overshoot_reduces_steps() {
    float v = cal_new_steps(40.0f, 10.0f, 10.25f);
    TEST_ASSERT_TRUE(v < 40.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 39.0244f, v);
}

void test_a_perfect_move_changes_nothing() {
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 40.0f, cal_new_steps(40.0f, 10.0f, 10.0f));
}

// A measured value of zero is the "I pressed apply before typing" case.
void test_rejects_nonsense_inputs() {
    TEST_ASSERT_EQUAL_FLOAT(0.0f, cal_new_steps(40.0f, 10.0f, 0.0f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, cal_new_steps(40.0f, 10.0f, -5.0f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, cal_new_steps(40.0f, 0.0f, 10.0f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, cal_new_steps(0.0f, 10.0f, 10.0f));
}

// Out of FluidNC's own accepted range -> refuse rather than send a value the
// controller will reject, or worse, accept.
void test_rejects_results_outside_fluidnc_range() {
    TEST_ASSERT_EQUAL_FLOAT(0.0f, cal_new_steps(40.0f, 10.0f, 0.000001f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, cal_new_steps(0.002f, 1.0f, 100000.0f));
}

void test_captures_a_steps_reply() {
    cal_feed_line("$/axes/x/steps_per_mm=40.000");
    TEST_ASSERT_TRUE(cal_have_steps(0));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 40.0f, cal_steps(0));
    TEST_ASSERT_FALSE(cal_have_steps(1));
}

void test_captures_every_axis_and_tolerates_spelling() {
    cal_feed_line("$/axes/y/steps_per_mm=31.910");
    cal_feed_line("$axes/z/steps_per_mm=50.0");        // no leading slash
    cal_feed_line("$/AXES/A/STEPS_PER_MM=11.111");     // upper case
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 31.910f, cal_steps(1));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 50.0f,   cal_steps(2));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 11.111f, cal_steps(3));
}

void test_ignores_other_lines() {
    cal_feed_line("<Idle|MPos:0.000,0.000,0.000|FS:0,0>");
    cal_feed_line("$/axes/x/max_travel_mm=800.000");
    cal_feed_line("$Report/Status=1");
    cal_feed_line("ok");
    cal_feed_line("");
    cal_feed_line(nullptr);
    for (int i = 0; i < CAL_AXES; i++) TEST_ASSERT_FALSE(cal_have_steps(i));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_scales_by_the_measured_error);
    RUN_TEST(test_overshoot_reduces_steps);
    RUN_TEST(test_a_perfect_move_changes_nothing);
    RUN_TEST(test_rejects_nonsense_inputs);
    RUN_TEST(test_rejects_results_outside_fluidnc_range);
    RUN_TEST(test_captures_a_steps_reply);
    RUN_TEST(test_captures_every_axis_and_tolerates_spelling);
    RUN_TEST(test_ignores_other_lines);
    return UNITY_END();
}
