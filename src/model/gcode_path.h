#pragma once
#include <stdint.h>
#include <stddef.h>

struct gpoint_t {
    float x, y, z;
    bool  rapid;
};

struct gextents_t {
    float min_x, max_x, min_y, max_y, min_z, max_z;
};

// Reset parser state and discard all points.
void gpath_reset();

// Feed one line of G-code. Safe with comments, blanks and garbage.
void gpath_feed_line(const char* line);

size_t     gpath_count();
gpoint_t   gpath_point(size_t i);
gextents_t gpath_extents();

// Capacity actually allocated. On device this is PSRAM-backed.
size_t gpath_capacity();
bool   gpath_reserve(size_t points);
