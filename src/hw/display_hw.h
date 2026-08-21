#pragma once
#include <cstdint>

#define SCREEN_WIDTH  800
#define SCREEN_HEIGHT 480

void display_init();   // esp_lcd RGB panel init (rails/backlight are the STC8's job)
void lvgl_init();      // Init LVGL buffers, register display/touch drivers
void lvgl_tick();      // Call lv_timer_handler() -- call from loop()

// Resynchronise the RGB DMA with the panel. arduino-esp32 ships its prebuilt
// libs with CONFIG_LCD_RGB_ISR_IRAM_SAFE unset, so the panel ISR sits in flash
// and cannot run while the flash cache is disabled -- which WiFi init, scans and
// NVS writes all do. The DMA loses its place and the image shifts or tears.
// CONFIG_LCD_RGB_RESTART_IN_VSYNC is set, so this restarts cleanly on a frame
// boundary. Call it after any operation that blacks out the cache.
void display_resync();

void set_backlight(uint8_t level);   // 0=off, 255=max. I2C command to the STC8.
uint8_t get_backlight();

// Idle blanking. Call once per UI tick with whatever the machine is doing;
// screen_idle_tick() reads the timeout from settings and drives the backlight
// through the STC8 (245 = off, fully reversible — no latch-off on this board).
void screen_idle_tick(bool machine_busy);
