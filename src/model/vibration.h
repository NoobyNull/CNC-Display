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
    float rms;            // g, RMS deviation from baseline over the window
    float peak;           // g, max absolute deviation, decayed
    float jerk;           // g/s, rate of change of acceleration magnitude
    float smoothed_rms;   // EMA of rms   (CNC-TW EMA_ALPHA_RMS  = 0.15)
    float smoothed_peak;  // EMA of peak  (CNC-TW EMA_ALPHA_PEAK = 0.10)
    vib_level_t level;
};

void vib_reset(float baseline_g);

// magnitude in g, timestamp in microseconds.
void vib_feed(float accel_magnitude_g, uint32_t t_us);

const vib_result_t* vib_result();
size_t vib_samples();          // samples seen since reset, for warm-up display

// --- jerk histogram -----------------------------------------------------------
// Every sample fed in is binned by jerk, so the display can show the shape of
// what the machine is actually doing rather than one flickering instantaneous
// number. Bins are sized off the e-stop jerk threshold rather than a fixed g/s
// span, so the chart stays meaningful when the threshold is retuned and the
// threshold always lands on the boundary of bin VIB_HIST_THRESH_BIN.
#define VIB_HIST_BINS        12
#define VIB_HIST_THRESH_BIN  4     // bin index where jerk == the e-stop threshold

const uint32_t* vib_hist();        // VIB_HIST_BINS counts, [0] is the quietest
uint32_t        vib_hist_max();    // tallest bin, for scaling the chart
float           vib_hist_bin_width();   // g/s covered by each bin
void            vib_hist_reset();  // counts only; leaves detection state alone

void  vib_set_thresholds(float hold_rms_g, float estop_peak_g, float estop_jerk_gps);
float vib_thresh_hold();
float vib_thresh_peak();
float vib_thresh_jerk();
