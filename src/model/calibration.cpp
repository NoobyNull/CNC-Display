#include "model/calibration.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static float s_steps[CAL_AXES];
static bool  s_have[CAL_AXES];

static const char AXIS_CH[CAL_AXES] = { 'x', 'y', 'z', 'a' };

// FluidNC's own bounds for the setting (Machine/Axis.cpp:17).
static const float STEPS_MIN = 0.001f;
static const float STEPS_MAX = 100000.0f;

void cal_reset() {
    for (int i = 0; i < CAL_AXES; i++) {
        s_steps[i] = 0.0f;
        s_have[i]  = false;
    }
}

bool  cal_have_steps(int axis) { return axis >= 0 && axis < CAL_AXES && s_have[axis]; }
float cal_steps(int axis)      { return axis >= 0 && axis < CAL_AXES ? s_steps[axis] : 0.0f; }

// Matches "$/axes/<axis>/steps_per_mm=<value>". The leading slash is optional
// because it is stripped at one end of FluidNC and added at the other, and a
// parser that only accepts one spelling breaks on the wrong day.
void cal_feed_line(const char* line) {
    if (!line || line[0] != '$') return;
    const char* p = line + 1;
    if (*p == '/') p++;

    static const char PRE[] = "axes/";
    if (strncasecmp(p, PRE, sizeof(PRE) - 1) != 0) return;
    p += sizeof(PRE) - 1;

    int axis = -1;
    for (int i = 0; i < CAL_AXES; i++) {
        if (tolower((unsigned char)*p) == AXIS_CH[i]) { axis = i; break; }
    }
    if (axis < 0) return;
    p++;

    static const char KEY[] = "/steps_per_mm";
    if (strncasecmp(p, KEY, sizeof(KEY) - 1) != 0) return;
    p += sizeof(KEY) - 1;

    if (*p != '=') return;
    p++;

    char* end = nullptr;
    float v = strtof(p, &end);
    if (end == p || v <= 0.0f) return;

    s_steps[axis] = v;
    s_have[axis]  = true;
}

float cal_new_steps(float old_steps, float commanded_mm, float measured_mm) {
    if (old_steps <= 0.0f)    return 0.0f;
    if (commanded_mm <= 0.0f) return 0.0f;
    if (measured_mm <= 0.0f)  return 0.0f;

    const float v = old_steps * (commanded_mm / measured_mm);
    if (v < STEPS_MIN || v > STEPS_MAX) return 0.0f;
    return v;
}
