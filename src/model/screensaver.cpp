#include "model/screensaver.h"

static bool s_off = false;
static bool s_eat = false;

void screensaver_reset() {
    s_off = false;
    s_eat = false;
}

bool screensaver_is_off() { return s_off; }

void screensaver_update(uint32_t inactive_ms, uint8_t timeout_min, bool machine_busy) {
    // A machine that is moving, holding or alarming keeps its own screen lit,
    // and going busy wakes it immediately rather than merely holding off the
    // countdown: an alarm raised while the panel is dark has to be visible
    // without someone having to find the panel and tap it first.
    if (timeout_min == 0 || machine_busy) {
        s_off = false;
        return;
    }
    if (inactive_ms >= (uint32_t)timeout_min * 60000UL) s_off = true;
}

bool screensaver_touch_allowed(bool touch_down) {
    if (s_off) {
        // The press that wakes the panel is consumed here, and stays consumed
        // until the finger lifts. Without the latch a press held across the
        // wake would land on whatever is under it once the backlight returns.
        if (touch_down) {
            s_off = false;
            s_eat = true;
        }
        return false;
    }
    if (s_eat) {
        if (touch_down) return false;
        s_eat = false;
    }
    return true;
}
