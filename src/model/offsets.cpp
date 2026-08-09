#include "model/offsets.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// FluidNC answers $# with lines like:
//   [G54:-100.000,-200.000,-50.000]
//   [TLO:3.175]
//   [PRB:0.000,0.000,-12.500:1]

static const char* ROW_NAMES[WCS_ROW_COUNT] = {
    "G54", "G55", "G56", "G57", "G58", "G59", "G28", "G30", "G92", "TLO", "PRB"
};

static wcs_t s_rows[WCS_ROW_COUNT];
static bool  s_dirty = false;
static char  s_active[8] = "G54";

void offsets_reset() {
    for (int i = 0; i < WCS_ROW_COUNT; i++) {
        memset(&s_rows[i], 0, sizeof(wcs_t));
        strncpy(s_rows[i].name, ROW_NAMES[i], sizeof(s_rows[i].name) - 1);
        s_rows[i].valid = false;
    }
    s_dirty = true;
}

static wcs_t* find(const char* name, size_t len) {
    for (int i = 0; i < WCS_ROW_COUNT; i++) {
        if (strlen(s_rows[i].name) == len && strncmp(s_rows[i].name, name, len) == 0) {
            return &s_rows[i];
        }
    }
    return nullptr;
}

const char* offsets_active() { return s_active; }

void offsets_feed_line(const char* line) {
    if (!line || line[0] != '[') return;

    // [GC:G0 G54 G17 G21 G90 ...] carries the active work coordinate system.
    if (strncmp(line, "[GC:", 4) == 0) {
        for (int g = 54; g <= 59; g++) {
            char pat[6];
            snprintf(pat, sizeof(pat), "G%d", g);
            const char* hit = strstr(line, pat);
            // Must be a whole word: "G5" inside "G54" would match otherwise.
            if (hit && (hit[3] == ' ' || hit[3] == ']')) {
                if (strcmp(s_active, pat) != 0) {
                    strncpy(s_active, pat, sizeof(s_active) - 1);
                    s_dirty = true;
                }
                break;
            }
        }
        return;
    }
    const char* colon = strchr(line, ':');
    if (!colon || colon == line + 1) return;

    wcs_t* row = find(line + 1, (size_t)(colon - line - 1));
    if (!row) return;

    float vals[WCS_MAX_AXES] = { 0, 0, 0, 0, 0, 0 };
    int   n = 0;
    const char* p = colon + 1;
    while (n < WCS_MAX_AXES && *p && *p != ']') {
        char* end = nullptr;
        float v = strtof(p, &end);
        if (end == p) return;              // malformed: keep whatever we had
        vals[n++] = v;
        p = end;
        if (*p == ',') { p++; continue; }
        break;
    }
    if (n == 0) return;

    if (n == 1 && strcmp(row->name, "TLO") == 0) {
        // TLO is a single value and it is the Z offset.
        row->axis[0] = 0.0f;
        row->axis[1] = 0.0f;
        row->axis[2] = vals[0];
        for (int i = 3; i < WCS_MAX_AXES; i++) row->axis[i] = 0.0f;
    } else {
        for (int i = 0; i < WCS_MAX_AXES; i++) row->axis[i] = (i < n) ? vals[i] : 0.0f;
    }
    row->valid = true;
    s_dirty = true;
}

const wcs_t* offsets_get(const char* name) {
    if (!name) return nullptr;
    return find(name, strlen(name));
}

const wcs_t* offsets_row(int i) {
    return (i >= 0 && i < WCS_ROW_COUNT) ? &s_rows[i] : nullptr;
}

bool offsets_dirty_take() {
    bool d = s_dirty;
    s_dirty = false;
    return d;
}
