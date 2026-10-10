#include <unity.h>
#include "model/homed.h"

void setUp() { homed_forget(); }
void tearDown() {}

void test_unknown_until_a_cycle_completes() {
    TEST_ASSERT_FALSE(homed());
    homed_note_state(true, false, false);
    TEST_ASSERT_TRUE(homed());
}

void test_failed_homing_is_not_homed() {
    homed_note_state(true, true, false);
    TEST_ASSERT_FALSE(homed());
}

void test_position_losing_alarm_forgets_and_bumps_epoch() {
    homed_note_state(true, false, false);
    const uint32_t e = homed_epoch();
    homed_note_alarm(1);
    TEST_ASSERT_FALSE(homed());
    TEST_ASSERT_NOT_EQUAL(e, homed_epoch());
}

void test_soft_limit_keeps_position() {
    homed_note_state(true, false, false);
    homed_note_alarm(2);
    TEST_ASSERT_TRUE(homed());
}

void test_link_loss_forgets() {
    homed_note_state(true, false, false);
    homed_note_state(false, false, true);
    TEST_ASSERT_FALSE(homed());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_unknown_until_a_cycle_completes);
    RUN_TEST(test_failed_homing_is_not_homed);
    RUN_TEST(test_position_losing_alarm_forgets_and_bumps_epoch);
    RUN_TEST(test_soft_limit_keeps_position);
    RUN_TEST(test_link_loss_forgets);
    return UNITY_END();
}
