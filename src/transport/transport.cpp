#include "transport/transport.h"
#include "transport/uart.h"
#include "transport/wifi_link.h"
#include "model/settings.h"
#include "transport/uart.h"
#include "model/FluidNCModel.h"
#include "GrblParserC.h"
#include <Arduino.h>
#include <stdio.h>

// The UART terminal carries either the Airdale pendant at 1 Mbaud or a bench
// adapter at 115200 depending on which cable is plugged in, so the UART
// candidate is really two candidates. Cycle rate every 5s while hunting.
static const uint32_t UART_BAUDS[] = { 1000000, 115200 };
static const int      N_UART_BAUDS = sizeof(UART_BAUDS) / sizeof(UART_BAUDS[0]);

static transport_t s_active = TRANSPORT_NONE;
static int         s_baud_idx = 0;
static uint32_t    s_next_hunt = 0;
static char        s_detail[64] = "no link";

void transport_init() {
    init_fnc_uart();
    if (settings_wifi_enabled()) wifilink_begin();
}

void transport_settings_changed() {
    if (settings_wifi_enabled()) wifilink_begin();
    else                         wifilink_stop();
    s_active    = TRANSPORT_NONE;
    s_next_hunt = 0;
}

transport_t transport_active() { return s_active; }

const char* transport_label() {
    switch (s_active) {
        case TRANSPORT_UART:
            return fnc_get_baud() >= 1000000 ? "UART 1M" : "UART 115K";
        case TRANSPORT_WIFI: return "TELNET";
        default:             return "NO LINK";
    }
}

const char* transport_detail() { return s_detail; }

void transport_poll() {
    if (settings_wifi_enabled()) wifilink_poll();

    // Once FluidNC is answering, stop hunting and leave the link alone.
    if (state != Disconnected && s_active != TRANSPORT_NONE) {
        snprintf(s_detail, sizeof(s_detail), "%s connected", transport_label());
        return;
    }

    uint32_t now = millis();
    if ((int32_t)(now - s_next_hunt) < 0) return;
    s_next_hunt = now + 2000;

    // WiFi wins as soon as its TCP session is up — it is either connected or it
    // is not, no ambiguity. The UART has to be probed.
    if (settings_wifi_enabled() && wifilink_connected()) {
        s_active = TRANSPORT_WIFI;
        snprintf(s_detail, sizeof(s_detail), "%s", wifilink_status());
        fnc_realtime(StatusReport);
        return;
    }

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
    snprintf(s_detail, sizeof(s_detail), "%s",
             settings_wifi_enabled() ? wifilink_status() : "all transports off");
}

// ---- the interface GrblParserC sees -----------------------------------------

extern "C" void fnc_putchar(uint8_t c) {
    switch (s_active) {
        case TRANSPORT_UART: uart_putc(c);     break;
        case TRANSPORT_WIFI: wifilink_putc(c); break;
        default: break;
    }
}

extern "C" int fnc_getchar() {
    switch (s_active) {
        case TRANSPORT_UART: return uart_getc();
        case TRANSPORT_WIFI: return wifilink_getc();
        default: return -1;
    }
}

extern "C" bool fnc_rx_waiting() {
    switch (s_active) {
        case TRANSPORT_UART: return uart_rx_waiting();
        case TRANSPORT_WIFI: return wifilink_rx_waiting();
        default: return false;
    }
}
