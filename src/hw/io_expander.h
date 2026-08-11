#pragma once
#include <stdint.h>

// The 7B's "IO EXTENSION" is a CH32V003 sitting on the I2C bus at 0x24, not a
// plain expander chip: it has a register map, an 8-bit output port, one PWM
// channel (the backlight) and an ADC (battery volts). Waveshare's driver is
// reimplemented here rather than vendored because theirs is ESP-IDF i2c_master
// and it gets the power-on state wrong for this board — see EXIO_CAN_SEL below.
//
// Everything the screen needs to light up hangs off this chip, so ioexp_init()
// must run before display_init() and before the GT911 is touched.

#define EXIO_TP_RST      1   // GT911 reset, active low
#define EXIO_DISP        2   // backlight enable
#define EXIO_LCD_RST     3   // panel reset, active low
#define EXIO_SD_CS       4   // TF card DAT3/CS — held HIGH for SDMMC 1-bit mode
#define EXIO_CAN_SEL     5   // 0 = USB-C is native USB, 1 = it becomes the CAN PHY
#define EXIO_LCD_VDD_EN  6   // panel VCOM rail

bool ioexp_init();                          // false if the chip does not answer
void ioexp_write(uint8_t pin, bool high);   // pin 0-7
void ioexp_backlight(uint8_t level);        // 0 = off-ish, 255 = full
