#pragma once
#include <stdint.h>

// Persistent display settings, NVS-backed. Written on change, not on a timer:
// these change a few times in a machine's life, and NVS has finite write cycles.

void settings_load();

bool settings_uart_enabled();
void settings_set_uart_enabled(bool on);

bool settings_wifi_enabled();
void settings_set_wifi_enabled(bool on);

const char* settings_wifi_ssid();
const char* settings_wifi_pass();
void        settings_set_wifi_creds(const char* ssid, const char* pass);

// Empty means "discover via mDNS" — FluidNC advertises _telnet._tcp in STA mode.
const char* settings_fnc_host();
uint16_t    settings_fnc_port();
void        settings_set_fnc_host(const char* host, uint16_t port);

uint8_t settings_brightness();
void    settings_set_brightness(uint8_t b);
