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
