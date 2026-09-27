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

// Continuous-jog speed as a percentage of each axis's configured max rate.
// Panel-side rather than the controller's overrides: RAPID has no +/- (three
// fixed positions) and FEED governs cutting moves, so borrowing it would scale
// the next G1 as a side effect of jogging. See docs/HANDOFF.md.
uint8_t settings_jog_pct();
void    settings_set_jog_pct(uint8_t pct);

uint8_t settings_screen_off_min();
void    settings_set_screen_off_min(uint8_t m);

// Probe page: which touch plate is on the machine, and for the Sienci AutoZero,
// which corner of the stock it sits on. 0 = flat plate (Z only), 1 = AutoZero
// (XYZ). Corner follows gSender's numbering: 0 BL, 1 TL, 2 TR, 3 BR.
uint8_t settings_probe_type();
void    settings_set_probe_type(uint8_t t);
uint8_t settings_probe_corner();
void    settings_set_probe_corner(uint8_t c);

// Plain persisted numbers for pages that own a small table of them (the probe
// page's plate settings). Read straight from NVS, so call at build time, not
// per frame; the key is the caller's (NVS keys max 15 chars).
float settings_get_float(const char* key, float def);
void  settings_set_float(const char* key, float v);
