#include "transport/transport.h"
#include "transport/uart.h"
#include "model/settings.h"
#include "model/FluidNCModel.h"
#include "GrblParserC.h"
#include <Arduino.h>
#include <stdio.h>

// The link is the Airedale pendant cable at a fixed FNC_BAUD. The old baud
// hunt (a 115200 bench-adapter candidate cycled every 2s) is gone: the pendant
// end is known hardware now, so there is nothing to hunt for.

static char s_detail[64] = "no link";

// On this board the link and the debug console are the same physical UART pins
// (43/44 — see uart.h), so the driver is only installed when the Setup toggle
// says the link owns them; otherwise the console keeps them. Enabling the
// toggle at runtime claims the pins live (and the console goes quiet until the
// next boot with the toggle off).
static bool s_uart_ready = false;

void transport_init() {
    if (settings_uart_enabled()) {
        init_fnc_uart();
        s_uart_ready = true;
    }
}

void transport_settings_changed() {
    if (settings_uart_enabled() && !s_uart_ready) {
        init_fnc_uart();
        s_uart_ready = true;
    }
}

const char* transport_label() {
    if (!s_uart_ready || state == Disconnected) return "NO LINK";
    return fnc_get_baud() >= 1000000 ? "UART 1M" : "UART 115K";
}

const char* transport_detail() { return s_detail; }

void transport_poll() {
    if (!settings_uart_enabled()) {
        snprintf(s_detail, sizeof(s_detail), "UART disabled");
        return;
    }
    if (state != Disconnected) {
        snprintf(s_detail, sizeof(s_detail), "%s connected", transport_label());
        return;
    }

    // FluidNC only auto-reports on a channel after it has heard something, so
    // keep poking until the controller answers.
    static uint32_t next_poke = 0;
    uint32_t now = millis();
    if ((int32_t)(now - next_poke) < 0) return;
    next_poke = now + 2000;
    snprintf(s_detail, sizeof(s_detail), "waiting for controller @ %lu",
             (unsigned long)fnc_get_baud());
    fnc_realtime(StatusReport);
}

// ---- the interface GrblParserC sees -----------------------------------------

extern "C" void fnc_putchar(uint8_t c) {
    if (s_uart_ready) uart_putc(c);
}

extern "C" int fnc_getchar() {
    return s_uart_ready ? uart_getc() : -1;
}

extern "C" bool fnc_rx_waiting() {
    return s_uart_ready && uart_rx_waiting();
}
