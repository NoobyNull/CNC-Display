#pragma once
#include <cstdint>

#define SCREEN_WIDTH  1024
#define SCREEN_HEIGHT 600

void display_init();   // Panel rails via the io-extender + LovyanGFX RGB init
void lvgl_init();      // Init LVGL buffers, register display/touch drivers
void lvgl_tick();      // Call lv_timer_handler() -- call from loop()

void set_backlight(uint8_t level);   // 0=off, 255=max. Drives the io-extender PWM.
uint8_t get_backlight();
