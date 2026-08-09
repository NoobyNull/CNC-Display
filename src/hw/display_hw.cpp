#include "display_hw.h"
#include "touch.h"

#include <Arduino.h>
#include <Wire.h>
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <lvgl.h>
#include <PCA9557.h>

// ============================================================
// LovyanGFX Display -- CrowPanel 7.0" (800x480 RGB565)
// ============================================================
class LGFX : public lgfx::LGFX_Device {
public:
  lgfx::Bus_RGB   _bus_instance;
  lgfx::Panel_RGB _panel_instance;
  lgfx::Light_PWM _light_instance;

  LGFX(void) {
    {
      auto cfg = _panel_instance.config();
      cfg.memory_width  = 800;
      cfg.memory_height = 480;
      cfg.panel_width   = 800;
      cfg.panel_height  = 480;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      _panel_instance.config(cfg);
    }
    {
      auto cfg = _bus_instance.config();
      cfg.panel = &_panel_instance;
      cfg.pin_d0  = GPIO_NUM_15; cfg.pin_d1  = GPIO_NUM_7;
      cfg.pin_d2  = GPIO_NUM_6;  cfg.pin_d3  = GPIO_NUM_5;
      cfg.pin_d4  = GPIO_NUM_4;  cfg.pin_d5  = GPIO_NUM_9;
      cfg.pin_d6  = GPIO_NUM_46; cfg.pin_d7  = GPIO_NUM_3;
      cfg.pin_d8  = GPIO_NUM_8;  cfg.pin_d9  = GPIO_NUM_16;
      cfg.pin_d10 = GPIO_NUM_1;  cfg.pin_d11 = GPIO_NUM_14;
      cfg.pin_d12 = GPIO_NUM_21; cfg.pin_d13 = GPIO_NUM_47;
      cfg.pin_d14 = GPIO_NUM_48; cfg.pin_d15 = GPIO_NUM_45;
      cfg.pin_henable = GPIO_NUM_41;
      cfg.pin_vsync   = GPIO_NUM_40;
      cfg.pin_hsync   = GPIO_NUM_39;
      cfg.pin_pclk    = GPIO_NUM_0;
      cfg.freq_write  = 12000000;
      cfg.hsync_polarity    = 0; cfg.hsync_front_porch = 40;
      cfg.hsync_pulse_width = 48; cfg.hsync_back_porch = 40;
      cfg.vsync_polarity    = 0; cfg.vsync_front_porch = 1;
      cfg.vsync_pulse_width = 31; cfg.vsync_back_porch = 13;
      cfg.pclk_active_neg = 1;
      cfg.de_idle_high    = 0;
      cfg.pclk_idle_high  = 0;
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }
    {
      auto cfg = _light_instance.config();
      cfg.pin_bl = GPIO_NUM_2;
      _light_instance.config(cfg);
      _panel_instance.light(&_light_instance);
    }
    setPanel(&_panel_instance);
  }
};

static LGFX lcd;
static PCA9557 ioExpander;

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
// display_init() -- PCA9557 touch reset + LCD init
// ============================================================
void display_init() {
  // GPIO 38 active-low for CrowPanel power rail
  pinMode(38, OUTPUT);
  digitalWrite(38, LOW);

  // PCA9557 touch reset sequence:
  // IO0 controls GT911 reset, IO1 is GT911 INT
  ioExpander.reset();
  ioExpander.setMode(IO_OUTPUT);
  ioExpander.setState(IO0, IO_LOW);
  ioExpander.setState(IO1, IO_LOW);
  delay(20);
  ioExpander.setState(IO0, IO_HIGH);
  delay(100);
  ioExpander.setMode(IO1, IO_INPUT);
  Serial.println("PCA9557 touch reset done");

  // Initialize LovyanGFX RGB panel
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

  // Double-buffered PSRAM allocation (800 x 40 lines each)
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
  lcd.setBrightness(level);
}

uint8_t get_backlight() {
  return current_brightness;
}
