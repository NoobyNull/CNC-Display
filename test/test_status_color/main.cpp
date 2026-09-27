#include <unity.h>
#include "model/status_color.h"

void setUp() {}
void tearDown() {}

void test_fault_states_are_red() {
    TEST_ASSERT_EQUAL_HEX32(0xFF0000, status_color(Alarm));
    TEST_ASSERT_EQUAL_HEX32(0xFF0000, status_color(Critical));
    TEST_ASSERT_EQUAL_HEX32(0xFF0000, status_color(ConfigAlarm));
}

void test_motion_is_green_and_idle_is_blue() {
    TEST_ASSERT_EQUAL_HEX32(0x00FF00, status_color(Cycle));
    TEST_ASSERT_EQUAL_HEX32(0x00FF00, status_color(Jog));
    TEST_ASSERT_EQUAL_HEX32(0x0000FF, status_color(Idle));
}

void test_hold_is_amber_and_disconnected_is_distinct() {
    TEST_ASSERT_EQUAL_HEX32(0xFFB000, status_color(Hold));
    TEST_ASSERT_NOT_EQUAL(status_color(Idle), status_color(Disconnected));
    TEST_ASSERT_NOT_EQUAL(status_color(Alarm), status_color(Disconnected));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_fault_states_are_red);
    RUN_TEST(test_motion_is_green_and_idle_is_blue);
    RUN_TEST(test_hold_is_amber_and_disconnected_is_distinct);
    return UNITY_END();
}
