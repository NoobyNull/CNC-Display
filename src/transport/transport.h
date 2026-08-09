#pragma once
#include <stdint.h>

// Picks between the wired UART and the WiFi/Telnet link at runtime, honouring
// the on-screen Setup toggles. GrblParserC only ever sees fnc_putchar/getchar,
// so nothing above this layer knows or cares which one is live.

enum transport_t { TRANSPORT_NONE = 0, TRANSPORT_UART, TRANSPORT_WIFI };

void        transport_init();
void        transport_poll();       // hunting, reconnection, baud cycling
transport_t transport_active();
const char* transport_label();      // e.g. "UART 1M", "TELNET", "NO LINK"
const char* transport_detail();     // longer status for the Setup page

// Called when a Setup toggle changes so the hunt restarts immediately.
void transport_settings_changed();

extern "C" {
void fnc_putchar(uint8_t c);
int  fnc_getchar();
bool fnc_rx_waiting();
}
