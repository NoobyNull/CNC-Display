#pragma once
#include <lvgl.h>

// Tap an axis in the DRO to type an absolute target and jog there. Exists
// because the step selector stops at 10mm: a 400mm traverse was forty taps.
void goto_open(int axis);
void goto_close();
bool goto_is_open();
