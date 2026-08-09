#include "transport/wifi_link.h"
#include "model/settings.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Arduino.h>

// State machine: OFF -> JOINING -> DISCOVER -> CONNECTING -> UP, with a retry
// path back to JOINING. Everything is non-blocking; nothing here may stall the
// UI loop, which is why there is not a single WiFi.waitForConnectResult().

enum wl_state_t { WL_OFF, WL_JOINING, WL_DISCOVER, WL_CONNECTING, WL_UP };

static wl_state_t   s_state = WL_OFF;
static WiFiClient   s_client;
static IPAddress    s_ip;
static uint16_t     s_port = 23;
static uint32_t     s_next_ms = 0;
static bool         s_mdns_up = false;
static char         s_status[48] = "off";

static void set_status(const char* s) {
    strncpy(s_status, s, sizeof(s_status) - 1);
    s_status[sizeof(s_status) - 1] = 0;
}

const char* wifilink_status() { return s_status; }
bool wifilink_connected() { return s_state == WL_UP && s_client.connected(); }

void wifilink_stop() {
    if (s_client.connected()) s_client.stop();
    if (s_mdns_up) { MDNS.end(); s_mdns_up = false; }
    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_OFF);
    s_state = WL_OFF;
    set_status("off");
}

void wifilink_begin() {
    if (!settings_wifi_enabled() || !settings_wifi_ssid()[0]) {
        wifilink_stop();
        return;
    }
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);            // latency matters more than power here
    WiFi.begin(settings_wifi_ssid(), settings_wifi_pass());
    s_state   = WL_JOINING;
    s_next_ms = millis() + 500;
    set_status("joining wifi");
}

void wifilink_poll() {
    if (!settings_wifi_enabled()) {
        if (s_state != WL_OFF) wifilink_stop();
        return;
    }
    if (s_state == WL_OFF) { wifilink_begin(); return; }

    uint32_t now = millis();
    if ((int32_t)(now - s_next_ms) < 0) return;

    switch (s_state) {
        case WL_JOINING:
            if (WiFi.status() == WL_CONNECTED) {
                s_state = WL_DISCOVER;
                set_status("wifi ok, finding fluidnc");
            } else {
                s_next_ms = now + 500;
            }
            return;

        case WL_DISCOVER: {
            // A host set by hand wins; otherwise ask mDNS. FluidNC advertises
            // _telnet._tcp from TelnetServer.cpp when it is in STA mode.
            const char* host = settings_fnc_host();
            if (host && host[0]) {
                s_ip.fromString(host);
                s_port = settings_fnc_port();
                if (s_ip == IPAddress((uint32_t)0)) {
                    s_ip = MDNS.queryHost(host, 2000);
                }
            } else {
                if (!s_mdns_up) s_mdns_up = MDNS.begin("cnc-console");
                int n = MDNS.queryService("telnet", "tcp");
                if (n > 0) {
                    s_ip   = MDNS.address(0);   // arduino-esp32 3.x names it address(), not IP()
                    s_port = MDNS.port(0);
                } else {
                    set_status("no fluidnc found");
                    s_next_ms = now + 5000;
                    return;
                }
            }
            s_state   = WL_CONNECTING;
            s_next_ms = now;
            return;
        }

        case WL_CONNECTING: {
            char b[48];
            snprintf(b, sizeof(b), "connecting %s:%u", s_ip.toString().c_str(), s_port);
            set_status(b);
            if (s_client.connect(s_ip, s_port, 2000)) {
                s_client.setNoDelay(true);   // jog lines must not wait on Nagle
                s_state = WL_UP;
                snprintf(b, sizeof(b), "telnet %s:%u", s_ip.toString().c_str(), s_port);
                set_status(b);
            } else {
                s_state   = WL_DISCOVER;
                s_next_ms = now + 3000;
                set_status("connect failed");
            }
            return;
        }

        case WL_UP:
            if (!s_client.connected() || WiFi.status() != WL_CONNECTED) {
                s_client.stop();
                s_state   = (WiFi.status() == WL_CONNECTED) ? WL_DISCOVER : WL_JOINING;
                s_next_ms = now + 1000;
                set_status("link lost");
            }
            return;

        default:
            return;
    }
}

void wifilink_putc(uint8_t c) {
    if (wifilink_connected()) s_client.write(c);
}

int wifilink_getc() {
    if (!wifilink_connected()) return -1;
    return s_client.available() ? s_client.read() : -1;
}

bool wifilink_rx_waiting() {
    return wifilink_connected() && s_client.available() > 0;
}
