#include <unity.h>
#include "model/vibration.h"
#include <math.h>

void setUp() {
    vib_set_thresholds(0.5f, 4.0f, 50.0f);
    vib_reset(1.0f);
}
void tearDown() {}

// Feed n samples at 1kHz around the baseline with the given deviation amplitude.
// A sinusoid at 40Hz sampled at 1kHz. Real vibration is band-limited; a square
// wave alternating every sample has infinite slew and is not a signal any
// accelerometer produces. RMS of a sine of amplitude A is A/sqrt(2).
static void feed_steady(float amp, int n, uint32_t* t) {
    static float phase = 0.0f;
    for (int i = 0; i < n; i++) {
        phase += 2.0f * (float)M_PI * 40.0f / 1000.0f;
        vib_feed(1.0f + amp * sinf(phase), *t);
        *t += 1000;
    }
}

void test_quiet_machine_reports_nothing() {
    uint32_t t = 1000;
    feed_steady(0.0f, 200, &t);
    TEST_ASSERT_EQUAL_INT(VIB_NONE, vib_result()->level);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, vib_result()->rms);
}

void test_light_vibration_stays_below_hold() {
    uint32_t t = 1000;
    feed_steady(0.3f, 400, &t);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.3f / 1.41421f, vib_result()->rms);
    TEST_ASSERT_EQUAL_INT(VIB_NONE, vib_result()->level);
}

void test_sustained_vibration_raises_hold() {
    uint32_t t = 1000;
    feed_steady(1.2f, 400, &t);   // RMS ~0.85g, over the 0.5g hold line
    TEST_ASSERT_EQUAL_INT(VIB_HOLD, vib_result()->level);
}

void test_single_impact_raises_estop_not_hold() {
    uint32_t t = 1000;
    feed_steady(0.0f, 130, &t);
    vib_feed(1.0f + 5.0f, t);            // 5g spike
    TEST_ASSERT_EQUAL_INT(VIB_ESTOP, vib_result()->level);
}

void test_impact_beats_sustained_vibration() {
    uint32_t t = 1000;
    feed_steady(1.2f, 400, &t);          // already in HOLD
    vib_feed(1.0f + 6.0f, t);
    TEST_ASSERT_EQUAL_INT(VIB_ESTOP, vib_result()->level);
}

void test_fast_jerk_raises_estop_even_below_peak() {
    uint32_t t = 1000;
    feed_steady(0.0f, 130, &t);
    // 3g change in 1ms = 3000 g/s, and 3g deviation clears the magnitude gate,
    // so this is an impact even though peak stays under the 4g threshold.
    vib_feed(4.0f, t);
    TEST_ASSERT_GREATER_THAN_FLOAT(50.0f, vib_result()->jerk);
    TEST_ASSERT_EQUAL_INT(VIB_ESTOP, vib_result()->level);
}

void test_peak_decays_so_a_spike_does_not_latch_forever() {
    uint32_t t = 1000;
    vib_feed(1.0f + 5.0f, t); t += 1000;
    float after_spike = vib_result()->peak;
    feed_steady(0.0f, 300, &t);
    TEST_ASSERT_LESS_THAN_FLOAT(after_spike, vib_result()->peak);
    TEST_ASSERT_EQUAL_INT(VIB_NONE, vib_result()->level);
}

void test_thresholds_are_configurable() {
    vib_set_thresholds(0.1f, 4.0f, 50.0f);
    vib_reset(1.0f);
    uint32_t t = 1000;
    feed_steady(0.3f, 400, &t);
    TEST_ASSERT_EQUAL_INT(VIB_HOLD, vib_result()->level);   // 0.2 > 0.1 now
}

void test_baseline_offset_is_removed() {
    vib_reset(0.5f);                     // sensor reading 0.5g at rest
    uint32_t t = 1000;
    for (int i = 0; i < 200; i++) { vib_feed(0.5f, t); t += 1000; }
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, vib_result()->rms);
    TEST_ASSERT_EQUAL_INT(VIB_NONE, vib_result()->level);
}

void test_recovers_to_none_when_vibration_stops() {
    uint32_t t = 1000;
    feed_steady(1.2f, 400, &t);   // RMS ~0.85g, over the 0.5g hold line
    TEST_ASSERT_EQUAL_INT(VIB_HOLD, vib_result()->level);
    feed_steady(0.0f, 200, &t);
    TEST_ASSERT_EQUAL_INT(VIB_NONE, vib_result()->level);
}

// The regression this whole gate exists for.
void test_sustained_vibration_does_not_escalate_to_estop() {
    uint32_t t = 1000;
    feed_steady(1.2f, 600, &t);
    TEST_ASSERT_GREATER_THAN_FLOAT(50.0f, vib_result()->jerk);   // jerk IS high
    TEST_ASSERT_EQUAL_INT(VIB_HOLD, vib_result()->level);        // still only HOLD
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_quiet_machine_reports_nothing);
    RUN_TEST(test_light_vibration_stays_below_hold);
    RUN_TEST(test_sustained_vibration_raises_hold);
    RUN_TEST(test_single_impact_raises_estop_not_hold);
    RUN_TEST(test_impact_beats_sustained_vibration);
    RUN_TEST(test_fast_jerk_raises_estop_even_below_peak);
    RUN_TEST(test_peak_decays_so_a_spike_does_not_latch_forever);
    RUN_TEST(test_thresholds_are_configurable);
    RUN_TEST(test_baseline_offset_is_removed);
    RUN_TEST(test_recovers_to_none_when_vibration_stops);
    RUN_TEST(test_sustained_vibration_does_not_escalate_to_estop);
    return UNITY_END();
}
