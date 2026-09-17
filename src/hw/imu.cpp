#include "hw/imu.h"
#include "hw/touch.h"           // i2c_take / i2c_give
#include "model/vibration.h"
#include "model/imu_log.h"
#include "model/settings.h"
#include <Arduino.h>
#include <Wire.h>
#include <ICM_20948.h>

// 200Hz, matching CNC-TW. Fast enough to catch an impact, slow enough that the
// I2C transaction does not starve touch polling on the shared bus.
// 200Hz was costing far more than it bought. getAGMT() reads ~23 bytes, so at
// 200Hz the IMU held the shared I2C bus for roughly 12% of wall clock, from
// inside loop(), directly delaying lvgl_tick() — a visibly sluggish UI to
// oversample a guard whose thresholds are an EMA and a peak-hold.
//
// Not lower than this, though. The guard exists to catch an impact, jerk is
// computed from the real interval so the arithmetic stays correct at any rate,
// but the BANDWIDTH does not: 50Hz still resolves 25Hz, while 10Hz resolves 5Hz
// and a 20ms impact would fall clean between two samples. That would not make
// the guard slower, it would make it blind while still appearing armed.
#define IMU_RATE_HZ  50

static uint16_t s_rate_hz   = IMU_RATE_HZ;
static uint32_t s_period_us = 1000000 / IMU_RATE_HZ;
static uint32_t s_skips     = 0;

static ICM_20948_I2C s_icm;
static bool     s_ok = false;
static uint8_t  s_addr = 0;
static float    s_last_mag = 1.0f;
static float    s_ax = 0.0f, s_ay = 0.0f, s_az = 1.0f;
static uint32_t s_next_us = 0;
static uint32_t s_last_good_us = 0;  // last successful sample
static bool     s_calibrated = false;
static float    s_baseline = 1.0f;
static uint32_t s_cal_start = 0;
static double   s_cal_sum = 0;
static uint32_t s_cal_n = 0;

bool imu_present()      { return s_ok; }
float imu_last_magnitude() { return s_last_mag; }
uint8_t imu_address()   { return s_addr; }
uint16_t imu_rate()     { return s_rate_hz; }
uint32_t imu_skips()    { return s_skips; }

void imu_last_axes(float* ax, float* ay, float* az) {
    if (ax) *ax = s_ax;
    if (ay) *ay = s_ay;
    if (az) *az = s_az;
}

// Anti-alias filter for a given sample rate: the widest of the ICM's accel
// bandwidths that still sits under Nyquist.
static uint8_t dlpf_for_rate(uint16_t hz) {
    if (hz >= 300) return acc_d111bw4_n136bw;   // 3dB 111.4 Hz
    if (hz >= 150) return acc_d50bw4_n68bw8;    // 3dB  50.4 Hz
    if (hz >= 75)  return acc_d23bw9_n34bw4;    // 3dB  23.9 Hz
    return acc_d11bw5_n17bw;                    // 3dB  11.5 Hz
}

// SparkFun's startupDefault() leaves the accelerometer at +/-2g with its
// low-pass filter DISABLED — acc_d473bw_n499bw, i.e. ~500 Hz of noise
// bandwidth — and the ODR at 1125 Hz. Both defaults are wrong here:
//
//  - Sampling 500 Hz of bandwidth at 50 Hz ALIASES. Every spindle harmonic
//    above 25 Hz folds back into the band and is then indistinguishable from
//    real low-frequency machine motion. The guard has been reading that, and
//    any log taken without fixing it would record it as signal.
//  - +/-2g CLIPS below the 4.0g impact threshold the guard ships with, so that
//    threshold could never fire however hard the machine was hit. Not a tuning
//    problem — an unreachable number.
//
// +/-8g still resolves 0.00024 g, which is orders below any vibration this
// machine makes, so the range costs nothing that matters.
static void imu_apply_config() {
    if (!s_ok) return;
    // Called from imu_probe() and from imu_set_rate(), and the second of those
    // can come from a UI callback. The wait is bounded and the bus is almost
    // always free, but a contended one costs a visible LVGL hitch rather than a
    // freeze — which beats leaving the sensor on defaults that alias.
    if (!i2c_take(100)) {
        Serial.println("[imu] config skipped: I2C busy");
        return;
    }

    ICM_20948_fss_t fss;
    fss.a = gpm8;
    fss.g = dps250;
    s_icm.setFullScale(ICM_20948_Internal_Acc, fss);

    ICM_20948_dlpcfg_t cfg;
    cfg.a = dlpf_for_rate(s_rate_hz);
    cfg.g = gyr_d196bw6_n229bw8;
    s_icm.setDLPFcfg(ICM_20948_Internal_Acc, cfg);
    s_icm.enableDLPF(ICM_20948_Internal_Acc, true);

    i2c_give();
    Serial.printf("[imu] %u Hz, +/-8g, dlpf cfg %u\n",
                  (unsigned)s_rate_hz, (unsigned)cfg.a);
}

