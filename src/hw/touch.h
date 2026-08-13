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

// Diagnostics. fresh_count rises every time the GT911 reports a new scan, so a
// stalled counter means the controller has gone quiet even though the bus is
// answering; reset_count is how often the watchdog has had to revive it.
bool     touch_is_down();
uint32_t touch_fresh_count();
uint32_t touch_reset_count();

// Re-init the I2C master after a device wedges the bus. Caller must hold the
// mutex. See the implementation for why this is not paranoia.
void i2c_bus_recover();
