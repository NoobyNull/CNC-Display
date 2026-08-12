#include "model/vibration.h"
#include <math.h>
#include <string.h>

// Defaults from CNC-TW config.h — arrived at on a real machine, so they are a
// far better starting point than anything invented here.
static float s_thresh_hold = 0.5f;    // g, sustained RMS -> feed hold
static float s_thresh_peak = 4.0f;    // g, impact spike -> e-stop
static float s_thresh_jerk = 50.0f;   // g/s, sudden change -> e-stop

// Two smoothing rates, as in CNC-TW. The peak is smoothed more slowly than the
// RMS deliberately: RMS already averages over a 128-sample window, while peak is
// a single decaying extreme that jumps the instant anything is struck. A slower
// EMA is what makes it usable as a trend rather than a twitching number.
static const float EMA_ALPHA      = 0.15f;   // CNC-TW EMA_ALPHA_RMS
static const float EMA_ALPHA_PEAK = 0.10f;   // CNC-TW EMA_ALPHA_PEAK

// CNC-TW's PEAK_DECAY_FACTOR. This was ported as 0.98 and that is far too fast:
// per-sample decay compounds, so at the 200Hz sample rate 0.98^200 = 0.017 wipes
// an impact to under 2% within a second, where 0.995^200 = 0.37 keeps 37% of it
// visible. The peak exists precisely so a spike survives long enough to be seen
// and acted on, and the fast value defeated that.
static const float PEAK_DECAY = 0.995f;

static float        s_window[VIB_WINDOW];
static size_t       s_wpos = 0;
static size_t       s_count = 0;
static size_t       s_total = 0;
static float        s_baseline = 1.0f;
static float        s_prev_mag = 1.0f;
static uint32_t     s_prev_us = 0;
static float        s_ema = 0.0f;
static float        s_ema_peak = 0.0f;
static vib_result_t s_res;
static uint32_t     s_hist[VIB_HIST_BINS];

float vib_hist_bin_width() { return s_thresh_jerk / (float)VIB_HIST_THRESH_BIN; }
const uint32_t* vib_hist() { return s_hist; }

uint32_t vib_hist_max() {
    uint32_t m = 0;
    for (int i = 0; i < VIB_HIST_BINS; i++) if (s_hist[i] > m) m = s_hist[i];
    return m;
}

void vib_hist_reset() { memset(s_hist, 0, sizeof(s_hist)); }

// Counts the current jerk figure. Saturates into the top bin rather than
// discarding: an off-scale impact is the single most interesting sample there
// is, and dropping it would make the chart quietest exactly when it matters.
static void hist_add(float jerk_gps) {
    float w = vib_hist_bin_width();
    if (w <= 0.0f) return;
    int bin = (int)(jerk_gps / w);
    if (bin < 0) bin = 0;
    if (bin >= VIB_HIST_BINS) bin = VIB_HIST_BINS - 1;
    if (s_hist[bin] != UINT32_MAX) s_hist[bin]++;
}

void vib_set_thresholds(float hold_rms_g, float estop_peak_g, float estop_jerk_gps) {
    if (hold_rms_g   > 0) s_thresh_hold = hold_rms_g;
    if (estop_peak_g > 0) s_thresh_peak = estop_peak_g;
    if (estop_jerk_gps > 0) s_thresh_jerk = estop_jerk_gps;
}
float vib_thresh_hold() { return s_thresh_hold; }
float vib_thresh_peak() { return s_thresh_peak; }
float vib_thresh_jerk() { return s_thresh_jerk; }

void vib_reset(float baseline_g) {
    memset(s_window, 0, sizeof(s_window));
    s_wpos = s_count = s_total = 0;
    s_baseline = baseline_g;
    s_prev_mag = baseline_g;
    s_prev_us  = 0;
    s_ema      = 0.0f;
    s_ema_peak = 0.0f;
    memset(&s_res, 0, sizeof(s_res));
    s_res.level = VIB_NONE;
    vib_hist_reset();
}

size_t vib_samples() { return s_total; }
const vib_result_t* vib_result() { return &s_res; }

void vib_feed(float mag_g, uint32_t t_us) {
    float dev     = mag_g - s_baseline;
    float abs_dev = fabsf(dev);
    s_total++;

    s_window[s_wpos] = dev;
    s_wpos = (s_wpos + 1) % VIB_WINDOW;
    if (s_count < VIB_WINDOW) s_count++;

    // Peak holds then decays, so a single impact stays visible for a moment
    // instead of vanishing before the next display tick.
    if (abs_dev > s_res.peak) s_res.peak = abs_dev;
    else                      s_res.peak *= PEAK_DECAY;

    if (s_prev_us > 0 && t_us > s_prev_us) {
        float dt = (t_us - s_prev_us) / 1000000.0f;
        if (dt > 0.0f) {
            s_res.jerk = fabsf(mag_g - s_prev_mag) / dt;
            // Only bin once a real interval exists — the first sample after a
            // reset has no previous timestamp and would otherwise pile a
            // meaningless zero into bin 0 on every restart.
            hist_add(s_res.jerk);
        }
    }
    s_prev_mag = mag_g;
    s_prev_us  = t_us;

    double sum_sq = 0.0;
    for (size_t i = 0; i < s_count; i++) sum_sq += (double)s_window[i] * s_window[i];
    s_res.rms = s_count ? sqrtf((float)(sum_sq / s_count)) : 0.0f;

    s_ema      = EMA_ALPHA      * s_res.rms  + (1.0f - EMA_ALPHA)      * s_ema;
    s_ema_peak = EMA_ALPHA_PEAK * s_res.peak + (1.0f - EMA_ALPHA_PEAK) * s_ema_peak;
    s_res.smoothed_rms  = s_ema;
    s_res.smoothed_peak = s_ema_peak;

    // Impact beats sustained vibration: a spike is the more urgent condition,
    // judged on instantaneous peak and jerk rather than the smoothed RMS, which
    // by design lags exactly when you need it not to.
    //
    // Jerk alone is not enough. Jerk is a per-sample delta divided by the sample
    // interval, so ANY vibration big enough to reach the 0.5g RMS hold threshold
    // also produces jerk far above 50 g/s — at 200Hz a 0.5g swing is already
    // 100 g/s. Triggering on jerk alone makes VIB_HOLD unreachable and turns
    // every rough cut into an e-stop. An impact is fast AND large, so jerk only
    // counts when the deviation is at least half the peak threshold.
    bool big_and_fast = s_res.jerk > s_thresh_jerk && abs_dev >= s_thresh_peak * 0.5f;

    if (s_res.peak > s_thresh_peak || big_and_fast) {
        s_res.level = VIB_ESTOP;
    } else if (s_res.rms > s_thresh_hold) {
        s_res.level = VIB_HOLD;
    } else {
        s_res.level = VIB_NONE;
    }
}
