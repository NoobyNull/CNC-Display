#include "display_hw.h"
#include "touch.h"
#include "io_expander.h"

#include <Arduino.h>
#include <Wire.h>
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <lvgl.h>

// ============================================================
// LovyanGFX Display -- Waveshare ESP32-S3-Touch-LCD-7B (1024x600 RGB565)
// ============================================================

// Straight from Waveshare's rgb_lcd_port.h. The sync widths look wrong next to
// a typical 1024x600 datasheet — a 162-clock hsync pulse is enormous — but this
// is the combination they ship and validate against the panel they solder on.
// Do not "correct" them from a generic ST7262 table.
//
// PCLK is the one number worth touching. 30 MHz x 2 bytes is 60 MB/s read out
// of PSRAM, which is most of what octal PSRAM at 80 MHz can give while LVGL is
// also writing to it. If the panel tears, shears, or shows a rolling band,
// drop this to 21 MHz first — refresh falls from ~33 Hz to ~23 Hz and the
// bandwidth pressure goes with it.
static const uint32_t LCD_PCLK_HZ = 30000000;

class LGFX : public lgfx::LGFX_Device {
public:
  lgfx::Bus_RGB   _bus_instance;
  lgfx::Panel_RGB _panel_instance;

  LGFX(void) {
    {
      auto cfg = _panel_instance.config();
      cfg.memory_width  = SCREEN_WIDTH;
      cfg.memory_height = SCREEN_HEIGHT;
      cfg.panel_width   = SCREEN_WIDTH;
      cfg.panel_height  = SCREEN_HEIGHT;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      _panel_instance.config(cfg);
    }
    {
      auto cfg = _bus_instance.config();
      cfg.panel = &_panel_instance;
      // d0-d4 blue, d5-d10 green, d11-d15 red — RGB565 bit order.
      cfg.pin_d0  = GPIO_NUM_14; cfg.pin_d1  = GPIO_NUM_38;   // B3, B4
      cfg.pin_d2  = GPIO_NUM_18; cfg.pin_d3  = GPIO_NUM_17;   // B5, B6
      cfg.pin_d4  = GPIO_NUM_10; cfg.pin_d5  = GPIO_NUM_39;   // B7, G2
      cfg.pin_d6  = GPIO_NUM_0;  cfg.pin_d7  = GPIO_NUM_45;   // G3, G4
      cfg.pin_d8  = GPIO_NUM_48; cfg.pin_d9  = GPIO_NUM_47;   // G5, G6
      cfg.pin_d10 = GPIO_NUM_21; cfg.pin_d11 = GPIO_NUM_1;    // G7, R3
      cfg.pin_d12 = GPIO_NUM_2;  cfg.pin_d13 = GPIO_NUM_42;   // R4, R5
      cfg.pin_d14 = GPIO_NUM_41; cfg.pin_d15 = GPIO_NUM_40;   // R6, R7
      cfg.pin_henable = GPIO_NUM_5;
      cfg.pin_vsync   = GPIO_NUM_3;
      cfg.pin_hsync   = GPIO_NUM_46;
      cfg.pin_pclk    = GPIO_NUM_7;
      cfg.freq_write  = LCD_PCLK_HZ;
      cfg.hsync_polarity    = 0; cfg.hsync_front_porch = 48;
      cfg.hsync_pulse_width = 162; cfg.hsync_back_porch = 152;
      cfg.vsync_polarity    = 0; cfg.vsync_front_porch = 3;
      cfg.vsync_pulse_width = 45; cfg.vsync_back_porch = 13;
      cfg.pclk_active_neg = 1;
      cfg.de_idle_high    = 0;
      cfg.pclk_idle_high  = 0;
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }
    // No Light_PWM: the backlight is not on a GPIO at all, it is a PWM register
    // on the io-extender. set_backlight() below drives it.
    setPanel(&_panel_instance);
  }
};

static LGFX lcd;

// ============================================================
// LVGL Display Flush Callback
// ============================================================
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf1;
static lv_color_t *buf2;

static void disp_flush_cb(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
  lcd.startWrite();
  lcd.setAddrWindow(area->x1, area->y1, w, h);
  lcd.writePixels((lgfx::rgb565_t *)color_p, w * h);
  lcd.endWrite();
  lv_disp_flush_ready(disp);
}

// ============================================================
// display_init() -- panel rails + LCD init
// ============================================================
void display_init() {
  // VCOM rail and backlight enable both live on the extender. The panel reset
  // is released too — it is wired, not strapped, and comes up asserted.
  ioexp_write(EXIO_LCD_VDD_EN, true);
  ioexp_write(EXIO_LCD_RST, false);
  delay(10);
  ioexp_write(EXIO_LCD_RST, true);
  delay(50);
  ioexp_write(EXIO_DISP, true);

  lcd.begin();
  lcd.fillScreen(TFT_BLACK);
  delay(200);
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
