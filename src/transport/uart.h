#pragma once
#include <stdint.h>

// FluidNC link: the pendant cable from the Airdale plugs into the Advance
// 7.0's UART0-IN connector (the XH2.54 next to the function DIP; pin order
// GND · 5V-IN · IO43-TX0 · IO44-RX0). UART0-OUT (HY2.0) is the same two nets
// before the isolation FETs.
//
//   GPIO 43 = TX -> Airdale pendant RX
//   GPIO 44 = RX <- Airdale pendant TX
//
// The CH340K on the USB Type-C sits on these same nets — there is no mux and
// no other UART connector once the function DIP is on TF-CARD (UART1-OUT is
// behind that mux and dead in TF-CARD mode; verified on the V1.4 schematic).
// So the link and the debug console are runtime-exclusive: the Setup "Wired
// UART" toggle decides which one owns the pins at boot (see main.cpp).
//
// These are UART0's default pins. We drive them from the UART2 peripheral
// instead; the GPIO matrix does not care which peripheral owns which pin, and
// taking UART0 out of the picture avoids a baud-rate fight with the console's
// UART0 peripheral over the same hardware.
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
