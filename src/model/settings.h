#pragma once
#include <stdint.h>

// Persistent display settings, NVS-backed. Written on change, not on a timer:
// these change a few times in a machine's life, and NVS has finite write cycles.

void settings_load();

bool settings_uart_enabled();
void settings_set_uart_enabled(bool on);

// 0 = monitor, 1 = soft (feed hold), 2 = hard (feed hold + soft reset on impact)
uint8_t settings_vib_mode();
void    settings_set_vib_mode(uint8_t m);

// Vibration sensitivity, persisted so a machine keeps its tuning across a power
// cycle. Defaults are CNC-TW's, arrived at on a real machine. Stored as floats
// in g / g / g-per-second, matching vib_set_thresholds().
float settings_vib_hold();     // sustained RMS -> feed hold
float settings_vib_peak();     // impact spike  -> e-stop
float settings_vib_jerk();     // sudden change -> e-stop
void  settings_set_vib_thresholds(float hold_g, float peak_g, float jerk_gps);

uint8_t settings_brightness();
void    settings_set_brightness(uint8_t b);

// WiFi exists ONLY for the network firmware update (see fw_update.cpp);
// nothing else may use these.
const char* settings_wifi_ssid();
const char* settings_wifi_pass();
const char* settings_update_url();
void settings_set_wifi(const char* ssid, const char* pass);
void settings_set_update_url(const char* url);

// Minutes of no touch before the backlight goes off, 0..60. 0 = never blank,
// and that is the default: a panel that goes dark unasked next to a running
// spindle is the wrong surprise, so this is opt-in.
// Which optional pages the rail shows. Held as a bitmask over page_id_t so one
// NVS key covers all of them, defaulting to every page on: a panel that boots
// with pages missing and no memory of why is worse than one showing a page you
// do not use.
bool settings_page_enabled(int page_id);
void settings_set_page_enabled(int page_id, bool on);

uint8_t settings_screen_off_min();
void    settings_set_screen_off_min(uint8_t m);
