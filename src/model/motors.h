#pragma once
#include <stdint.h>

// STEPPERS OFF frees the gantry to be pushed by hand. FluidNC still treats a
// limit switch touched in Idle as a hard-limit crash: alarm, reset, and with
// idle_ms 255 the steppers come back on under the operator's hands while the
// panel still says OFF.
//
// So releasing also disarms hard limits ($/axes/<a>/motor<n>/hard_limits),
// and the panel puts them back before anything that could move the machine.
// FluidNC reads that flag live (LimitPin::isHard), so no restart is needed.
//
// Ceiling: motion started from somewhere else (web UI, pendant) while released
// runs unguarded until the panel sees it and re-arms.

bool motors_released();
void motors_release();   // $MD, then disarm
void motors_hold();      // re-arm, then $ME

// Called from send_line()/send_jog_line() with every outgoing line.
void motors_guard(const char* line);
bool motors_line_can_move(const char* line);

// The controller may have turned the steppers back on behind the panel's back
// (reset, alarm, relink, or the panel itself rebooted while released): put
// both sides back to held + armed.
void motors_resync();
// Once per link-up: restores limits a previous panel boot left disarmed.
void motors_on_link();
