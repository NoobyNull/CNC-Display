#include "model/gcode_stats.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <stdio.h>

static gstats_t s;

static float s_x, s_y, s_z;   // current position, always mm
static bool  s_absolute;      // G90 true / G91 false
static float s_unit;          // 1.0 mm, 25.4 inches
static int   s_motion;        // 0,1,2,3 = G0,G1,G2,G3; -1 = none yet
static float s_feed;          // mm/min, 0 = not set
static int   s_pending_tool;  // last T word, -1 = none
static char  s_last_comment[40];
static int   s_comment_age;   // lines since s_last_comment
static int   s_just_tool;     // tools[] index added on the previous line, -1 none
static bool  s_init = false;

void gstats_reset() {
    memset(&s, 0, sizeof(s));
    s.min_x = s.min_y = s.min_z = 1e9f;
    s.max_x = s.max_y = s.max_z = -1e9f;
    s.top_z = -1e9f;
    s.feed_min = 1e9f;
    s_x = s_y = s_z = 0.0f;
    s_absolute = true;
    s_unit     = 1.0f;
    s_motion   = -1;
    s_feed     = 0.0f;
    s_pending_tool = -1;
    s_last_comment[0] = 0;
    s_comment_age = 99;
    s_just_tool = -1;
    s_init = true;
}

const gstats_t& gstats() {
    if (!s_init) gstats_reset();
    return s;
}

static void cut_point(float x, float y, float z) {
    if (x < s.min_x) s.min_x = x;
    if (x > s.max_x) s.max_x = x;
    if (y < s.min_y) s.min_y = y;
    if (y > s.max_y) s.max_y = y;
    if (z < s.min_z) s.min_z = z;
    if (z > s.max_z) s.max_z = z;
    s.have_cut = true;
}

// Find a word letter and parse its number. False if absent or malformed.
static bool word(const char* p0, char letter, float* out) {
    for (const char* p = p0; *p; ++p) {
        if (toupper((unsigned char)*p) != letter) continue;
        char* end = nullptr;
        float v = strtof(p + 1, &end);
        if (end == p + 1) return false;
        *out = v;
        return true;
    }
    return false;
}

static bool has_m(const char* p0, int m) {
    for (const char* p = p0; *p; ++p) {
        if (toupper((unsigned char)*p) != 'M') continue;
        char* end = nullptr;
        long v = strtol(p + 1, &end, 10);
        if (end != p + 1 && v == m) return true;
    }
    return false;
}

// Trim spaces and the box-drawing VCarve puts round its header ("( | X= ...").
static void tidy(char* t) {
    char* a = t;
    while (*a == ' ' || *a == '|' || *a == '\t') a++;
    memmove(t, a, strlen(a) + 1);
    size_t n = strlen(t);
    while (n && (t[n - 1] == ' ' || t[n - 1] == '|' || t[n - 1] == '\t')) t[--n] = 0;
}

static bool is_rule(const char* t) {   // "-------" / "=======" separators
    if (!*t) return true;
    for (; *t; ++t) if (*t != '-' && *t != '=' && *t != '*' && *t != ' ') return false;
    return true;
}

static void add_note(const char* t) {
    if (s.n_notes >= GSTATS_NOTES || is_rule(t)) return;
    snprintf(s.notes[s.n_notes++], GSTATS_NOTE_LEN, "%s", t);
}

static int add_tool(int num, const char* desc) {
    for (int i = 0; i < s.n_tools; i++)
        if (s.tools[i].num == num) {
            if (!s.tools[i].desc[0] && desc) snprintf(s.tools[i].desc, sizeof(s.tools[i].desc), "%s", desc);
            return i;
        }
    if (s.n_tools >= GSTATS_TOOLS) return -1;
    s.tools[s.n_tools].num = num;
    snprintf(s.tools[s.n_tools].desc, sizeof(s.tools[s.n_tools].desc), "%s", desc ? desc : "");
    return s.n_tools++;
}

// Arc length and extents. Sampled rather than solved: the samples also catch
// the arc bulging past its endpoints, which is what the extents need.
static float arc(float ex, float ey, float i, float j, bool cw) {
    float cx = s_x + i, cy = s_y + j;
    float r  = sqrtf(i * i + j * j);
    if (r < 1e-6f) { cut_point(ex, ey, s_z); return hypotf(ex - s_x, ey - s_y); }
    float a0 = atan2f(s_y - cy, s_x - cx);
    float a1 = atan2f(ey  - cy, ex  - cx);
    float sweep = a1 - a0;
    if (cw) { while (sweep >= 0.0f) sweep -= 2.0f * (float)M_PI; }
    else    { while (sweep <= 0.0f) sweep += 2.0f * (float)M_PI; }
    const int n = 16;   // ponytail: fixed samples; extents off by <2% of r at worst
    for (int k = 0; k <= n; k++) {
        float a = a0 + sweep * ((float)k / (float)n);
        cut_point(cx + r * cosf(a), cy + r * sinf(a), s_z);
    }
    return r * fabsf(sweep);
}

