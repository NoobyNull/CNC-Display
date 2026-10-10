#pragma once
#include <stdint.h>

// Whether machine coordinates can be trusted, i.e. the machine has homed since
// the last thing that could have moved it unseen.
//
// FluidNC does not report this (must_home: false boots straight to Idle), so
// the panel keeps it: unknown at boot, true once a homing cycle ends anywhere
// but Alarm, false again on anything that loses position. Conservative on
// purpose — a needless "please home" costs a tap; a trusted wrong origin costs
// a crash.

bool     homed();
// Bumps on every loss, so a dismissed prompt comes back for the next one.
uint32_t homed_epoch();

// Leaving Homing for any state but Alarm is a completed cycle. Homing failures
// (alarms 6-9) and reset-mid-home both land in Alarm.
void homed_note_state(bool was_homing, bool now_alarm, bool now_disconnected);
// Alarms that mean the steps were lost or never counted.
void homed_note_alarm(int code);
// Controller rebooted under a live link.
void homed_forget();
