#include "model/filelist.h"
#include "model/FluidNCModel.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// FluidNC answers $Files/ListGCode with an encapsulated JSON document listing
// {"name":"x","size":"n"} objects. Scanning for that shape directly is ~40 lines;
// wiring up the streaming parser and a listener class for one screen is not
// worth it when the reply format is fixed.

static fl_entry_t s_entries[FL_MAX_ENTRIES];
static size_t     s_count = 0;
static char       s_path[128] = "/sd";
static bool       s_dirty = false;

void filelist_reset() {
    s_count = 0;
    s_dirty = true;
}

const char* filelist_path() { return s_path; }
size_t      filelist_count() { return s_count; }

const fl_entry_t* filelist_at(size_t i) {
    return (i < s_count) ? &s_entries[i] : nullptr;
}

bool filelist_dirty_take() {
    bool d = s_dirty;
    s_dirty = false;
    return d;
}

void filelist_request() {
    filelist_reset();
    send_linef("$Files/ListGCode=%s", s_path);
}

// Pull the value of "key":"value" or "key":value starting at p.
static bool field(const char* p, const char* key, char* out, size_t outn) {
    char pat[24];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char* k = strstr(p, pat);
    if (!k) return false;
    const char* c = strchr(k, ':');
    if (!c) return false;
    c++;
    while (*c == ' ') c++;
    bool quoted = (*c == '"');
    if (quoted) c++;
    size_t n = 0;
    while (*c && n < outn - 1) {
        if (quoted ? (*c == '"') : (*c == ',' || *c == '}')) break;
        out[n++] = *c++;
    }
    out[n] = 0;
    return n > 0;
}

void filelist_feed_line(const char* line) {
    if (!line) return;
    const char* p = line;
    while ((p = strchr(p, '{')) != nullptr) {
        if (s_count >= FL_MAX_ENTRIES) break;
        char name[FL_NAME_MAX] = { 0 };
        char size[24] = { 0 };
        if (!field(p, "name", name, sizeof(name))) { p++; continue; }
        bool has_size = field(p, "size", size, sizeof(size));

        fl_entry_t& e = s_entries[s_count];
        strncpy(e.name, name, FL_NAME_MAX - 1);
        e.name[FL_NAME_MAX - 1] = 0;
        // FluidNC reports directories with a negative size.
        e.is_dir = !has_size || size[0] == '-';
        e.size   = e.is_dir ? 0u : (uint32_t)strtoul(size, nullptr, 10);
        s_count++;
        s_dirty = true;
        p++;
    }
}

void filelist_enter(const char* name) {
    if (!name) return;
    if (strcmp(name, "..") == 0) {
        char* slash = strrchr(s_path, '/');
        if (slash && slash != s_path) *slash = 0;
    } else {
        size_t l = strlen(s_path);
        snprintf(s_path + l, sizeof(s_path) - l, "/%s", name);
    }
    filelist_request();
}
