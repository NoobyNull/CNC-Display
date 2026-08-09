#include "touch.h"

#include <Arduino.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Adapted from Elcrow-Display-hotkeys/display/touch.cpp with its gesture layer
// removed. That layer held LVGL's press report back by 120ms to tell taps from
// swipes, and suppressed input entirely once a finger moved 30px. Both are wrong
// here: a jog key must register the instant it is touched, and a swipe starting
// on a jog key would be a page change with the spindle running. The GT911 read
// path itself is unchanged.

// ============================================================
// I2C mutex — protects all Wire operations (GT911 shares the bus
// with the PCA9557 on SDA=19 / SCL=20)
// ============================================================
static SemaphoreHandle_t i2c_mutex = NULL;

bool i2c_take(uint32_t timeout_ms) {
    if (!i2c_mutex) return false;
    return xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void i2c_give() {
    if (i2c_mutex) xSemaphoreGive(i2c_mutex);
}

// ============================================================
// GT911 touch — direct I2C via Wire
// ============================================================
static uint8_t gt911_addr = 0;

static volatile bool     touch_down = false;
static volatile uint16_t touch_x = 0;
static volatile uint16_t touch_y = 0;

static uint32_t touch_err_timer = 0;   // rate-limit error logging

void touch_init() {
    i2c_mutex = xSemaphoreCreateMutex();
    configASSERT(i2c_mutex != NULL);
    Serial.println("I2C mutex created");
}

// Probe 0x5D and 0x14. Call AFTER display_init(), which resets the GT911
// through the PCA9557.
void gt911_discover() {
    uint8_t addrs[] = { 0x5D, 0x14 };
    for (int attempt = 0; attempt < 10; attempt++) {
        for (int i = 0; i < 2; i++) {
            if (!i2c_take(50)) continue;
            Wire.beginTransmission(addrs[i]);
            uint8_t err = Wire.endTransmission();
            i2c_give();

            if (err == 0) {
                gt911_addr = addrs[i];
                Serial.printf("GT911 found at 0x%02X (attempt %d)\n", gt911_addr, attempt);
                return;
            }
        }
        delay(100);
    }
    Serial.println("GT911 not found!");
}

void touch_poll() {
    if (gt911_addr == 0) return;
    if (!i2c_take(10)) return;             // bus busy, skip this cycle

    // Status register 0x814E
    Wire.beginTransmission(gt911_addr);
    Wire.write(0x81);
    Wire.write(0x4E);
    int err = Wire.endTransmission();
    if (err != 0) {
        i2c_give();
        if (millis() - touch_err_timer > 2000) {
            touch_err_timer = millis();
            Serial.printf("GT911 i2c err: %d\n", err);
        }
        touch_down = false;
        return;
    }

    Wire.requestFrom(gt911_addr, (uint8_t)1);
    if (!Wire.available()) {
        i2c_give();
        touch_down = false;
        return;
    }

    uint8_t status  = Wire.read();
    uint8_t touches = status & 0x0F;

    if ((status & 0x80) && touches > 0) {
        // Touch point 0 at 0x8150
        Wire.beginTransmission(gt911_addr);
        Wire.write(0x81);
        Wire.write(0x50);
        Wire.endTransmission();
        Wire.requestFrom(gt911_addr, (uint8_t)4);
        if (Wire.available() >= 4) {
            touch_x = Wire.read() | (Wire.read() << 8);
            touch_y = Wire.read() | (Wire.read() << 8);
            touch_down = true;
        }
    } else {
        touch_down = false;
    }

    // Clear the status register
    Wire.beginTransmission(gt911_addr);
    Wire.write(0x81);
    Wire.write(0x4E);
    Wire.write(0x00);
    Wire.endTransmission();

    i2c_give();
}

// LVGL input driver callback. Reports cached state with no I2C and no delay:
// press is reported on the first poll that sees a finger.
void touch_read_cb(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    (void)drv;
    data->point.x = touch_x;
    data->point.y = touch_y;
    data->state   = touch_down ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
}
