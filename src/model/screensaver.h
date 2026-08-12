#pragma once
#include <stdint.h>

// When the panel blanks on idle, and what happens to the touch that wakes it.
//
// Deliberately free of LVGL and hardware so the swallow-the-wake-tap rule can be
// tested on the host. That rule is the safety-relevant part: the safety bar puts
// RESET on every page, so a wake tap that reached the UI would eventually hit it
// in the dark.

void screensaver_reset();

// inactive_ms: LVGL's idle time. timeout_min: 0 disables blanking entirely.
// machine_busy: anything other than the machine sitting still.
void screensaver_update(uint32_t inactive_ms, uint8_t timeout_min, bool machine_busy);

bool screensaver_is_off();

// Call once per touch poll. True when the sample should reach the UI. False
// while blanked, and for the whole press that wakes the panel.
bool screensaver_touch_allowed(bool touch_down);
