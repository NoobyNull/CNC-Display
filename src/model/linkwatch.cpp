#include "model/linkwatch.h"

static bool          s_init = false;
static unsigned long s_rx;
static uint32_t      s_last_rx_ms;
static uint32_t      s_last_ping_ms;

void linkwatch_reset() { s_init = false; }

linkwatch_t linkwatch_tick(uint32_t now, unsigned long rx, bool connected) {
    if (!s_init || rx != s_rx || !connected) {
        s_init = true;
        s_rx = rx;
        s_last_rx_ms = now;
        s_last_ping_ms = now;
        return LINKWATCH_OK;
    }
    const uint32_t silent = now - s_last_rx_ms;
    if (silent >= LINKWATCH_LOST_AFTER_MS) {
        s_init = false;   // report it once; the next tick starts over
        return LINKWATCH_LOST;
    }
    if (silent >= LINKWATCH_PING_AFTER_MS && now - s_last_ping_ms >= LINKWATCH_PING_EVERY_MS) {
        s_last_ping_ms = now;
        return LINKWATCH_PING;
    }
    return LINKWATCH_OK;
}
