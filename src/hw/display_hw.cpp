#include "display_hw.h"
#include "touch.h"
#include "stc8.h"
#include "model/screensaver.h"
#include "model/settings.h"

#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>
#include <esp_lcd_panel_rgb.h>
#include <esp_lcd_panel_ops.h>

// ============================================================
// RGB panel -- Elecrow CrowPanel Advance 7.0 V1.4 (800x480 RGB565)
// ============================================================
//
// This stays on the ESP-IDF esp_lcd_rgb_panel driver the Waveshare 7B port
// introduced (LovyanGFX has no bounce buffer, and streaming the framebuffer
// straight out of PSRAM while LVGL writes into it produces static). Elecrow's
// own V1.3+ factory example drives this panel through esp_lcd too — every
// timing and flag below is copied from it verbatim.
static const uint32_t LCD_PCLK_HZ = 18000000;

static const uint32_t HSYNC_PULSE = 4, HSYNC_BACK = 8, HSYNC_FRONT = 8;
static const uint32_t VSYNC_PULSE = 4, VSYNC_BACK = 8, VSYNC_FRONT = 8;

// Two DRAM bounce buffers of this many pixels each. The DMA reads from these
// instead of from PSRAM, which is what keeps the scanout clean. 20 lines is
// the factory value: 800*20 px * 2 B * 2 buffers = 64 KB of internal RAM.
static const size_t BOUNCE_PX = SCREEN_WIDTH * 20;

static esp_lcd_panel_handle_t s_panel = nullptr;

// ============================================================
// LVGL flush
// ============================================================
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf1;

static void disp_flush_cb(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  // esp_lcd takes an exclusive end coordinate; LVGL's area is inclusive.
  esp_lcd_panel_draw_bitmap(s_panel, area->x1, area->y1,
                            area->x2 + 1, area->y2 + 1, color_p);
  lv_disp_flush_ready(disp);
}

// ============================================================
// display_init() -- panel rails + RGB panel
// ============================================================
void display_init() {
  // No rails to sequence here: the STC8 powers the panel and backlight on its
  // own at boot, and stc8_init() has already confirmed it is alive.

  // Field-by-field rather than a designated initializer: sram_trans_align and
  // psram_trans_align are deprecated and union'd with dma_burst_size in IDF 5.x,
  // so positional init breaks across versions.
  esp_lcd_rgb_panel_config_t cfg = {};
  cfg.clk_src = LCD_CLK_SRC_DEFAULT;

  cfg.timings.pclk_hz           = LCD_PCLK_HZ;
  cfg.timings.h_res             = SCREEN_WIDTH;
  cfg.timings.v_res             = SCREEN_HEIGHT;
  cfg.timings.hsync_pulse_width = HSYNC_PULSE;
  cfg.timings.hsync_back_porch  = HSYNC_BACK;
  cfg.timings.hsync_front_porch = HSYNC_FRONT;
  cfg.timings.vsync_pulse_width = VSYNC_PULSE;
  cfg.timings.vsync_back_porch  = VSYNC_BACK;
  cfg.timings.vsync_front_porch = VSYNC_FRONT;
  cfg.timings.flags.pclk_active_neg = 1;
  cfg.timings.flags.pclk_idle_high  = 1;  // factory flag; keep with the timings

  cfg.data_width            = 16;
  cfg.bits_per_pixel        = 16;
  cfg.num_fbs               = 1;          // LVGL draws partial areas, not whole frames
  cfg.bounce_buffer_size_px = BOUNCE_PX;
  cfg.dma_burst_size        = 64;
  cfg.flags.fb_in_psram     = 1;
  cfg.flags.bb_invalidate_cache = 1;

  cfg.hsync_gpio_num = 40;
  cfg.vsync_gpio_num = 41;
  cfg.de_gpio_num    = 42;
  cfg.pclk_gpio_num  = 39;
  cfg.disp_gpio_num  = -1;                // no DISP signal on this board

  // d0-d4 blue, d5-d10 green, d11-d15 red -- RGB565 bit order, matching
  // Elecrow's factory example exactly.
  const int data_pins[16] = { 21, 47, 48, 45, 38,      // B0..B4
                               9, 10, 11, 12, 13, 14,  // G0..G5
                               7, 17, 18,  3, 46 };    // R0..R4
  for (int i = 0; i < 16; i++) cfg.data_gpio_nums[i] = data_pins[i];

  esp_err_t err = esp_lcd_new_rgb_panel(&cfg, &s_panel);
  if (err != ESP_OK) {
      Serial.printf("[lcd] esp_lcd_new_rgb_panel FAILED: %s\n", esp_err_to_name(err));
      return;
  }
  err = esp_lcd_panel_reset(s_panel);
  if (err != ESP_OK) Serial.printf("[lcd] reset: %s\n", esp_err_to_name(err));
  err = esp_lcd_panel_init(s_panel);
  if (err != ESP_OK) {
      Serial.printf("[lcd] esp_lcd_panel_init FAILED: %s\n", esp_err_to_name(err));
      return;
  }
  Serial.printf("[lcd] rgb panel up, %dx%d @ %lu Hz, bounce %u px\n",
                SCREEN_WIDTH, SCREEN_HEIGHT,
                (unsigned long)LCD_PCLK_HZ, (unsigned)BOUNCE_PX);

  Serial.println("Display initialized");
}

