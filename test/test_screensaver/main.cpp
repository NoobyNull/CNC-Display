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

    TEST_ASSERT_FALSE(screensaver_touch_allowed(true, 1000));  // the tap that wakes
    TEST_ASSERT_FALSE(screensaver_is_off());                   // backlight is back
}

void test_a_press_held_across_the_wake_stays_swallowed() {
    screensaver_update(FIVE_MIN, 5, false);
    screensaver_touch_allowed(true, 1000);
    // Finger still down over several polls: none of them may be delivered.
    for (uint32_t t = 1000; t < 1500; t += 50) {
        TEST_ASSERT_FALSE(screensaver_touch_allowed(true, t));
    }
    TEST_ASSERT_TRUE(screensaver_touch_allowed(false, 1550));  // lift
    TEST_ASSERT_TRUE(screensaver_touch_allowed(true, 1600));   // a fresh press counts
}

// The bug that killed touch on real hardware: the GT911 holds its last state
// when it stops producing fresh scans, so touch_down stayed asserted, the
// release never came, and every sample was swallowed for ever. The swallow must
// time out on its own or the panel needs a power cycle to get its touch back.
void test_a_touch_stuck_down_cannot_disable_touch_for_ever() {
    screensaver_update(FIVE_MIN, 5, false);
    TEST_ASSERT_FALSE(screensaver_touch_allowed(true, 1000));   // wake tap

    // Never released. Within the cap it is still swallowed...
    TEST_ASSERT_FALSE(screensaver_touch_allowed(true, 1400));
    // ...but past it, input has to start flowing again regardless.
    TEST_ASSERT_TRUE(screensaver_touch_allowed(true, 1700));
    TEST_ASSERT_TRUE(screensaver_touch_allowed(true, 5000));
    TEST_ASSERT_TRUE(screensaver_touch_allowed(true, 60000));
}

// millis() wraps every 49 days. A panel left on must not lose its touch.
void test_the_swallow_cap_survives_a_millis_wrap() {
    screensaver_update(FIVE_MIN, 5, false);
    const uint32_t near_wrap = 0xFFFFFF00u;
    TEST_ASSERT_FALSE(screensaver_touch_allowed(true, near_wrap));
    TEST_ASSERT_FALSE(screensaver_touch_allowed(true, near_wrap + 100));
    TEST_ASSERT_TRUE(screensaver_touch_allowed(true, near_wrap + 700));  // wrapped
}

void test_touches_pass_through_when_awake() {
    TEST_ASSERT_TRUE(screensaver_touch_allowed(false, 100));
    TEST_ASSERT_TRUE(screensaver_touch_allowed(true, 150));
    TEST_ASSERT_TRUE(screensaver_touch_allowed(true, 200));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_blanks_once_the_timeout_elapses);
    RUN_TEST(test_zero_minutes_never_blanks);
    RUN_TEST(test_a_busy_machine_never_blanks);
    RUN_TEST(test_going_busy_wakes_a_blanked_panel);
    RUN_TEST(test_the_wake_tap_never_reaches_the_ui);
    RUN_TEST(test_a_press_held_across_the_wake_stays_swallowed);
    RUN_TEST(test_a_touch_stuck_down_cannot_disable_touch_for_ever);
    RUN_TEST(test_the_swallow_cap_survives_a_millis_wrap);
    RUN_TEST(test_touches_pass_through_when_awake);
    return UNITY_END();
}
