#pragma once
#include <stdint.h>
#include <stddef.h>

// Vibration/impact detection, ported from CNC-TW's analysis.cpp. Pure logic so
// it can be tested on the host: this decides whether to stop a moving machine,
// which is not something to find out about on the bench.

#define VIB_WINDOW 128          // samples in the RMS window

enum vib_level_t { VIB_NONE = 0, VIB_HOLD = 1, VIB_ESTOP = 2 };

enum vib_mode_t {
    VIB_MODE_MONITOR = 0,   // show only, never touch the machine
    VIB_MODE_SOFT    = 1,   // feed hold on sustained vibration or impact
    VIB_MODE_HARD    = 2,   // feed hold on vibration, soft reset on impact
};

struct vib_result_t {
    float rms;          // g, RMS deviation from baseline over the window
    float peak;         // g, max absolute deviation, decayed
    float jerk;         // g/s, rate of change of acceleration magnitude
    float smoothed;     // EMA of rms
    vib_level_t level;
};

void vib_reset(float baseline_g);

// magnitude in g, timestamp in microseconds.
void vib_feed(float accel_magnitude_g, uint32_t t_us);

const vib_result_t* vib_result();
size_t vib_samples();          // samples seen since reset, for warm-up display

void  vib_set_thresholds(float hold_rms_g, float estop_peak_g, float estop_jerk_gps);
float vib_thresh_hold();
float vib_thresh_peak();
float vib_thresh_jerk();
