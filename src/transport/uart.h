#pragma once
#include <stdint.h>

// FluidNC link: the pendant cable from the Airdale plugs into the board's UART
// terminal, the PH2.0 header next to the USB-UART Type-C.
//
//   GPIO 43 = TX -> Airdale pendant RX
//   GPIO 44 = RX <- Airdale pendant TX
//
// An onboard analog mux gives those two pins to *either* the CH343 USB-UART or
// the terminal, never both — the board switch labelled UART picks one. Set it
// to the terminal and leave it there: flashing and the console go over the
// native USB Type-C, which does not touch these pins.
//
// These are UART0's default pins. We drive them from the UART2 peripheral
// instead; the GPIO matrix does not care which peripheral owns which pin, and
// taking UART0 out of the picture avoids a baud-rate fight over the same
// hardware if anything ever re-enables a console there.
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

// Raw UART access. fnc_putchar/fnc_getchar live in transport.cpp, for which
// this is now the only transport.
void uart_putc(uint8_t c);
int  uart_getc();
bool uart_rx_waiting();
