#pragma once
#include <cstdint>

#define SCREEN_WIDTH  1024
#define SCREEN_HEIGHT 600

void display_init();   // Panel rails via the io-extender + LovyanGFX RGB init
void lvgl_init();      // Init LVGL buffers, register display/touch drivers
void lvgl_tick();      // Call lv_timer_handler() -- call from loop()

// Resynchronise the RGB DMA with the panel. arduino-esp32 ships its prebuilt
// libs with CONFIG_LCD_RGB_ISR_IRAM_SAFE unset, so the panel ISR sits in flash
// and cannot run while the flash cache is disabled -- which WiFi init, scans and
// NVS writes all do. The DMA loses its place and the image shifts or tears.
// CONFIG_LCD_RGB_RESTART_IN_VSYNC is set, so this restarts cleanly on a frame
// boundary. Call it after any operation that blacks out the cache.
void display_resync();

void set_backlight(uint8_t level);   // 0=off, 255=max. Drives the io-extender PWM.
uint8_t get_backlight();
