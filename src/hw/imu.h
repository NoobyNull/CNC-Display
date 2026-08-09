#pragma once
#include <stdint.h>

// ICM-20948 9DOF on the panel's existing I2C bus (SDA 19 / SCL 20), sharing it
// with the GT911 touch controller (0x5D) and the PCA9557 (0x18). The IMU sits at
// 0x68 or 0x69, so there is no address clash and no new pins are needed.
bool  imu_begin();
bool  imu_present();
void  imu_poll();            // samples at a fixed rate and feeds the analyser
float imu_last_magnitude();  // g
uint8_t imu_address();
