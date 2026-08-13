#include "model/screensaver.h"

static bool     s_off = false;
static bool     s_eat = false;
static uint32_t s_eat_since = 0;

// Longest a wake press may be swallowed. Comfortably past a tap, and far short
// of anything a person would call unresponsive. The cap is the whole point:
// waiting on a release that a wedged touch controller will never send is how
// this turned into dead touch until a power cycle.
static const uint32_t EAT_MAX_MS = 600;

void screensaver_reset() {
    s_off = false;
    s_eat = false;
    s_eat_since = 0;
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

bool screensaver_touch_allowed(bool touch_down, uint32_t now_ms) {
    if (s_off) {
        // The press that wakes the panel is consumed here, and stays consumed
        // until the finger lifts. Without the latch a press held across the
        // wake would land on whatever is under it once the backlight returns.
        if (touch_down) {
            s_off       = false;
            s_eat       = true;
            s_eat_since = now_ms;
        }
        return false;
    }
    if (s_eat) {
        // Released, or held past the cap. The cap belongs here rather than in
        // the caller: a swallow that can outlive the finger is a swallow that
        // can outlive the session.
        if (touch_down && (uint32_t)(now_ms - s_eat_since) < EAT_MAX_MS) return false;
        s_eat = false;
    }
    return true;
}
