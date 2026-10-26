#pragma once
#include <stdint.h>
#include <stddef.h>

// What this machine can actually DO, read from the controller instead of
// assumed. A pendant that offers homing to a machine with no endstops is
// offering an error:5; a probe screen on a machine with /probe/pin NO_PIN is a
// button that cannot work. Both are worse than a greyed-out control that says
// why.
//
// Two sources, because FluidNC v4 splits them:
//
//   $S   the Grbl-compat settings. Carries HomingCycleEnable, the limit
//        enables, and the per-axis rate/accel/steps/travel numbers. NOTE this
//        is NOT the v3 behaviour: v3 exposed the whole config tree here (which
//        is why the old /probe/pin lookup in page_diag.cpp never matched on
//        v4.0.3). Verified against the live Corgi on 2026-08-25.
//
//   $CD  the config dump, i.e. config.yaml. The only place pins live on v4:
//        probe, toolsetter, coolant, and the control inputs.
//
// Anything never reported stays CAP_UNKNOWN and the UI leaves it ENABLED. A
// pendant that hides a working feature because a query failed is worse than one
// that offers a feature and surfaces the controller's error.

enum cap_state_t : uint8_t {
    CAP_UNKNOWN = 0,   // not reported yet — treat as available
    CAP_ABSENT,        // reported, and this machine does not have it
    CAP_PRESENT,       // reported, configured
};

// FluidNC v4 defines Macro0..Macro3.
#define CAPS_MACRO_MAX 4
#define CAPS_MACRO_NAME_MAX 24

void caps_reset();

// Sends $CD. The reply is consumed by caps_feed_line(), not the console.
void caps_request_config();

// Consumes the $CD stream. Returns true when the line belonged to the config
// dump, so the caller keeps it out of the console log.
bool caps_feed_line(const char* line);

// Recompute the $S-derived half. Call after writing a setting.
void caps_refresh_from_settings();

// Cheap per-tick check: refreshes only when the settings table has grown, so
// it can be called from the UI tick without stealing fncset's dirty flag from
// the Diag page (both need to observe the same dump).
void caps_poll();

// Bumped whenever anything here changes. Several pages and the nav rail all
// need to react to the same update, so this is a counter each of them compares
// against its own last-seen value — not a take-once dirty flag, which only the
// first caller of the tick would ever see.
uint32_t caps_generation();

// True once a $CD dump has completed or been given up on. The boot splash
// waits on this so the page set is decided before the UI is shown.
bool caps_settled();

// Non-empty macro slots only, in controller order. count is 0..CAPS_MACRO_MAX.
size_t      caps_macro_count();
const char* caps_macro_name(size_t i);   // the key, e.g. "Macro0"
const char* caps_macro_body(size_t i);   // the g-code, for the button subtitle
// The slot number to run it by: $RM=<n>. FluidNC 4.0.x reads only the first
// character of $RM's argument as a digit and does not range-check it, so
// "$RM=Macro1" indexes slot 29 of a 4-slot table. -1 if the name has none.
int         caps_macro_num(size_t i);
// The macro that applies a measured tool length: G43.1 Z with a computed value
// (#var or [expr]); G43.1Z0 only clears one. -1 if none. Judged on the full
// body, which caps_macro_body() truncates. Index into the macro list above.
int         caps_tlo_macro();
// Motors with hard_limits: true, bit axis*2+motor (axis 0 = X). From the
// running config, so a mask read while the panel has them disarmed reads 0.
uint16_t    caps_hard_limit_mask();

cap_state_t caps_homing();
cap_state_t caps_probe();
cap_state_t caps_toolsetter();
cap_state_t caps_flood();
cap_state_t caps_mist();
cap_state_t caps_hard_limits();
cap_state_t caps_soft_limits();
cap_state_t caps_safety_door();
cap_state_t caps_estop();

// True when the control should be live: present, or not yet known.
inline bool cap_ok(cap_state_t c) { return c != CAP_ABSENT; }

// Short reason for a disabled control, e.g. "no endstops configured".
// Returns nullptr when the capability is fine.
const char* caps_why(cap_state_t c, const char* what);

// --- per-axis motion settings, straight out of $S --------------------------
// The panel had no way to change a feed rate or an acceleration; these are the
// values behind the Motion page.

#define CAPS_MAX_AXES 6

int   caps_axis_count();
char  caps_axis_letter(int i);
float caps_axis_steps(int i);    // steps/mm      $Grbl/Resolution/<A>
float caps_axis_rate(int i);     // mm/min        $Grbl/MaxRate/<A>
float caps_axis_accel(int i);    // mm/sec^2      $Grbl/Acceleration/<A>
float caps_axis_travel(int i);   // mm            $Grbl/MaxTravel/<A>

// The $S setting name for an axis field, e.g. ("MaxRate", 0) -> "Grbl/MaxRate/X".
// Used to write the value back with fncset_write().
enum caps_field_t { CAPS_STEPS = 0, CAPS_RATE, CAPS_ACCEL, CAPS_TRAVEL, CAPS_FIELD_COUNT };
const char* caps_field_name(caps_field_t f);
const char* caps_setting_for(caps_field_t f, int axis);
float       caps_axis_value(caps_field_t f, int axis);

// ok / error:N from the controller, which the parser consumes itself: ends a
// config dump in progress.
void caps_reply_end();

// What the running config uses ESP32 gpio.<n> for ("x/motor0/limit_neg (pu low)"),
// from the $CD dump; nullptr when the config does not use it.
const char* caps_gpio_role(int gpio);
