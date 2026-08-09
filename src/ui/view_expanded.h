#pragma once
#include <lvgl.h>

enum expanded_face_t { EXPANDED_TOOLPATH, EXPANDED_CONSOLE };

void expanded_build();
void expanded_open(expanded_face_t face);
void expanded_close();
void expanded_flip();
bool expanded_is_open();
void expanded_tick();

// Move the live position marker. Driven from the DRO tick, so it costs no extra
// traffic on the wire during a cut.
void toolpath_set_marker(float x, float y);

// Redraw the whole path after a load.
void toolpath_redraw();
