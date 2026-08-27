#include "hw/stc8.h"
#include "hw/touch.h"        // i2c_take / i2c_give — the GT911 shares this bus
#include <Arduino.h>
#include <Wire.h>

#define STC8_ADDR    0x30
#define STC8_BL_OFF  245     // top of the brightness range; also "backlight off"

// What the backlight was last asked for, so stc8_display(true) can restore it.
static uint8_t s_level = 200;

bool stc8_cmd(uint8_t cmd) {
    if (!i2c_take(50)) return false;
    Wire.beginTransmission(STC8_ADDR);
    Wire.write(cmd);
    bool ok = (Wire.endTransmission() == 0);
    i2c_give();
    return ok;
}

// The STC8 runs its own firmware on its own clock and is not guaranteed to be
// listening the instant the ESP32 comes out of reset. Elecrow's factory code
// loops until it acks; so does this, bounded. If it never answers, the panel
// still lights (the STC8 defaults the backlight on by itself) — brightness and
// blanking just won't work, which the log line makes visible.
// Latched from init so the HW page can report it. The probe loop below is a
// boot-time affair — re-running it to answer "is it there?" would cost up to
// two seconds of blocked UI.
static bool s_present = false;

bool stc8_present() { return s_present; }

bool stc8_init() {
    for (int attempt = 0; attempt < 20; attempt++) {
        if (!i2c_take(50)) { delay(10); continue; }
        Wire.beginTransmission(STC8_ADDR);
        bool ok = (Wire.endTransmission() == 0);
        i2c_give();
        if (ok) {
            Serial.printf("[stc8] ready at 0x%02X (attempt %d)\n", STC8_ADDR, attempt);
            s_present = true;
            return true;
        }
        delay(100);
    }
    Serial.println("[stc8] NOT FOUND — backlight control and touch reset unavailable");
    return false;
}

void stc8_backlight(uint8_t level) {
    s_level = level;
    // 0..255 (0 = off) -> 245..0 (245 = off), the chip's inverted scale.
    stc8_cmd((uint8_t)(((255 - level) * STC8_BL_OFF) / 255));
}

void stc8_display(bool on) {
    if (on) stc8_backlight(s_level);
    else    stc8_cmd(STC8_BL_OFF);
}
