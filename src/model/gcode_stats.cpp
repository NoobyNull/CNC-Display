#include "model/gcode_path.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

#ifndef UNIT_TEST
#  include <Arduino.h>
#  define PATH_ALLOC(n) ((gpoint_t*)ps_malloc((n) * sizeof(gpoint_t)))
#else
#  define PATH_ALLOC(n) ((gpoint_t*)malloc((n) * sizeof(gpoint_t)))
#endif

static gpoint_t* s_pts   = nullptr;
static size_t    s_cap   = 0;
static size_t    s_count = 0;

static float s_x, s_y, s_z;   // current position, always mm
static bool  s_absolute;      // G90 true / G91 false
static float s_unit;          // 1.0 mm, 25.4 inches
static int   s_motion;        // 0,1,2,3 = G0,G1,G2,G3; -1 = none yet
static int   s_plane;         // 17,18,19
static gextents_t s_ext;
static bool  s_init = false;

static const size_t DEFAULT_CAP = 4096;

size_t gpath_capacity() { return s_cap; }

bool gpath_reserve(size_t points) {
    if (points <= s_cap) return true;
    gpoint_t* n = PATH_ALLOC(points);
    if (!n) return false;
    if (s_pts) {
        memcpy(n, s_pts, s_count * sizeof(gpoint_t));
        free(s_pts);
    }
    s_pts = n;
    s_cap = points;
    return true;
}

void gpath_reset() {
    s_count    = 0;
    s_x = s_y = s_z = 0.0f;
    s_absolute = true;
    s_unit     = 1.0f;
    s_motion   = -1;
    s_plane    = 17;
    s_ext      = { 1e9f, -1e9f, 1e9f, -1e9f, 1e9f, -1e9f };
    s_init     = true;
    if (!s_pts) gpath_reserve(DEFAULT_CAP);
}

static void push(float x, float y, float z, bool rapid) {
    if (s_count >= s_cap && !gpath_reserve(s_cap ? s_cap * 2 : DEFAULT_CAP)) return;
    s_pts[s_count].x     = x;
    s_pts[s_count].y     = y;
    s_pts[s_count].z     = z;
    s_pts[s_count].rapid = rapid;
    s_count++;
    if (x < s_ext.min_x) s_ext.min_x = x;
    if (x > s_ext.max_x) s_ext.max_x = x;
    if (y < s_ext.min_y) s_ext.min_y = y;
    if (y > s_ext.max_y) s_ext.max_y = y;
    if (z < s_ext.min_z) s_ext.min_z = z;
    if (z > s_ext.max_z) s_ext.max_z = z;
}

// Find a word letter and parse its number. False if absent or malformed
// ("X" with no digits, or "Xabc").
static bool word(const char* s, char letter, float* out) {
    for (const char* p = s; *p; ++p) {
        if (toupper((unsigned char)*p) != letter) continue;
        const char* nptr = p + 1;
        char* end = nullptr;
        float v = strtof(nptr, &end);
        if (end == nptr) return false;
        *out = v;
        return true;
    }
    return false;
}

static void arc(float ex, float ey, float i, float j, bool cw) {
    float cx = s_x + i, cy = s_y + j;
    float r  = sqrtf(i * i + j * j);
    if (r < 1e-6f) { push(ex, ey, s_z, false); return; }

    float a0 = atan2f(s_y - cy, s_x - cx);
    float a1 = atan2f(ey  - cy, ex  - cx);
    float sweep = a1 - a0;
    if (cw) { while (sweep >= 0.0f) sweep -= 2.0f * (float)M_PI; }
    else    { while (sweep <= 0.0f) sweep += 2.0f * (float)M_PI; }

    // Chord deviation ~0.2mm sets the step angle. Clamped so a tiny arc does not
    // explode into segments and a huge one does not look polygonal.
    float arg  = 1.0f - 0.2f / r;
    if (arg < -1.0f) arg = -1.0f;
    if (arg >  1.0f) arg =  1.0f;
    float step = 2.0f * acosf(arg);
    if (step < 1e-3f) step = 1e-3f;

    int n = (int)ceilf(fabsf(sweep) / step);
    if (n < 4)  n = 4;
    if (n > 64) n = 64;

    for (int k = 1; k <= n; k++) {
        float a = a0 + sweep * ((float)k / (float)n);
        push(cx + r * cosf(a), cy + r * sinf(a), s_z, false);
    }
}

void gpath_feed_line(const char* line) {
    if (!line || !*line) return;
    if (!s_init) gpath_reset();

    // Strip comments: ( ... ) and ; to end of line
    char buf[256];
    size_t o = 0;
    int paren = 0;
    for (const char* p = line; *p && o < sizeof(buf) - 1; ++p) {
        if (*p == '(') { paren++; continue; }
        if (*p == ')') { if (paren) paren--; continue; }
        if (paren) continue;
        if (*p == ';') break;
        buf[o++] = *p;
    }
    buf[o] = 0;
    if (o == 0) return;

    // Modal words. Several G words per line are legal ("G21 G90 G1").
    for (const char* p = buf; *p; ++p) {
        if (toupper((unsigned char)*p) != 'G') continue;
        char* end = nullptr;
        long g = strtol(p + 1, &end, 10);
        if (end == p + 1) continue;
        switch (g) {
            case 0: case 1: case 2: case 3: s_motion = (int)g; break;
            case 17: case 18: case 19:      s_plane  = (int)g; break;
            case 20: s_unit = 25.4f;   break;
            case 21: s_unit = 1.0f;    break;
            case 90: s_absolute = true;  break;
            case 91: s_absolute = false; break;
            default: break;
        }
    }

    float vx = 0, vy = 0, vz = 0;
    bool hx = word(buf, 'X', &vx);
    bool hy = word(buf, 'Y', &vy);
    bool hz = word(buf, 'Z', &vz);
    if (!hx && !hy && !hz) return;   // no motion on this line
    if (s_motion < 0)      return;   // no modal motion established yet

    float nx = s_x, ny = s_y, nz = s_z;
    if (hx) nx = s_absolute ? vx * s_unit : s_x + vx * s_unit;
    if (hy) ny = s_absolute ? vy * s_unit : s_y + vy * s_unit;
    if (hz) nz = s_absolute ? vz * s_unit : s_z + vz * s_unit;

    if (s_motion == 2 || s_motion == 3) {
        float vi = 0.0f, vj = 0.0f;
        word(buf, 'I', &vi);
        word(buf, 'J', &vj);
        s_z = nz;
        arc(nx, ny, vi * s_unit, vj * s_unit, s_motion == 2);
    } else {
        push(nx, ny, nz, s_motion == 0);
    }
    s_x = nx; s_y = ny; s_z = nz;
}

size_t gpath_count() { return s_count; }

gpoint_t gpath_point(size_t i) {
    if (i < s_count) return s_pts[i];
    gpoint_t z = { 0.0f, 0.0f, 0.0f, false };
    return z;
}

gextents_t gpath_extents() {
    if (s_count == 0) {
        gextents_t z = { 0, 0, 0, 0, 0, 0 };
        return z;
    }
    return s_ext;
}
