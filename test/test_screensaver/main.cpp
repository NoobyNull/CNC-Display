#include <unity.h>
#include "model/screensaver.h"

void setUp() { screensaver_reset(); }
void tearDown() {}

static const uint32_t FIVE_MIN = 5UL * 60000UL;

void test_blanks_once_the_timeout_elapses() {
    screensaver_update(FIVE_MIN - 1, 5, false);
    TEST_ASSERT_FALSE(screensaver_is_off());
    screensaver_update(FIVE_MIN, 5, false);
    TEST_ASSERT_TRUE(screensaver_is_off());
}

void test_zero_minutes_never_blanks() {
    screensaver_update(24UL * 60UL * 60000UL, 0, false);
    TEST_ASSERT_FALSE(screensaver_is_off());
}

void test_a_busy_machine_never_blanks() {
    screensaver_update(FIVE_MIN * 10, 5, true);
    TEST_ASSERT_FALSE(screensaver_is_off());
}

// The alarm case: the panel is already dark when the machine stops being idle.
void test_going_busy_wakes_a_blanked_panel() {
    screensaver_update(FIVE_MIN, 5, false);
    TEST_ASSERT_TRUE(screensaver_is_off());
    screensaver_update(FIVE_MIN, 5, true);
    TEST_ASSERT_FALSE(screensaver_is_off());
}

// The one that matters: RESET is on the safety bar of every page.
void test_the_wake_tap_never_reaches_the_ui() {
    screensaver_update(FIVE_MIN, 5, false);
    TEST_ASSERT_TRUE(screensaver_is_off());

    TEST_ASSERT_FALSE(screensaver_touch_allowed(true));   // the tap that wakes
    TEST_ASSERT_FALSE(screensaver_is_off());              // backlight is back
}

void test_a_press_held_across_the_wake_stays_swallowed() {
    screensaver_update(FIVE_MIN, 5, false);
    screensaver_touch_allowed(true);
    // Finger still down over several polls: none of them may be delivered.
    for (int i = 0; i < 20; i++) {
        TEST_ASSERT_FALSE(screensaver_touch_allowed(true));
    }
    TEST_ASSERT_TRUE(screensaver_touch_allowed(false));   // lift
    TEST_ASSERT_TRUE(screensaver_touch_allowed(true));    // a fresh press counts
}

void test_touches_pass_through_when_awake() {
    TEST_ASSERT_TRUE(screensaver_touch_allowed(false));
    TEST_ASSERT_TRUE(screensaver_touch_allowed(true));
    TEST_ASSERT_TRUE(screensaver_touch_allowed(true));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_blanks_once_the_timeout_elapses);
    RUN_TEST(test_zero_minutes_never_blanks);
    RUN_TEST(test_a_busy_machine_never_blanks);
    RUN_TEST(test_going_busy_wakes_a_blanked_panel);
    RUN_TEST(test_the_wake_tap_never_reaches_the_ui);
    RUN_TEST(test_a_press_held_across_the_wake_stays_swallowed);
    RUN_TEST(test_touches_pass_through_when_awake);
    return UNITY_END();
}
