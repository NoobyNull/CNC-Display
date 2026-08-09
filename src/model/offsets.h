#pragma once
#include <stdint.h>

#define WCS_MAX_AXES 6

struct wcs_t {
    char  name[8];
    float axis[WCS_MAX_AXES];
    bool  valid;
};

// Rows in display order: G54..G59, G28, G30, G92, TLO, PRB
#define WCS_ROW_COUNT 11

void         offsets_reset();
void         offsets_feed_line(const char* line);
const wcs_t* offsets_get(const char* name);
const wcs_t* offsets_row(int i);      // i in [0, WCS_ROW_COUNT)
bool         offsets_dirty_take();    // true once after any change
