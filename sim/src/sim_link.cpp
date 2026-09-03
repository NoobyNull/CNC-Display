// The machine link as a TCP socket to fluidnc_sim.py.
//
// src/transport/transport.h is the entire hardware boundary for the FluidNC
// connection: GrblParserC and everything above it only ever calls
// fnc_putchar/fnc_getchar/fnc_rx_waiting. Re-implementing those three over a
// socket is all it takes to run the real firmware against the fake controller.

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <Arduino.h>

#include "transport/transport.h"
#include "transport/uart.h"
#include "model/settings.h"
#include "GrblParserC.h"   // fnc_realtime / StatusReport for the reconnect poke

static int  s_fd = -1;
static char s_detail[64] = "no link";
static char s_init_err[32] = "";
static unsigned long s_rx = 0, s_tx = 0;

static char s_host[64] = "127.0.0.1";
static int  s_port = 2323;

// A small ring, drained by loop(). The firmware pulls up to 512 bytes per
// iteration, so this is sized well above one drain's worth.
static uint8_t  s_ring[4096];
static size_t   s_head = 0, s_tail = 0;

void sim_link_target(const char* host, int port) {
    if (host && *host) { strncpy(s_host, host, sizeof(s_host) - 1); s_host[sizeof(s_host) - 1] = 0; }
    if (port > 0) s_port = port;
}

static void link_close() {
    if (s_fd >= 0) { close(s_fd); s_fd = -1; }
}

static bool link_open() {
    link_close();
    s_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (s_fd < 0) { snprintf(s_init_err, sizeof(s_init_err), "socket%d", errno); return false; }

    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port   = htons((uint16_t)s_port);
    if (inet_pton(AF_INET, s_host, &a.sin_addr) != 1) {
        snprintf(s_init_err, sizeof(s_init_err), "addr");
        link_close();
        return false;
    }
    if (connect(s_fd, (sockaddr*)&a, sizeof(a)) != 0) {
        snprintf(s_init_err, sizeof(s_init_err), "conn%d", errno);
        link_close();
        return false;
    }

    fcntl(s_fd, F_SETFL, O_NONBLOCK);
    // The panel sends single realtime bytes and expects an answer immediately;
    // Nagle would sit on them waiting for a full segment.
    int one = 1;
    setsockopt(s_fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    s_init_err[0] = 0;
    return true;
}

bool init_fnc_uart() { return link_open(); }
void reinit_fnc_uart() { link_open(); }

unsigned long fnc_rx_count() { return s_rx; }
unsigned long fnc_tx_count() { return s_tx; }
const char*   fnc_init_error() { return s_init_err; }
uint32_t      fnc_get_baud() { return FNC_BAUD; }

static void pump() {
    if (s_fd < 0) return;
    uint8_t buf[1024];
    for (;;) {
        ssize_t n = recv(s_fd, buf, sizeof(buf), 0);
        if (n > 0) {
            for (ssize_t i = 0; i < n; i++) {
                size_t next = (s_head + 1) % sizeof(s_ring);
                if (next == s_tail) break;    // full: drop, same as a UART overrun
                s_ring[s_head] = buf[i];
                s_head = next;
                s_rx++;
            }
            continue;
        }
        if (n == 0) { link_close(); return; }        // peer closed
        if (errno == EAGAIN || errno == EWOULDBLOCK) return;
        link_close();
        return;
    }
}

extern "C" void fnc_putchar(uint8_t c) {
    if (s_fd < 0) return;
    if (send(s_fd, &c, 1, MSG_NOSIGNAL) == 1) s_tx++;
    else link_close();
}

extern "C" int fnc_getchar() {
    pump();
    if (s_tail == s_head) return -1;
    uint8_t c = s_ring[s_tail];
    s_tail = (s_tail + 1) % sizeof(s_ring);
    return c;
}

extern "C" bool fnc_rx_waiting() {
    pump();
    return s_tail != s_head;
}

// Both of these live in src/transport/uart.cpp on the panel, which this file
// replaces wholesale, so the simulator has to supply them too.

// Clear a stale XOFF on the far end.
void resetFlowControl() { fnc_putchar(0x11); }

// GrblParserC requires the host to supply a millisecond clock.
extern "C" int milliseconds() { return (int)millis(); }

void transport_init() {
    if (settings_uart_enabled()) link_open();
}

void transport_settings_changed() {
    if (settings_uart_enabled() && s_fd < 0) link_open();
    else if (!settings_uart_enabled()) link_close();
}

const char* transport_label() {
    if (!settings_uart_enabled()) return "UART OFF";
    return s_fd >= 0 ? "SIM LINK" : "NO LINK";
}

const char* transport_detail() {
    if (!settings_uart_enabled()) return "link disabled in Setup";
    snprintf(s_detail, sizeof(s_detail), "%s:%d rx=%lu tx=%lu %s",
             s_host, s_port, s_rx, s_tx, s_init_err);
    return s_detail;
}

void transport_poll() {
    static uint32_t next = 0;
    if (!settings_uart_enabled()) return;
    uint32_t now = millis();
    if ((int32_t)(now - next) < 0) return;
    next = now + 2000;
    // Reconnect on demand: restarting fluidnc_sim.py should not mean
    // restarting the panel.
    if (s_fd < 0) link_open();
    fnc_realtime(StatusReport);
}
