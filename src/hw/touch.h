#pragma once
#include <cstdint>
#include <lvgl.h>

void touch_init();       // Create I2C mutex
void gt911_discover();   // Discover GT911 address -- call after display_init()
void touch_poll();       // Poll GT911 with mutex protection -- call from loop() at 20Hz
void touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data);  // LVGL callback

// I2C mutex helpers -- used by any module needing I2C
bool i2c_take(uint32_t timeout_ms = 10);
void i2c_give();
