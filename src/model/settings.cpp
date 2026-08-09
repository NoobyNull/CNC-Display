#include "model/settings.h"
#include <Preferences.h>
#include <string.h>

static Preferences prefs;

static bool     s_uart_en = true;
static bool     s_wifi_en = false;
static char     s_ssid[33] = { 0 };
static char     s_pass[65] = { 0 };
static char     s_host[64] = { 0 };
static uint16_t s_port     = 23;
static uint8_t  s_bright   = 200;
static uint8_t  s_vibmode  = 0;
static uint8_t  s_linkmode = 0;   // UPDATE by default: a fresh flash must be recoverable   // monitor until deliberately armed

void settings_load() {
    prefs.begin("cncdisp", /*readOnly=*/true);
    s_uart_en = prefs.getBool("uart_en", true);
    s_wifi_en = prefs.getBool("wifi_en", false);
    prefs.getString("ssid", s_ssid, sizeof(s_ssid));
    prefs.getString("pass", s_pass, sizeof(s_pass));
    prefs.getString("host", s_host, sizeof(s_host));
    s_port   = prefs.getUShort("port", 23);
    s_bright  = prefs.getUChar("bright", 200);
    s_vibmode  = prefs.getUChar("vibmode", 0);
    s_linkmode = prefs.getUChar("linkmode", 0);
    prefs.end();
}

static void put_bool(const char* k, bool v) {
    prefs.begin("cncdisp", false); prefs.putBool(k, v); prefs.end();
}

bool settings_uart_enabled() { return s_uart_en; }
void settings_set_uart_enabled(bool on) {
    if (on == s_uart_en) return;
    s_uart_en = on; put_bool("uart_en", on);
}

bool settings_wifi_enabled() { return s_wifi_en; }
void settings_set_wifi_enabled(bool on) {
    if (on == s_wifi_en) return;
    s_wifi_en = on; put_bool("wifi_en", on);
}

const char* settings_wifi_ssid() { return s_ssid; }
const char* settings_wifi_pass() { return s_pass; }

void settings_set_wifi_creds(const char* ssid, const char* pass) {
    if (ssid) { strncpy(s_ssid, ssid, sizeof(s_ssid) - 1); s_ssid[sizeof(s_ssid) - 1] = 0; }
    if (pass) { strncpy(s_pass, pass, sizeof(s_pass) - 1); s_pass[sizeof(s_pass) - 1] = 0; }
    prefs.begin("cncdisp", false);
    prefs.putString("ssid", s_ssid);
    prefs.putString("pass", s_pass);
    prefs.end();
}

const char* settings_fnc_host() { return s_host; }
uint16_t    settings_fnc_port() { return s_port; }

void settings_set_fnc_host(const char* host, uint16_t port) {
    if (host) { strncpy(s_host, host, sizeof(s_host) - 1); s_host[sizeof(s_host) - 1] = 0; }
    s_port = port ? port : 23;
    prefs.begin("cncdisp", false);
    prefs.putString("host", s_host);
    prefs.putUShort("port", s_port);
    prefs.end();
}

uint8_t settings_link_mode() { return s_linkmode; }
void settings_set_link_mode(uint8_t m) {
    if (m > 1 || m == s_linkmode) return;
    s_linkmode = m;
    prefs.begin("cncdisp", false); prefs.putUChar("linkmode", m); prefs.end();
}

uint8_t settings_vib_mode() { return s_vibmode; }
void settings_set_vib_mode(uint8_t m) {
    if (m > 2 || m == s_vibmode) return;
    s_vibmode = m;
    prefs.begin("cncdisp", false); prefs.putUChar("vibmode", m); prefs.end();
}

uint8_t settings_brightness() { return s_bright; }
void settings_set_brightness(uint8_t b) {
    s_bright = b;
    prefs.begin("cncdisp", false); prefs.putUChar("bright", b); prefs.end();
}
