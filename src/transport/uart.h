#pragma once
#include <stdint.h>

// FluidNC link: the pendant cable from the Airdale plugs into the module's
// TXD0/RXD0 pads — pin 37 (GPIO43) and pin 36 (GPIO44). Same pins the FTDI uses
// for programming; the two are never connected at once.
//
//   module pin 37 = GPIO 43 = TX -> Airdale pendant RX
//   module pin 36 = GPIO 44 = RX <- Airdale pendant TX
//
// These are UART0's default pins, which is why the debug console is moved off
// them in main.cpp — 115200-baud log text on a 1 Mbaud link would be garbage in
// both directions. We drive them from the UART2 peripheral instead; the GPIO
// matrix does not care which peripheral owns which pin, and taking UART0 out of
// the picture entirely avoids a baud-rate fight over the same hardware.
#define FNC_UART_NUM 2
#define FNC_TX_PIN   43
#define FNC_RX_PIN   44

void init_fnc_uart();
void reinit_fnc_uart();

// Byte counters for the link diagnostic. Cheap, and the only way to tell
// "wired wrong" (rx stays 0) from "wrong baud" (rx climbs, nothing parses).
unsigned long fnc_rx_count();
unsigned long fnc_tx_count();

// Change the link speed without tearing the driver down. Used by the baud hunt
// in main.cpp: the same two pins carry a 1 Mbaud pendant or a 115200 FTDI
// depending on what is plugged in, and the firmware should not care which.
void     fnc_set_baud(uint32_t baud);
uint32_t fnc_get_baud();

// Raw UART access. fnc_putchar/fnc_getchar live in transport.cpp, which picks
// between this and the WiFi link at runtime.
void uart_putc(uint8_t c);
int  uart_getc();
bool uart_rx_waiting();
