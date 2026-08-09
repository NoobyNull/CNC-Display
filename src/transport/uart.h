#pragma once
#include <stdint.h>

// FluidNC link: UART2, GPIO17 TX, GPIO18 RX, 1 Mbaud.
// GPIO 43/44 are the USB console and must not be reused — see the note in
// docs/superpowers/specs about uart_input.cpp in the hotkey project.
#define FNC_UART_NUM 2
#define FNC_TX_PIN   17
#define FNC_RX_PIN   18

void init_fnc_uart();
void reinit_fnc_uart();

extern "C" {
void fnc_putchar(uint8_t c);
int  fnc_getchar();
bool fnc_rx_waiting();
}
