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

// GPIO 43/44 can be the programming/debug console OR the FluidNC link, never
// both — one is 115200 log text, the other a 1 Mbaud machine link.
//   0 = UPDATE  : console owns 43/44, UART transport disabled. Always flashable.
//   1 = PENDANT : FluidNC owns 43/44, console parks on the unconnected GPIO 42.
#define LINK_MODE_UPDATE  0
#define LINK_MODE_PENDANT 1
uint8_t settings_link_mode();
void    settings_set_link_mode(uint8_t m);

// 0 = monitor, 1 = soft (feed hold), 2 = hard (feed hold + soft reset on impact)
uint8_t settings_vib_mode();
void    settings_set_vib_mode(uint8_t m);

uint8_t settings_brightness();
void    settings_set_brightness(uint8_t b);