void imu_set_rate(uint16_t hz) {
    if (hz < 10)  hz = 10;
    if (hz > 500) hz = 500;
    if (hz == s_rate_hz) return;
    s_rate_hz   = hz;
    s_period_us = 1000000u / hz;
    imu_apply_config();
}

// Probe both address straps. `patient` is used for the one-shot call at boot,
// where waiting on the I2C mutex is free because nothing is drawing yet; the
// background retry passes false so it can never stall touch polling.
static bool imu_probe(bool patient) {
    const uint32_t wait_ms = patient ? 100 : 5;
    for (int ad0 = 1; ad0 >= 0; ad0--) {
        if (!i2c_take(wait_ms)) continue;
        s_icm.begin(Wire, ad0);
        bool ok = (s_icm.status == ICM_20948_Stat_Ok);
        i2c_give();
        // SparkFun's begin() against a missing or miswired device leaves the ng
        // I2C driver returning ESP_ERR_INVALID_STATE for good, which takes the
        // GT911 down with it — touch dies, safety bar included. Put the bus back
        // before returning so a fruitless probe costs nothing.
        if (!ok) {
            if (i2c_take(50)) { i2c_bus_recover(); i2c_give(); }
        }

        if (ok) {
            s_ok = true;
            s_last_good_us = micros();
            s_addr = ad0 ? 0x69 : 0x68;
            Serial.printf("[imu] ICM-20948 at 0x%02X\n", s_addr);
            // Restart the stillness calibration: a sensor that just appeared has
            // no baseline, and reusing the boot timestamp would mark it done.
            s_calibrated = false;
            s_cal_sum    = 0;
            s_cal_n      = 0;
            s_cal_start  = millis();
            imu_apply_config();
            return true;
        }
    }
    return false;
}

bool imu_begin() {
    if (imu_probe(true)) return true;
    Serial.println("[imu] not found");
    s_ok = false;
    return false;
}

// Manual re-probe, driven by the SEARCH button on the Setup page. Patient like
// the boot path: the user has just asked for this and is watching, so a full
// mutex wait is right — unlike a background poll, it happens once.
bool imu_retry() {
    if (s_ok) return true;
    bool found = imu_probe(true);
    if (!found) Serial.println("[imu] search: nothing on the bus");
    return found;
}

void imu_poll() {
    // No background probing. Most machines never have a sensor fitted, and
    // spending an I2C transaction every few seconds to rediscover that costs
    // bus time the GT911 wants. Absent is a quiet, stable state; the Setup page
    // has a SEARCH button for when one actually gets plugged in.
    if (!s_ok) return;

    uint32_t now = micros();
    if ((int32_t)(now - s_next_us) < 0) return;
    s_next_us = now + s_period_us;

    // The bus is shared with touch; never block waiting for it, just skip this
    // sample. Missing one sample at 200Hz is invisible, a stalled UI is not.
    if (!i2c_take(2)) { s_skips++; return; }
    bool have = s_icm.dataReady();
    if (have) s_icm.getAGMT();
    i2c_give();
    if (have) {
        s_last_good_us = now;
    } else if ((int32_t)(now - s_last_good_us) > 2000000) {
        // Presence expires. A hot-plug glitch or cable dropout leaves the
        // device silent while s_ok swears it is fitted — the reading freezes
        // at its last value and Setup's SEARCH refuses to re-probe (seen on
        // the bench, 2026-09-06). Two silent seconds covers both failure
        // shapes: a NAK-ing device and one that browned out and rebooted
        // into sleep. Absent is honest; SEARCH re-probes and recalibrates.
        s_ok = false;
        Serial.println("[imu] went silent - marked absent (SEARCH re-probes)");
        return;
    }
    if (!have) return;

    // SparkFun reports milli-g.
    float ax = s_icm.accX() / 1000.0f;
    float ay = s_icm.accY() / 1000.0f;
    float az = s_icm.accZ() / 1000.0f;
    s_ax = ax; s_ay = ay; s_az = az;
    s_last_mag = sqrtf(ax * ax + ay * ay + az * az);

    // Before the calibration gate below, deliberately: the recorder wants the
    // still period at the head of a run, because that block IS the gravity
    // reference the sensor's orientation gets recovered from.
    imulog_sample(now, ax, ay, az);

    // Two seconds of stillness at boot establishes what "at rest" reads, so the
    // machine's own orientation and any sensor bias fall out of the deviation.
    if (!s_calibrated) {
        s_cal_sum += s_last_mag;
        s_cal_n++;
        if (millis() - s_cal_start >= 2000 && s_cal_n > 50) {
            s_baseline = (float)(s_cal_sum / s_cal_n);
            vib_reset(s_baseline);
            s_calibrated = true;
            Serial.printf("[imu] baseline %.3fg from %u samples\n",
                          (double)s_baseline, (unsigned)s_cal_n);
        }
        return;
    }

    vib_feed(s_last_mag, now);
}
