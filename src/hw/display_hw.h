#pragma once
#include <cstdint>

#define SCREEN_WIDTH  800
#define SCREEN_HEIGHT 480

void display_init();   // Init LovyanGFX RGB panel + PCA9557 touch reset + backlight
void lvgl_init();      // Init LVGL buffers, register display/touch drivers
void lvgl_tick();      // Call lv_timer_handler() -- call from loop()

void set_backlight(uint8_t level);   // 0=off, 255=max. Wraps lcd.setBrightness().
uint8_t get_backlight();
