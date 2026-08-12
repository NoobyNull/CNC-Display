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

// --- smoothing ---

void test_smoothed_peak_lags_the_raw_peak_on_an_impact() {
    uint32_t t = 1000;
    feed_steady(0.01f, 100, &t);
    vib_feed(1.0f + 3.0f, t); t += 1000;
    // One sample in, the raw peak is already at the impact while the EMA is only
    // 10% of the way there. That lag is the whole point of the smoothed figure.
    TEST_ASSERT_GREATER_THAN_FLOAT(vib_result()->smoothed_peak, vib_result()->peak);
    TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, vib_result()->smoothed_peak);
}

void test_smoothed_peak_is_slower_than_smoothed_rms() {
    uint32_t t = 1000;
    vib_reset(1.0f);
    // Same step fed to both: with alpha 0.10 vs 0.15 the peak EMA must rise to a
    // smaller fraction of its input than the rms EMA does to its own.
    for (int i = 0; i < 20; i++) { vib_feed(1.0f + 1.0f, t); t += 1000; }
    const vib_result_t* v = vib_result();
    TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, v->smoothed_peak);
    TEST_ASSERT_TRUE(v->smoothed_peak / v->peak < v->smoothed_rms / v->rms);
}

void test_smoothing_state_clears_on_reset() {
    uint32_t t = 1000;
    feed_steady(1.0f, 200, &t);
    TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, vib_result()->smoothed_peak);
    vib_reset(1.0f);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, vib_result()->smoothed_peak);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, vib_result()->smoothed_rms);
}

// --- jerk histogram ---

void test_histogram_bins_scale_with_the_threshold() {
    // The threshold must land on the boundary of VIB_HIST_THRESH_BIN, otherwise
    // "past the line" cannot be read off the chart by eye.
    vib_set_thresholds(0.5f, 4.0f, 50.0f);
    TEST_ASSERT_EQUAL_FLOAT(50.0f / VIB_HIST_THRESH_BIN, vib_hist_bin_width());
    vib_set_thresholds(0.5f, 4.0f, 200.0f);
    TEST_ASSERT_EQUAL_FLOAT(200.0f / VIB_HIST_THRESH_BIN, vib_hist_bin_width());
}

void test_histogram_counts_quiet_samples_in_the_low_bins() {
    uint32_t t = 1000;
    feed_steady(0.01f, 200, &t);
    const uint32_t* h = vib_hist();
    TEST_ASSERT_GREATER_THAN_UINT32(0, h[0]);
    // Nothing that gentle may reach the e-stop boundary.
    for (int i = VIB_HIST_THRESH_BIN; i < VIB_HIST_BINS; i++) {
        TEST_ASSERT_EQUAL_UINT32(0, h[i]);
    }
}

void test_histogram_saturates_instead_of_dropping_an_off_scale_impact() {
    uint32_t t = 1000;
    feed_steady(0.01f, 50, &t);
    vib_hist_reset();
    // One enormous step: jerk far past the top bin's range.
    vib_feed(500.0f, t);
    TEST_ASSERT_EQUAL_UINT32(1, vib_hist()[VIB_HIST_BINS - 1]);
}

void test_histogram_reset_clears_counts_but_not_detection() {
    uint32_t t = 1000;
    feed_steady(1.2f, 600, &t);
    TEST_ASSERT_GREATER_THAN_UINT32(0, vib_hist_max());
    vib_level_t before = vib_result()->level;
    vib_hist_reset();
    TEST_ASSERT_EQUAL_UINT32(0, vib_hist_max());
    TEST_ASSERT_EQUAL_INT(before, vib_result()->level);
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
    RUN_TEST(test_smoothed_peak_lags_the_raw_peak_on_an_impact);
    RUN_TEST(test_smoothed_peak_is_slower_than_smoothed_rms);
    RUN_TEST(test_smoothing_state_clears_on_reset);
    RUN_TEST(test_histogram_bins_scale_with_the_threshold);
    RUN_TEST(test_histogram_counts_quiet_samples_in_the_low_bins);
    RUN_TEST(test_histogram_saturates_instead_of_dropping_an_off_scale_impact);
    RUN_TEST(test_histogram_reset_clears_counts_but_not_detection);
    return UNITY_END();
}
