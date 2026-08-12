#include "display_hw.h"
#include "touch.h"
#include "io_expander.h"

#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>
#include <esp_lcd_panel_rgb.h>
#include <esp_lcd_panel_ops.h>

// ============================================================
// RGB panel -- Waveshare ESP32-S3-Touch-LCD-7B (1024x600 RGB565)
// ============================================================
//
// This drives the ESP-IDF esp_lcd_rgb_panel directly rather than going through
// LovyanGFX, for two reasons found the hard way on real hardware:
//
//  1. LovyanGFX's Bus_RGB::config_t stores every porch and pulse width in an
//     `int8_t`. Waveshare's real values -- hsync pulse 162, back porch 152 --
//     wrap to -94 and -104, the horizontal timing generator gets garbage, the
//     panel never syncs and the screen sits white with the backlight lit. Still
//     true on LovyanGFX master; there is no version to upgrade to.
//
//  2. LovyanGFX has no bounce buffer and cannot be given one. Its
//     Panel_RGB::initFrameBuffer() unconditionally puts the whole framebuffer in
//     PSRAM ("provisional implementation ... ToDo: split allocation") and its
//     use_psram setting is marked `unimplemented`. Streaming 1.2 MB per frame
//     straight out of PSRAM while LVGL writes into it produces the static-TV
//     noise this panel showed at both 30 MHz and 21 MHz.
//
// esp_lcd fixes both: uint32_t timings, and two DRAM bounce buffers that the
// DMA feeds from, which is exactly how Waveshare's own driver runs this panel.
static const uint32_t LCD_PCLK_HZ = 30000000;

// Waveshare's real timings, straight from their rgb_lcd_port.cpp. The 162-clock
// hsync pulse looks enormous next to a generic 1024x600 datasheet and is correct
// for the panel they solder on. Do not "fix" them from a generic ST7262 table.
static const uint32_t HSYNC_PULSE = 162, HSYNC_BACK = 152, HSYNC_FRONT = 48;
static const uint32_t VSYNC_PULSE = 45,  VSYNC_BACK = 13,  VSYNC_FRONT = 3;

// Two DRAM bounce buffers of this many pixels each. The DMA reads from these
// instead of from PSRAM, which is what keeps the scanout clean at 30 MHz.
// 1024*10 px * 2 B * 2 buffers = 40 KB of internal RAM.
static const size_t BOUNCE_PX = SCREEN_WIDTH * 10;

static esp_lcd_panel_handle_t s_panel = nullptr;

// ============================================================
// LVGL flush
// ============================================================
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf1;
static lv_color_t *buf2;

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
  // VCOM rail and backlight enable both live on the extender. The panel reset is
  // released too -- it is wired, not strapped, and comes up asserted.
  ioexp_write(EXIO_LCD_VDD_EN, true);
  ioexp_write(EXIO_LCD_RST, false);
  delay(10);
  ioexp_write(EXIO_LCD_RST, true);
  delay(50);
  ioexp_write(EXIO_DISP, true);

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

  cfg.data_width            = 16;
  cfg.bits_per_pixel        = 16;
  cfg.num_fbs               = 1;          // LVGL draws partial areas, not whole frames
  cfg.bounce_buffer_size_px = BOUNCE_PX;
  cfg.flags.fb_in_psram     = 1;

  cfg.hsync_gpio_num = 46;
  cfg.vsync_gpio_num = 3;
  cfg.de_gpio_num    = 5;
  cfg.pclk_gpio_num  = 7;
  cfg.disp_gpio_num  = -1;                // no DISP signal on this board

  // d0-d4 blue, d5-d10 green, d11-d15 red -- RGB565 bit order, matching
  // Waveshare's rgb_lcd_port.h exactly.
  const int data_pins[16] = { 14, 38, 18, 17, 10,      // B3..B7
                              39,  0, 45, 48, 47, 21,  // G2..G7
                               1,  2, 42, 41, 40 };    // R3..R7
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

// ============================================================
// lvgl_init() -- LVGL buffers + display/touch driver registration
// ============================================================
void lvgl_init() {
  lv_init();

  // Double-buffered PSRAM allocation (one screen width x 40 lines each)
  buf1 = (lv_color_t *)ps_malloc(SCREEN_WIDTH * 40 * sizeof(lv_color_t));
  buf2 = (lv_color_t *)ps_malloc(SCREEN_WIDTH * 40 * sizeof(lv_color_t));
  lv_disp_draw_buf_init(&draw_buf, buf1, buf2, SCREEN_WIDTH * 40);

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
  ioexp_backlight(level);
}

uint8_t get_backlight() {
  return current_brightness;
}
