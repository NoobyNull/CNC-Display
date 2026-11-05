#pragma once
#include <stdint.h>

// Is the controller still there? FluidNC only auto-reports when something
// changes, so an idle machine on a healthy cable can be silent for minutes.
// After PING_AFTER_MS of silence ask for a status report (one realtime '?',
// repeated every PING_EVERY_MS); if nothing at all has arrived for LOST_AFTER_MS
// the link is gone: cable out, controller off, or controller wedged.
//
// Pure: fed the clock and the UART's received-byte counter, it says what to do.
// The caller sends the '?' and handles the loss.

#define LINKWATCH_PING_AFTER_MS 1000
#define LINKWATCH_PING_EVERY_MS 1000
#define LINKWATCH_LOST_AFTER_MS 4000

typedef enum { LINKWATCH_OK, LINKWATCH_PING, LINKWATCH_LOST } linkwatch_t;

// connected: the panel currently believes the link is up. While it is not,
// the watch only tracks the counter, so coming up starts a fresh window.
linkwatch_t linkwatch_tick(uint32_t now_ms, unsigned long rx_count, bool connected);
void        linkwatch_reset();
