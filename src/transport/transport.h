#pragma once
#include <stdint.h>

// The wired UART link, hunting across its two candidate baud rates. GrblParserC
// only ever sees fnc_putchar/getchar, so nothing above this layer knows how the
// bytes get there.
//
// A WiFi/Telnet transport used to sit alongside this one and was removed: the
// ESP32-S3's WiFi stack blacks out the flash cache, and the RGB panel's ISR is
// not IRAM-safe in arduino-esp32's prebuilt libs, so the display tore and
// drifted whenever the radio worked. The machine link is a cable.

enum transport_t { TRANSPORT_NONE = 0, TRANSPORT_UART };

void        transport_init();
void        transport_poll();       // hunting, reconnection, baud cycling
transport_t transport_active();
const char* transport_label();      // e.g. "UART 1M", "UART 115K", "NO LINK"
const char* transport_detail();     // longer status for the Setup page

// Called when a Setup toggle changes so the hunt restarts immediately.
void transport_settings_changed();

extern "C" {
void fnc_putchar(uint8_t c);
int  fnc_getchar();
bool fnc_rx_waiting();
}
