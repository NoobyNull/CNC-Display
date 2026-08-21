#pragma once
#include <stdint.h>

// The Advance 7.0's housekeeping MCU: an STC8H1K28 on the I2C bus at 0x30
// (V1.2+ boards; V1.0 used a PCA9557 instead). Backlight power and brightness,
// the panel's touch reset line (P1_7/TP_RST) and the buzzer all hang off its
// port pins, not the ESP32's — so "drive the backlight" and "reset the GT911"
// are I2C commands here, the way they were extender writes on the Waveshare 7B.
//
// The known command byte space, from Elecrow's factory code:
//   0..245  backlight brightness, inverted: 0 = full, 245 = off
//   250     activate the touch controller (pulses TP_RST)
// Everything else is undocumented; nothing here sends it.
bool stc8_init();                    // false if 0x30 never answers
bool stc8_cmd(uint8_t cmd);          // raw command, mutex-protected
void stc8_backlight(uint8_t level);  // 0 = off, 255 = max (scaled+inverted to 245..0)
void stc8_display(bool on);          // blanking: off sends 245, on restores the last level
