#include "model/fnc_settings.h"
#include "model/FluidNCModel.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>   // strtol; the ESP toolchain leaks it via stdio.h, gcc does not

static fnc_setting_t s_list[FS_MAX];
static size_t        s_count = 0;
static bool          s_dirty = false;

void fncset_clear() { s_count = 0; s_dirty = true; }
size_t fncset_count() { return s_count; }
const fnc_setting_t* fncset_at(size_t i) { return i < s_count ? &s_list[i] : nullptr; }

bool fncset_dirty_take() { bool d = s_dirty; s_dirty = false; return d; }

// "%XX" back to raw bytes. FluidNC encodes anything Grbl's line parser would
// misread; without decoding, a path or a pin spec comes back mangled.
static void uri_decode(const char* src, char* dst, size_t dstn) {
    size_t o = 0;
    for (const char* p = src; *p && o < dstn - 1; ++p) {
        if (*p == '%' && p[1] && p[2]) {
            char hex[3] = { p[1], p[2], 0 };
            dst[o++] = (char)strtol(hex, nullptr, 16);
            p += 2;
        } else {
            dst[o++] = *p;
        }
    }
    dst[o] = 0;
}

void fncset_feed_line(const char* line) {
    if (!line || line[0] != '$') return;
    const char* eq = strchr(line, '=');
    if (!eq) return;

    size_t nlen = (size_t)(eq - line - 1);           // skip the leading '$'
    if (nlen == 0 || nlen >= FS_NAME_MAX) return;
    if (s_count >= FS_MAX) return;

    fnc_setting_t& e = s_list[s_count];
    memcpy(e.name, line + 1, nlen);
    e.name[nlen] = 0;
    uri_decode(eq + 1, e.value, FS_VAL_MAX);

    // A repeat of a name we already hold is an update, not a new row.
    for (size_t i = 0; i < s_count; i++) {
        if (strcmp(s_list[i].name, e.name) == 0) {
            strncpy(s_list[i].value, e.value, FS_VAL_MAX - 1);
            s_list[i].value[FS_VAL_MAX - 1] = 0;
            s_dirty = true;
            return;
        }
    }
    s_count++;
    s_dirty = true;
}

void fncset_request_all()     { fncset_clear(); send_line("$S"); }
void fncset_request_changed() { fncset_clear(); send_line("$SC"); }

void fncset_write(const char* name, const char* value) {
    if (!name || !*name) return;
    send_linef("$%s=%s", name, value ? value : "");
}