void gstats_feed_line(const char* line) {
    if (!line) return;
    if (!s_init) gstats_reset();
    s.lines++;
    s_comment_age++;

    // Split code from comment: ( ... ) and ; to end of line.
    char code[256], cmt[GSTATS_NOTE_LEN];
    size_t o = 0, c = 0;
    int paren = 0;
    bool semi = false;
    for (const char* p = line; *p; ++p) {
        if (semi) { if (c < sizeof(cmt) - 1) cmt[c++] = *p; continue; }
        if (*p == '(') { if (paren++ && c < sizeof(cmt) - 1) cmt[c++] = *p; continue; }
        if (*p == ')') { if (paren && --paren && c < sizeof(cmt) - 1) cmt[c++] = *p; continue; }
        if (paren) { if (c < sizeof(cmt) - 1) cmt[c++] = *p; continue; }
        if (*p == ';') { semi = true; continue; }
        if (o < sizeof(code) - 1) code[o++] = *p;
    }
    while (o && isspace((unsigned char)code[o - 1])) o--;
    code[o] = 0;
    { size_t k = 0; while (k < o && isspace((unsigned char)code[k])) k++; if (k == o) o = 0; }
    cmt[c] = 0;
    tidy(cmt);
    // VCarve names the tool on a comment-only line just after the M6.
    const int just = s_just_tool;
    s_just_tool = -1;
    if (just >= 0 && o == 0 && cmt[0] && !is_rule(cmt))
        snprintf(s.tools[just].desc, sizeof(s.tools[just].desc), "%s", cmt);

    if (cmt[0]) {
        if (s.moves == 0) add_note(cmt);
        if (!is_rule(cmt)) {
            snprintf(s_last_comment, sizeof(s_last_comment), "%s", cmt);
            s_comment_age = 0;
        }
    }
    if (o == 0) return;

    // Modal words. Several G words per line are legal ("G21 G90 G1").
    // Read as a decimal: G91.1 (arc IJ incremental, VCarve emits it in every
    // header) is not G91, and taking it for G91 turned every move after it
    // relative.
    for (const char* p = code; *p; ++p) {
        if (toupper((unsigned char)*p) != 'G') continue;
        char* end = nullptr;
        float gf = strtof(p + 1, &end);
        if (end == p + 1) continue;
        const int g = (int)gf;
        if (gf != (float)g) continue;   // G91.1, G43.1, G38.2 ... none are modal state here
        switch (g) {
            case 0: case 1: case 2: case 3: s_motion = (int)g; break;
            case 20: s_unit = 25.4f; s.inches = true; break;
            case 21: s_unit = 1.0f;  break;
            case 90: s_absolute = true;  break;
            case 91: s_absolute = false; break;
            default: break;
        }
    }

    float v;
    if (word(code, 'F', &v) && v > 0.0f) {
        s_feed = v * s_unit;
        if (s_feed < s.feed_min) s.feed_min = s_feed;
        if (s_feed > s.feed_max) s.feed_max = s_feed;
    }
    if (word(code, 'S', &v) && v > s.spindle_max) s.spindle_max = v;

    // Tools: T names the next tool, M6 changes to it (often the same line).
    // The description is a comment on that line or the line above (Fusion),
    // or a comment-only line just below (VCarve, handled above). Nothing
    // further away: VCarve's header tool list sits a few lines up and would
    // otherwise name T1 after whichever tool the header listed last.
    if (word(code, 'T', &v)) s_pending_tool = (int)v;
    if (has_m(code, 6)) {
        s.tool_changes++;
        if (s_pending_tool >= 0)
            s_just_tool = add_tool(s_pending_tool, s_comment_age <= 1 && s_last_comment[0] ? s_last_comment : nullptr);
    }

    float vx = 0, vy = 0, vz = 0;
    bool hx = word(code, 'X', &vx);
    bool hy = word(code, 'Y', &vy);
    bool hz = word(code, 'Z', &vz);
    if (!hx && !hy && !hz) return;   // no motion on this line
    if (s_motion < 0)      return;   // no modal motion established yet

    float nx = s_x, ny = s_y, nz = s_z;
    if (hx) nx = s_absolute ? vx * s_unit : s_x + vx * s_unit;
    if (hy) ny = s_absolute ? vy * s_unit : s_y + vy * s_unit;
    if (hz) nz = s_absolute ? vz * s_unit : s_z + vz * s_unit;
    s.moves++;
    if (nz > s.top_z) s.top_z = nz;

    float len;
    if (s_motion == 0) {
        len = sqrtf((nx - s_x) * (nx - s_x) + (ny - s_y) * (ny - s_y) + (nz - s_z) * (nz - s_z));
        s.rapid_mm += len;
    } else {
        if (s_motion == 1) {
            len = sqrtf((nx - s_x) * (nx - s_x) + (ny - s_y) * (ny - s_y) + (nz - s_z) * (nz - s_z));
            cut_point(s_x, s_y, s_z);
            cut_point(nx, ny, nz);
        } else {
            float vi = 0.0f, vj = 0.0f;
            word(code, 'I', &vi);
            word(code, 'J', &vj);
            cut_point(s_x, s_y, s_z);
            s_z = nz;
            len = arc(nx, ny, vi * s_unit, vj * s_unit, s_motion == 2);
        }
        s.cut_mm += len;
        if (s_feed > 0.0f) s.cut_sec += len / s_feed * 60.0f;
    }
    s_x = nx; s_y = ny; s_z = nz;
}
