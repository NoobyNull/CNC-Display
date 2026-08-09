#pragma once
#include <stdint.h>

// FluidNC link: UART2 @ 1 Mbaud on the panel's microSD pins.
//
// Why not 17/18: electrically free, but not broken out to any connector on this
// board. Why not 43/44: that is the CH340 console, and its header pads are torn.
// Why not 19/20: that is the I2C bus carrying the GT911 touch controller and the
// PCA9557 that releases its reset — taking it would cost all touch input.
//
// The panel's microSD slot is unused by this firmware, so its four pins are free
// and its socket pads are reachable. Solder points (standard microSD pinout —
// verify against the board before soldering):
//
//   socket pin 3  (CMD  / MOSI) = GPIO 11  -> TX, to the Airdale's RX
//   socket pin 6  (VSS)         = GND      -> common ground
//   socket pin 7  (DAT0 / MISO) = GPIO 13  -> RX, to the Airdale's TX
//
// TX and RX are deliberately not adjacent: VDD, CLK and GND sit between them,
// so a solder bridge shorts a data line to a rail rather than to the other data
// line. Leave the SD slot empty — an inserted card loads these pins.
#define FNC_UART_NUM 2
#define FNC_TX_PIN   11
#define FNC_RX_PIN   13

void init_fnc_uart();
void reinit_fnc_uart();

// Byte counters for the link diagnostic. Cheap, and the only way to tell
// "wired wrong" (rx stays 0) from "wrong baud" (rx climbs, nothing parses).
unsigned long fnc_rx_count();
unsigned long fnc_tx_count();

extern "C" {
void fnc_putchar(uint8_t c);
int  fnc_getchar();
bool fnc_rx_waiting();
}
