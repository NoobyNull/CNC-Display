#include "hw/imu.h"
#include "hw/touch.h"           // i2c_take / i2c_give
#include "model/vibration.h"
#include "model/settings.h"
#include <Arduino.h>
#include <Wire.h>
#include <ICM_20948.h>

// 200Hz, matching CNC-TW. Fast enough to catch an impact, slow enough that the
// I2C transaction does not starve touch polling on the shared bus.
#define IMU_RATE_HZ  200
#define IMU_PERIOD_US (1000000 / IMU_RATE_HZ)

static ICM_20948_I2C s_icm;
static bool     s_ok = false;
static uint8_t  s_addr = 0;
static float    s_last_mag = 1.0f;
static uint32_t s_next_us = 0;
static bool     s_calibrated = false;
static float    s_baseline = 1.0f;
static uint32_t s_cal_start = 0;
static double   s_cal_sum = 0;
static uint32_t s_cal_n = 0;

bool imu_present()      { return s_ok; }
float imu_last_magnitude() { return s_last_mag; }
uint8_t imu_address()   { return s_addr; }

bool imu_begin() {
    // AD0 high or low depending on the breakout; try both rather than make the
    // user care which one they bought.
    for (int ad0 = 1; ad0 >= 0; ad0--) {
        if (!i2c_take(100)) continue;
        s_icm.begin(Wire, ad0);
        bool ok = (s_icm.status == ICM_20948_Stat_Ok);
        i2c_give();
        if (ok) {
            s_ok = true;
            s_addr = ad0 ? 0x69 : 0x68;
            Serial.printf("[imu] ICM-20948 at 0x%02X\n", s_addr);
            s_cal_start = millis();
            return true;
        }
    }
    Serial.println("[imu] not found");
    s_ok = false;
    return false;
}

void imu_poll() {
    if (!s_ok) return;

    uint32_t now = micros();
    if ((int32_t)(now - s_next_us) < 0) return;
    s_next_us = now + IMU_PERIOD_US;

    // The bus is shared with touch; never block waiting for it, just skip this
    // sample. Missing one sample at 200Hz is invisible, a stalled UI is not.
    if (!i2c_take(2)) return;
    bool have = s_icm.dataReady();
    if (have) s_icm.getAGMT();
    i2c_give();
    if (!have) return;

    // SparkFun reports milli-g.
    float ax = s_icm.accX() / 1000.0f;
    float ay = s_icm.accY() / 1000.0f;
    float az = s_icm.accZ() / 1000.0f;
    s_last_mag = sqrtf(ax * ax + ay * ay + az * az);

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
