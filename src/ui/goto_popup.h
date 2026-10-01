#pragma once
#include <lvgl.h>

// Tap an axis in the DRO to type an absolute target and jog there. Exists
// because the step selector stops at 10mm: a 400mm traverse was forty taps.
void goto_open(int axis);

// Same keypad, used to type a plain number instead of a target. Exists so the
// jog step selector does not grow a second numeric-entry idiom that would drift
// from this one. cb receives the typed value; cancelling calls nothing.
void numpad_open(const char* title, const char* note, float initial,
                 void (*cb)(float));
// Text prefill, for values a float would round (Diag settings). During cb,
// numpad_text() is exactly what was typed.
void numpad_open(const char* title, const char* note, const char* initial,
                 void (*cb)(float));
const char* numpad_text();
void goto_close();
bool goto_is_open();
