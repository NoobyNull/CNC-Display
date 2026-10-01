#pragma once
#include <stdint.h>

// FluidNC link: the pendant cable from the Airedale plugs into the Advance
// 7.0's UART0-OUT connector (HY2.0) — 3.3V direct and push-pull. That is the
// only pairing ever verified at 1 Mbaud.
//
// UART0-IN (the XH2.54 next to the function DIP) reaches the same two nets
// through a BSS138 shifter and adds a 5V/2A power input. Feeding the panel
// there is fine and is how this one is powered; its data pins are the ones to
// leave alone, since the shifter exists for 5V Arduino hosts rather than a
// 3.3V controller. Elecrow's own docs and our own comment history disagree on
// its pin order, so meter that connector rather than trusting either.
//
//   GPIO 43 = TX -> Airedale pendant RX
//   GPIO 44 = RX <- Airedale pendant TX
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

// False when the peripheral never came up, so the caller does not declare a
// link ready over a driver that failed to install.
bool init_fnc_uart();
void reinit_fnc_uart();

// Byte counters for the link diagnostic. Cheap, and the only way to tell
// "wired wrong" (rx stays 0) from "wrong baud" (rx climbs, nothing parses).
struct uart_rx_losses_t {
    unsigned long fifo_ovf, buf_full, frame_err, parity_err;
};
// Drain the driver's event queue; true if any receive loss happened since the
// last call. Call often (the queue is small). Totals in uart_rx_losses().
bool uart_poll_events();
const uart_rx_losses_t& uart_rx_losses();

unsigned long fnc_rx_count();
unsigned long fnc_tx_count();

// Empty when init was clean, else the failing call and its code ("drv257") or
// a short internal loopback ("lb0/4"). Belongs on the glass because Serial is
// un-begun exactly when the link owns the pins.
const char* fnc_init_error();

// Fixed at FNC_BAUD; exposed so the UI can say what speed the link runs at
// instead of hardcoding the number in labels.
uint32_t fnc_get_baud();

// Raw UART access. fnc_putchar/fnc_getchar live in transport.cpp, for which
// this is now the only transport.
void uart_putc(uint8_t c);
int  uart_getc();
bool uart_rx_waiting();
