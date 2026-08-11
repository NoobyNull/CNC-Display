#include "hw/io_expander.h"
#include "hw/touch.h"        // i2c_take / i2c_give — the GT911 shares this bus
#include <Arduino.h>
#include <Wire.h>

#define IOEXP_ADDR      0x24
#define REG_MODE        0x02   // 1 bit per pin, 1 = output
#define REG_OUTPUT      0x03
#define REG_PWM         0x05

// The output register is write-only, so the byte is shadowed here. Its initial
// value matters more than it looks: the chip is written as a whole port, so the
// first ioexp_write() of any pin publishes every other bit at once.
//
// Waveshare's driver seeds this with 0xFF, which drives EXIO_CAN_SEL high and
// hands the USB-C over to the CAN transceiver. Their demos flash over the
// *other* Type-C so they never notice. We flash over native USB, so a stray 1
// in bit 5 would disconnect the port we are talking to and the one we recover
// through. It starts low and stays low.
//
// SD_CS starts high because the card is driven in SDMMC 1-bit mode, where that
// line is DAT3 and must idle high or the card falls back to SPI mode.
static uint8_t s_out = (1 << EXIO_TP_RST) | (1 << EXIO_DISP) | (1 << EXIO_LCD_RST) |
                       (1 << EXIO_SD_CS)  | (1 << EXIO_LCD_VDD_EN);

static bool s_ok = false;

static bool write_reg(uint8_t reg, uint8_t val) {
    if (!i2c_take(50)) return false;
    Wire.beginTransmission(IOEXP_ADDR);
    Wire.write(reg);
    Wire.write(val);
    bool ok = (Wire.endTransmission() == 0);
    i2c_give();
    return ok;
}

bool ioexp_init() {
    s_ok = write_reg(REG_MODE, 0xFF);      // every line an output
    s_ok = write_reg(REG_OUTPUT, s_out) && s_ok;
    Serial.printf("[ioexp] %s at 0x%02X\n", s_ok ? "ready" : "NOT FOUND", IOEXP_ADDR);
    return s_ok;
}

void ioexp_write(uint8_t pin, bool high) {
    if (pin > 7) return;
    if (high) s_out |=  (uint8_t)(1 << pin);
    else      s_out &= (uint8_t)~(1 << pin);
    write_reg(REG_OUTPUT, s_out);
}

// The PWM register is inverted: 0 is full brightness and larger values dim the
// boost converter. Waveshare caps it at 247/255 because the panel latches off
// past that and the only way back is a power cycle.
void ioexp_backlight(uint8_t level) {
    uint16_t duty = 255 - level;
    if (duty > 247) duty = 247;
    write_reg(REG_PWM, (uint8_t)duty);
}
