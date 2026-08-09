#include "model/path_loader.h"
#include "model/gcode_path.h"
#include "model/FluidNCModel.h"
#include "model/console_log.h"
#include "transport/xmodem_rx.h"
#include "transport/uart.h"
#include <string.h>
#include <stdio.h>

static bool   s_busy = false;
static bool   s_ok   = false;
static char   s_line[256];
static size_t s_linelen = 0;

static void tx(uint8_t c) { fnc_putchar(c); }

// Split the byte stream into lines and feed the parser. XModem pads the final
// block with CTRL-Z; discard those rather than parse them.
static void sink(const uint8_t* d, size_t n) {
    for (size_t i = 0; i < n; i++) {
        uint8_t c = d[i];
        if (c == 0x1A) continue;
        if (c == '\n' || c == '\r') {
            if (s_linelen) {
                s_line[s_linelen] = 0;
                gpath_feed_line(s_line);
                s_linelen = 0;
            }
            continue;
        }
        if (s_linelen < sizeof(s_line) - 1) s_line[s_linelen++] = (char)c;
    }
}

bool pathload_start(const char* path) {
    if (s_busy) return false;
    if (!(state == Idle || state == Alarm)) {
        clog_add("[MSG:Cannot load toolpath unless Idle or Alarm]");
        return false;
    }
    gpath_reset();
    s_linelen = 0;
    s_busy = true;
    s_ok   = false;
    xmodem_rx_begin(tx, sink);
    send_linef("$XS=%s", path);
    return true;
}

void pathload_poll() {
    if (!s_busy) return;
    if (!xmodem_rx_done()) return;

    s_busy = false;
    s_ok   = xmodem_rx_ok();
    if (s_linelen) {
        s_line[s_linelen] = 0;
        gpath_feed_line(s_line);
        s_linelen = 0;
    }
    char b[80];
    snprintf(b, sizeof(b), "[MSG:Toolpath %s, %u points, %u bytes]",
             s_ok ? "loaded" : "failed",
             (unsigned)gpath_count(), (unsigned)xmodem_rx_bytes());
    clog_add(b);
}

bool   pathload_busy()  { return s_busy; }
bool   pathload_ok()    { return s_ok; }
size_t pathload_bytes() { return xmodem_rx_bytes(); }
