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
void  imu_poll();            // samples at a fixed rate and feeds the analyser
float imu_last_magnitude();  // g
uint8_t imu_address();
