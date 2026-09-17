#pragma once
#include <stdint.h>

// ICM-20948 9DOF on the panel's I2C bus (SDA 8 / SCL 9), sharing it with the
// GT911 touch controller (0x5D) and the io-extender (0x24). The IMU sits at
// 0x68 or 0x69, so there is no address clash and no new pins are needed.
//
// This board has no onboard IMU — unlike the CrowPanel it replaced — so unless
// one is plugged into the I2C terminal, imu_begin() reports absent and the
// vibration guard stays inert. That is the intended failure mode, not a bug.
// Probed once at boot and then left alone — there is no background retry, since
// most machines never fit a sensor and rediscovering that costs bus time the
// GT911 wants. imu_retry() is the manual search, wired to the Setup page.
bool  imu_begin();
bool  imu_retry();           // re-probe on demand; restarts calibration if found
bool  imu_present();
void  imu_poll();            // samples at imu_rate() and feeds the analyser
float imu_last_magnitude();  // g
uint8_t imu_address();

// The three axes behind that magnitude, in g, in the SENSOR frame. The guard
// only ever wanted |a|, but a scalar cannot be rotated into the machine frame
// afterwards and cannot separate an X reversal from Z chatter — so the axes are
// kept and model/imu_log.cpp records them.
void imu_last_axes(float* ax, float* ay, float* az);

// Sample rate. 50 Hz is the guard's own rate and the default: enough bandwidth
// to see an impact, little enough of the shared I2C bus that touch polling
// still gets a look in. Raise it for a recording and put it back afterwards —
// at 200 Hz the sensor holds the bus for several percent of wall clock, which
// is visible in the UI. Changing the rate re-picks the anti-alias filter, so
// nothing else has to remember to.
void     imu_set_rate(uint16_t hz);   // clamped to 10..500
uint16_t imu_rate();

// Samples skipped because the GT911 held the I2C mutex. Zero is the expected
// reading at 50 Hz; a climbing count at a higher rate is the honest signal that
// the rate is too high for this bus, and it belongs next to any log taken at
// that rate.
uint32_t imu_skips();