// The network update needs the internal RAM back: mbedTLS on this build can
// only allocate internally (CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC) and the
// handshake wants tens of KB, which the 40 KB draw buffer above was eating --
// GET came back HTTP -1. Every update that reaches the radio ends in a reboot,
// so this is one-way. Call between renders (from an event callback is fine).
void display_release_internal_ram() {
  if (!buf1 || !esp_ptr_internal(buf1)) return;
  const size_t px = draw_buf.size;
  lv_color_t* ps = (lv_color_t *)ps_malloc(px * sizeof(lv_color_t));
  if (!ps) return;
  lv_disp_draw_buf_init(&draw_buf, ps, nullptr, px);
  heap_caps_free(buf1);
  buf1 = ps;
}

// ============================================================
// lvgl_init() -- LVGL buffers + display/touch driver registration
// ============================================================
void lvgl_init() {
  lv_init();

  // One draw buffer in internal SRAM. LVGL renders several times faster there
  // than in PSRAM, and it leaves PSRAM bandwidth to the bounce-buffer refills
  // that feed the scanout. One is enough: the flush below is a synchronous
  // memcpy into the framebuffer, so a second buffer would never be drawn into
  // while the first is in flight. Same height as the bounce buffers.
  const size_t px = SCREEN_WIDTH * 20;
  buf1 = (lv_color_t *)heap_caps_malloc(px * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!buf1) {
    // Internal RAM short: PSRAM still works, just slower.
    Serial.println("[lcd] no internal RAM for the draw buffer, using PSRAM");
    buf1 = (lv_color_t *)ps_malloc(px * sizeof(lv_color_t));
  }
  lv_disp_draw_buf_init(&draw_buf, buf1, nullptr, px);

  // Display driver
  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res  = SCREEN_WIDTH;
  disp_drv.ver_res  = SCREEN_HEIGHT;
  disp_drv.flush_cb = disp_flush_cb;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  // Touch input driver
  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type    = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = touch_read_cb;
  lv_indev_drv_register(&indev_drv);
}

void display_resync() {
  if (!s_panel) return;
  esp_lcd_rgb_panel_restart(s_panel);
}

// ============================================================
// lvgl_tick() -- call from loop()
// ============================================================
void lvgl_tick() {
  lv_timer_handler();
}

// ============================================================
// Brightness control wrappers
// ============================================================
static uint8_t current_brightness = 200;

void set_backlight(uint8_t level) {
  current_brightness = level;
  stc8_backlight(level);
}

uint8_t get_backlight() {
  return current_brightness;
}

// ============================================================
// Idle blanking
// ============================================================
// The decision lives in model/screensaver.cpp so it can be tested on the host;
// all this does is follow it with the STC8 backlight command.
// What the backlight is actually doing, tracked here rather than inferred from
// the screensaver's own state.
//
// Inferring it was a bug that left the panel permanently dark: the touch driver
// clears the blanked flag itself when it swallows the wake tap, so by the time
// this ran there was no transition left to see, the backlight was never
// re-enabled, and nothing afterwards could recover it. Comparing "what the
// screensaver wants" against "what the hardware is doing" cannot miss an edge,
// whoever moved it.
static bool s_backlight_off = false;

void screen_idle_tick(bool machine_busy) {
  screensaver_update(lv_disp_get_inactive_time(NULL),
                     settings_screen_off_min(), machine_busy);

  const bool want_off = screensaver_is_off();
  if (want_off == s_backlight_off) return;

  s_backlight_off = want_off;
  stc8_display(!want_off);
  if (!want_off) {
    // Restart the idle clock so a wake does not blank again on the next tick.
    lv_disp_trig_activity(NULL);
  }
}
