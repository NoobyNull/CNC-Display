#include "model/motors.h"
#include "model/caps.h"
#include "model/settings.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

void send_line(const char* s, int timeout);

static bool     s_released = false;
static uint16_t s_disarmed = 0;   // the motors this panel turned hard limits off on

// Persisted so a panel that reboots mid-release still re-arms the controller,
// whose dump would by then report those limits as false.
static const char* const KEY = "hl_off";

static void set_limits(uint16_t mask, bool on) {
    for (int b = 0; b < 12; b++) {
        if (!(mask & (1u << b))) continue;
        char l[64];
        snprintf(l, sizeof(l), "$/axes/%c/motor%d/hard_limits=%s", "xyzabc"[b / 2], b % 2,
                 on ? "true" : "false");
        send_line(l, 0);
    }
}

bool motors_released() { return s_released; }

void motors_release() {
    if (s_released) return;
    s_released = true;
    s_disarmed = caps_hard_limit_mask();
    settings_set_float(KEY, (float)s_disarmed);
    send_line("$MD", 0);
    set_limits(s_disarmed, false);
}

void motors_hold() {
    s_released = false;            // first: the lines below pass through the guard
    set_limits(s_disarmed, true);
    s_disarmed = 0;
    settings_set_float(KEY, 0.0f);
    send_line("$ME", 0);
}

void motors_resync() { if (s_released) motors_hold(); }

void motors_on_link() {
    const uint16_t left = (uint16_t)settings_get_float(KEY, 0.0f);
    if (!left || s_released) return;
    s_disarmed = left;
    motors_hold();
}

static bool prefix(const char* l, const char* p) {
    for (; *p; l++, p++)
        if (toupper((unsigned char)*l) != toupper((unsigned char)*p)) return false;
    return true;
}

bool motors_line_can_move(const char* l) {
    while (*l == ' ') l++;
    if (!*l) return false;
    if (*l != '$') return true;    // G-code
    static const char* const MOVERS[] = {
        "$J=", "$H", "$RM", "$ME", "$SD/Run", "$LocalFS/Run", "$Macros/Run", "$Run",
    };
    for (const char* m : MOVERS)
        if (prefix(l, m)) return true;
    return false;
}

void motors_guard(const char* line) {
    if (s_released && line && motors_line_can_move(line)) motors_hold();
}
