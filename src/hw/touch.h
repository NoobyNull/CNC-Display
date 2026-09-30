#pragma once
#include <cstdint>
#include <lvgl.h>

// TP_IRQ (net IO1_TP_INT on the V1.4 schematic). The only touch signal on a
// real GPIO; RST is a port pin on the STC8 helper MCU, reached by I2C command.
#define GT911_INT_PIN 1

// The panel's one I2C bus: GT911 touch (0x5D), STC8 housekeeping MCU (0x30),
// BM8563 RTC (0x51), and an ICM-20948 (0x68/0x69) if one is fitted to the
// I2C-OUT connector. Defined here rather than written literally at each
// Wire.begin() so the bus recovery below cannot drift from what main.cpp
// brought the bus up with.
#define GT911_SDA_PIN 15
#define GT911_SCL_PIN 16

// Bus clock; override with -DI2C_HZ=... (Elecrow's factory code uses 100 kHz).
#ifndef I2C_HZ
#define I2C_HZ 400000
#endif

void touch_init();       // Create I2C mutex
void gt911_reset();      // Pulse RST (via the STC8) with INT held low -- call before discover
void gt911_discover();   // Find the GT911, resetting it between misses
void touch_poll();       // Poll GT911 with mutex protection -- call from loop() at 20Hz
void touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data);  // LVGL callback

// I2C mutex helpers -- used by any module needing I2C
bool i2c_take(uint32_t timeout_ms = 10);
void i2c_give();

// Diagnostics. fresh_count rises every time the GT911 reports a new scan. A
// stalled counter is NOT a fault: this chip reports only while something is
// happening, so untouched it stops at whatever it last reached. See touch.cpp.
bool     touch_is_down();

// Fingers reported by the last fresh scan (GT911 tracks up to 5). Used for the
// two-finger gesture that opens config mode; only point 0 is ever positioned.
uint8_t  touch_points();
uint32_t touch_fresh_count();
uint32_t touch_poll_count();   // polls attempted
uint32_t touch_lock_fails();   // polls that lost the I2C mutex
uint32_t touch_stat_errs();    // status reads the chip refused
uint8_t  touch_int_level();    // live INT pin level -- stuck high/low is diagnostic
uint8_t  touch_last_status();  // last 0x814E byte seen

// Print the GT911's stored configuration. Read-only: nothing is ever written
// back, see the implementation for why.
void gt911_dump_config();

// Re-init the I2C master after a device wedges the bus. Caller must hold the
// mutex. See the implementation for why this is not paranoia.
void i2c_bus_recover();
