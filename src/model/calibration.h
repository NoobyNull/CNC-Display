#pragma once
#include <stdint.h>

// Steps-per-mm calibration: command a known move, measure what the machine
// actually did, and scale the axis setting by the error.
//
// Free of LVGL and hardware so the arithmetic and the reply parsing can be
// tested on the host. This is a number that decides whether every cut is the
// size it says it is, so it gets a test rather than a careful read.

#define CAL_AXES 4   // X Y Z A

// Forget any captured values. Call before asking the controller again.
void cal_reset();

// Feed controller reply lines. Captures "$/axes/<x>/steps_per_mm=<value>",
// which is what FluidNC echoes for a runtime setting query
// (Configuration/RuntimeSetting.cpp: setting_prefix()). Ignores everything else.
void cal_feed_line(const char* line);

bool  cal_have_steps(int axis);
float cal_steps(int axis);

// new = old * commanded / measured.
//
// Returns 0 when the inputs cannot give a meaningful answer: a measured
// distance of zero or less, a commanded distance of zero or less, or a result
// outside the range FluidNC itself accepts for steps_per_mm (Machine/Axis.cpp:17
// bounds it to 0.001 .. 100000). A rejected value has to read as rejected
// rather than silently becoming something plausible.
float cal_new_steps(float old_steps, float commanded_mm, float measured_mm);
