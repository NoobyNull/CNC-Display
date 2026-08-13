#pragma once
#include <cstdint>
#include <lvgl.h>

// TP_IRQ. The only touch signal on a real GPIO; RST is on the io-extender.
#define GT911_INT_PIN 4

// The panel's one I2C bus: GT911 touch (0x5D), io-extender (0x24), and an
// ICM-20948 (0x68/0x69) if one is fitted to the I2C terminal. Defined here
// rather than written literally at each Wire.begin() so the bus recovery below
// cannot drift from what main.cpp brought the bus up with.
#define GT911_SDA_PIN 8
#define GT911_SCL_PIN 9

void touch_init();       // Create I2C mutex
void gt911_reset();      // Pulse RST with INT held low -- call before discover
void gt911_discover();   // Discover GT911 address -- call after gt911_reset()
void touch_poll();       // Poll GT911 with mutex protection -- call from loop() at 20Hz
void touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data);  // LVGL callback

// I2C mutex helpers -- used by any module needing I2C
bool i2c_take(uint32_t timeout_ms = 10);
void i2c_give();

// Diagnostics. fresh_count rises every time the GT911 reports a new scan. A
// stalled counter is NOT a fault: this chip reports only while something is
// happening, so untouched it stops at whatever it last reached. See touch.cpp.
bool     touch_is_down();
uint32_t touch_fresh_count();
uint32_t touch_poll_count();   // polls attempted
uint32_t touch_lock_fails();   // polls that lost the I2C mutex
uint32_t touch_stat_errs();    // status reads the chip refused
uint8_t  touch_int_level();    // live INT pin level -- stuck high/low is diagnostic
uint8_t  touch_last_status();  // last 0x814E byte seen

// True once per completed four-corner hold. Read-and-clear, so the caller that
// takes it owns it. Multi-touch is invisible to LVGL, so this is detected in
// the driver and polled from the shell.
bool touch_take_corner_gesture();

// Print the GT911's stored configuration. Read-only: nothing is ever written
// back, see the implementation for why.
void gt911_dump_config();

// Re-init the I2C master after a device wedges the bus. Caller must hold the
// mutex. See the implementation for why this is not paranoia.
void i2c_bus_recover();
