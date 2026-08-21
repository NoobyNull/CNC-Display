#include "transport/transport.h"
#include "transport/uart.h"
#include "model/settings.h"
#include "model/FluidNCModel.h"
#include "GrblParserC.h"
#include <Arduino.h>
#include <stdio.h>

// The UART terminal carries either the Airdale pendant at 1 Mbaud or a bench
// adapter at 115200 depending on which cable is plugged in, so the UART
// candidate is really two candidates. Cycle rate every 2s while hunting.
static const uint32_t UART_BAUDS[] = { 1000000, 115200 };
static const int      N_UART_BAUDS = sizeof(UART_BAUDS) / sizeof(UART_BAUDS[0]);

static transport_t s_active = TRANSPORT_NONE;
static int         s_baud_idx = 0;
static uint32_t    s_next_hunt = 0;
static char        s_detail[64] = "no link";

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
    s_active    = TRANSPORT_NONE;
    s_next_hunt = 0;
    if (settings_uart_enabled() && !s_uart_ready) {
        init_fnc_uart();
        s_uart_ready = true;
    }
}

transport_t transport_active() { return s_active; }

const char* transport_label() {
    switch (s_active) {
        case TRANSPORT_UART:
            return fnc_get_baud() >= 1000000 ? "UART 1M" : "UART 115K";
        default:
            return "NO LINK";
    }
}

const char* transport_detail() { return s_detail; }

void transport_poll() {
    // Once FluidNC is answering, stop hunting and leave the link alone.
    if (state != Disconnected && s_active != TRANSPORT_NONE) {
        snprintf(s_detail, sizeof(s_detail), "%s connected", transport_label());
        return;
    }

    uint32_t now = millis();
    if ((int32_t)(now - s_next_hunt) < 0) return;
    s_next_hunt = now + 2000;

    if (settings_uart_enabled()) {
        s_active = TRANSPORT_UART;
        s_baud_idx = (s_baud_idx + 1) % N_UART_BAUDS;
        fnc_set_baud(UART_BAUDS[s_baud_idx]);
        snprintf(s_detail, sizeof(s_detail), "probing UART @ %lu",
                 (unsigned long)UART_BAUDS[s_baud_idx]);
        fnc_realtime(StatusReport);
        return;
    }

    s_active = TRANSPORT_NONE;
    snprintf(s_detail, sizeof(s_detail), "UART disabled");
}

// ---- the interface GrblParserC sees -----------------------------------------

extern "C" void fnc_putchar(uint8_t c) {
    if (s_active == TRANSPORT_UART) uart_putc(c);
}

extern "C" int fnc_getchar() {
    return s_active == TRANSPORT_UART ? uart_getc() : -1;
}

extern "C" bool fnc_rx_waiting() {
    return s_active == TRANSPORT_UART ? uart_rx_waiting() : false;
}
