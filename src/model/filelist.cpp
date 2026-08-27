#include "model/filelist.h"
// Declared rather than included: FluidNCModel.h drags in GrblParserC for one
// symbol, which puts this file out of reach of the host test runner. The reply
// parsing below is exactly the part worth testing.
void send_linef(const char* fmt, ...);
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// FluidNC answers $Files/ListGCode with a JSON document listing
// {"name":"x","size":"n"} objects. Scanning for that shape directly is still
// the right call for one screen, but it has to be done on the whole document,
// bounded per object — the first attempt did neither and got both wrong:
//
//   1. field() searched with an unbounded strstr, so the OUTER {"files":[...
//      object found the first entry's "name" and added it as a file. Every
//      listing silently duplicated its first entry, split or not.
//   2. The reply arrives split across lines, and the break lands mid-object
//      (...{"name / ":"surfacing-pass.nc"...). A line-based parse sees two
//      fragments, neither a whole object, and drops the file entirely.
//
// Together those read as "one file listed twice and another missing", with no
// error anywhere — which on a RUN page means picking the wrong file to cut.
// So: accumulate the document, then parse objects inside the array only.

static void filelist_discard_partial();

static fl_entry_t s_entries[FL_MAX_ENTRIES];
static size_t     s_count = 0;
static char       s_path[128] = "/sd";
static bool       s_dirty = false;

void filelist_reset() {
    s_count = 0;
    filelist_discard_partial();
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

// Pull "key":"value" or "key":value from within [p,end) ONLY. The bound is the
// point: without it a lookup runs on into the next object and reports its value
// as this one's.
static bool field(const char* p, const char* end, const char* key,
                  char* out, size_t outn) {
    char pat[24];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char* k = nullptr;
    for (const char* q = p; q + strlen(pat) <= end; q++)
        if (strncmp(q, pat, strlen(pat)) == 0) { k = q; break; }
    if (!k) return false;
    const char* c = k;
    while (c < end && *c != ':') c++;
    if (c >= end) return false;
    c++;
    while (c < end && *c == ' ') c++;
    bool quoted = (c < end && *c == '"');
    if (quoted) c++;
    size_t n = 0;
    while (c < end && n < outn - 1) {
        if (quoted ? (*c == '"') : (*c == ',' || *c == '}')) break;
        out[n++] = *c++;
    }
    out[n] = 0;
    return n > 0;
}

// Sized for a full directory: FL_MAX_ENTRIES names plus their size fields and
// the surrounding punctuation, with headroom. An overlong reply is dropped
// rather than half-parsed.
static char   s_doc[2048];
static size_t s_doclen = 0;

static void filelist_discard_partial() { s_doclen = 0; }

// Entries live between the array brackets. Anything outside them — the outer
// object, the trailing "path" — is not a file and must not be scanned.
static void parse_doc() {
    const char* fk = strstr(s_doc, "\"files\"");
    if (!fk) return;
    const char* p = strchr(fk, '[');
    const char* z = p ? strrchr(p, ']') : nullptr;
    if (!p || !z) return;

    s_count = 0;
    for (p++; p < z && s_count < FL_MAX_ENTRIES; ) {
        const char* ob = (const char*)memchr(p, '{', (size_t)(z - p));
        if (!ob) break;
        const char* oe = (const char*)memchr(ob, '}', (size_t)(z - ob));
        if (!oe) break;                       // truncated tail: stop, do not guess

        char name[FL_NAME_MAX] = { 0 };
        char size[24] = { 0 };
        if (field(ob, oe, "name", name, sizeof(name))) {
            bool has_size = field(ob, oe, "size", size, sizeof(size));
            fl_entry_t& e = s_entries[s_count];
            strncpy(e.name, name, FL_NAME_MAX - 1);
            e.name[FL_NAME_MAX - 1] = 0;
            // FluidNC reports directories with a negative size.
            e.is_dir = !has_size || size[0] == '-';
            e.size   = e.is_dir ? 0u : (uint32_t)strtoul(size, nullptr, 10);
            s_count++;
        }
        p = oe + 1;
    }
    s_dirty = true;
}

void filelist_feed_line(const char* line) {
    if (!line) return;

    // Start collecting at the document head; ignore everything else, so status
    // reports arriving mid-listing cannot corrupt it.
    if (!s_doclen && !strstr(line, "\"files\"")) return;

    const size_t n = strlen(line);
    if (s_doclen + n >= sizeof(s_doc)) { s_doclen = 0; return; }   // too long: drop it whole
    memcpy(s_doc + s_doclen, line, n);
    s_doclen += n;
    s_doc[s_doclen] = 0;

    // The array's close is the completion signal — the document may arrive in
    // any number of pieces, and the split can land inside an object.
    if (!strchr(s_doc, ']')) return;
    parse_doc();
    s_doclen = 0;
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
