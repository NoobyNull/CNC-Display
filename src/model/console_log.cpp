#include "model/console_log.h"
#include "model/fnc_codes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// FluidNC emits <Run|MPos:...> several times a second. Unfiltered, those scroll
// real errors out of a 13-line window in about two seconds — which is why the
// status filter defaults to on and is the point of this module.

static clog_line_t s_ring[CLOG_CAPACITY];
static size_t s_head  = 0;      // index of the oldest retained line
static size_t s_count = 0;
static bool   s_show_status = false;
static bool   s_dirty = false;

void clog_reset() {
    s_head = s_count = 0;
    s_dirty = true;
}

void clog_set_show_status(bool on) { s_show_status = on; s_dirty = true; }
bool clog_show_status()            { return s_show_status; }

static clog_kind_t classify(const char* s) {
    if (s[0] == '<')                  return CLOG_STATUS;
    if (s[0] == '>')                  return CLOG_SENT;
    if (strncmp(s, "ok", 2) == 0)     return CLOG_OK;
    if (strncmp(s, "error:", 6) == 0) return CLOG_ERROR;
    if (strncmp(s, "ALARM:", 6) == 0) return CLOG_ALARM;
    if (strncmp(s, "[MSG:", 5) == 0)  return CLOG_MSG;
    return CLOG_INFO;
}

// "error:22" -> "error:22 Gcode undefined feed rate". Only a bare code is
// named: anything already carrying text is left as FluidNC sent it.
static const char* with_name(const char* line, clog_kind_t k, char* buf, size_t n) {
    if (k != CLOG_ERROR && k != CLOG_ALARM) return line;
    char* end;
    long code = strtol(line + 6, &end, 10);
    if (end == line + 6 || *end) return line;
    const char* name = k == CLOG_ERROR ? fnc_error_name((int)code) : fnc_alarm_name((int)code);
    if (!name) return line;
    snprintf(buf, n, "%s %s", line, name);
    return buf;
}

void clog_add(const char* line) {
    if (!line || !*line) return;

    clog_kind_t k = classify(line);
    if (k == CLOG_STATUS && !s_show_status) return;
    char named[CLOG_LINE_MAX];
    line = with_name(line, k, named, sizeof(named));

    size_t slot;
    if (s_count < CLOG_CAPACITY) {
        slot = (s_head + s_count) % CLOG_CAPACITY;
        s_count++;
    } else {
        slot   = s_head;
        s_head = (s_head + 1) % CLOG_CAPACITY;
    }

    strncpy(s_ring[slot].text, line, CLOG_LINE_MAX - 1);
    s_ring[slot].text[CLOG_LINE_MAX - 1] = 0;
    s_ring[slot].kind = k;
    s_dirty = true;
}

size_t clog_count() { return s_count; }

const clog_line_t* clog_line(size_t i) {
    if (i >= s_count) return nullptr;
    return &s_ring[(s_head + i) % CLOG_CAPACITY];
}

bool clog_dirty_take() {
    bool d = s_dirty;
    s_dirty = false;
    return d;
}
