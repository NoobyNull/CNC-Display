#pragma once
#include <lvgl.h>

enum expanded_face_t { EXPANDED_STATS, EXPANDED_CONSOLE };

void expanded_build();
void expanded_open(expanded_face_t face);
void expanded_close();
void expanded_flip();
bool expanded_is_open();
void expanded_tick();

// Refill the stats face after a file scan.
void stats_refresh();

// The few lines the Run page shows for the scanned file.
void stats_summary(char* out, size_t n);
