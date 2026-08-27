#include "model/caps.h"

// Declared rather than included: pulling in FluidNCModel.h drags GrblParserC
// with it, and caps needs exactly two symbols from the whole chain. Keeping
// the dependency this narrow is what lets the parser be unit-tested on the
// host against synthetic dumps, which is where its negative cases live.
void send_line(const char* s, int timeout);
extern "C" int milliseconds();

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>

// Line-oriented, not a YAML parser. `$CD` emits a fixed two-space-indent tree
// and only four facts are wanted out of it, so tracking the current top-level
// section and matching a handful of keys is enough. A real YAML parser would be
// a dependency and a heap allocator for no extra answer.

static cap_state_t s_probe  = CAP_UNKNOWN;
static cap_state_t s_homing = CAP_UNKNOWN;
static cap_state_t s_limits = CAP_UNKNOWN;

// Homing is two facts, not one, and they live in different parts of the tree:
// a homing cycle must be declared AND the axis must have a switch to seek.
// A config with cycles but no limit pins — which is what stripping endstops
// leaves behind — would otherwise show a HOME page whose $H drives the axis
// into the frame at seek rate.
static bool s_has_cycle = false;
static bool s_has_limit_pin = false;

static char s_mname[CAPS_MACRO_MAX][CAPS_MACRO_NAME_MAX];
static char s_mbody[CAPS_MACRO_MAX][CAPS_MACRO_NAME_MAX];
static size_t s_mcount = 0;

// Reading a dump right now. Ended by quiescence, NOT by the dump's "ok":
// GrblParserC's parse_report() matches "ok" before anything else and returns,
// so the ack never reaches handle_other_line() and an end-of-dump sentinel is
// simply unavailable here. The dump arrives as a continuous burst, so a gap
// with no config lines is the reliable signal that it finished.
static bool     s_capturing = false;
static bool     s_settled   = false;
static uint32_t s_last_ms   = 0;   // last dump line seen
static uint32_t s_req_ms    = 0;   // when $CD was queued
static size_t   s_lines     = 0;

// Quiet gap that ends a dump, and how long to wait for it to start at all.
static const uint32_t CAPS_QUIET_MS = 400;
static const uint32_t CAPS_START_MS = 4000;

// Which top-level section the current line sits under.
enum section_t { SEC_NONE = 0, SEC_PROBE, SEC_MACROS, SEC_OTHER };
static section_t s_sec = SEC_NONE;

void caps_reset() {
    s_probe = s_homing = s_limits = CAP_UNKNOWN;
    s_mcount = 0;
    s_capturing = false;
    s_settled = false;
    s_sec = SEC_NONE;
    s_lines = 0;
    s_last_ms = s_req_ms = 0;
    s_has_cycle = s_has_limit_pin = false;
}

// complete == a whole tree actually arrived. Only then does "not mentioned"
// mean "absent"; if the dump never came, everything stays UNKNOWN and the UI
// keeps showing the pages rather than hiding controls on no evidence.
static void caps_finish(bool complete) {
    s_capturing = false;
    s_settled   = true;
    if (!complete) return;
    if (s_probe  == CAP_UNKNOWN) s_probe  = CAP_NO;
    if (s_limits == CAP_UNKNOWN) s_limits = CAP_NO;
    // Both halves, or homing is not offered at all.
    s_homing = (s_has_cycle && s_has_limit_pin) ? CAP_YES : CAP_NO;
}

void caps_tick() {
    if (!s_capturing) return;
    const uint32_t now = (uint32_t)milliseconds();
    if (s_lines) {
        if (now - s_last_ms > CAPS_QUIET_MS) caps_finish(true);
    } else if (now - s_req_ms > CAPS_START_MS) {
        caps_finish(false);          // $CD unsupported or unanswered
    }
}

void caps_request() {
    caps_reset();
    s_capturing = true;
    s_req_ms = (uint32_t)milliseconds();
    // Bare $CD dumps to the channel. `$CD=<file>` would WRITE the live tree
    // over the boot config — never send the assignment form from here.
    send_line("$CD", 2000);
}

cap_state_t caps_probe()  { return s_probe; }
cap_state_t caps_homing() { return s_homing; }
cap_state_t caps_limits() { return s_limits; }

cap_state_t caps_macros() {
    if (!s_settled) return CAP_UNKNOWN;
    return s_mcount ? CAP_YES : CAP_NO;
}

bool caps_settled() { return s_settled; }

size_t      caps_macro_count() { return s_mcount; }
const char* caps_macro_name(size_t i) { return i < s_mcount ? s_mname[i] : ""; }
const char* caps_macro_body(size_t i) { return i < s_mcount ? s_mbody[i] : ""; }

// Leading spaces before the first non-space.
static int indent_of(const char* s) {
    int n = 0;
    while (s[n] == ' ') n++;
    return n;
}

// Copy a scalar value, stripping surrounding quotes and trailing whitespace.
static void copy_value(char* dst, size_t cap, const char* src) {
    while (*src == ' ') src++;
    size_t n = strlen(src);
    while (n && (src[n - 1] == ' ' || src[n - 1] == '\r' || src[n - 1] == '\n')) n--;
    if (n >= 2 && ((src[0] == '\'' && src[n - 1] == '\'') ||
                   (src[0] == '"'  && src[n - 1] == '"'))) { src++; n -= 2; }
    if (n > cap - 1) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
}

static bool value_is_set(const char* v) {
    return v[0] && strcmp(v, "NO_PIN") != 0;
}

bool caps_feed_line(const char* line) {
    if (!s_capturing || !line) return false;

    if (!line[0]) return false;

    const int ind = indent_of(line);
    const char* t = line + ind;

    // Only a line that looks like part of the tree counts as dump content, and
    // "has a colon somewhere" is not that test. The $Limits/Show monitor emits
    // ": <pos> <neg> P T" twice a second; those matched, kept the quiet window
    // permanently fresh, and the dump never ended. A config key always starts
    // with a letter, which excludes the monitor (':'), status reports ('<') and
    // bracketed replies ('[') in one condition.
    if (!isalpha((unsigned char)t[0])) return false;
    if (!strchr(t, ':')) return false;

    s_lines++;
    s_last_ms = (uint32_t)milliseconds();

    if (ind == 0) {
        // New top-level section.
        if      (strncmp(t, "probe:",  6) == 0) s_sec = SEC_PROBE;
        else if (strncmp(t, "macros:", 7) == 0) s_sec = SEC_MACROS;
        else                                    s_sec = SEC_OTHER;
        return true;
    }

    const char* colon = strchr(t, ':');
    if (!colon) return true;

    char key[32];
    size_t klen = (size_t)(colon - t);
    if (klen > sizeof(key) - 1) klen = sizeof(key) - 1;
    memcpy(key, t, klen);
    key[klen] = 0;

    char val[CAPS_MACRO_NAME_MAX];
    copy_value(val, sizeof(val), colon + 1);

    if (s_sec == SEC_PROBE && strcmp(key, "pin") == 0) {
        s_probe = value_is_set(val) ? CAP_YES : CAP_NO;
        return true;
    }

    if (s_sec == SEC_MACROS && strncmp(key, "Macro", 5) == 0) {
        // Empty slots are the common case and must not become buttons.
        if (val[0] && s_mcount < CAPS_MACRO_MAX) {
            snprintf(s_mname[s_mcount], CAPS_MACRO_NAME_MAX, "%s", key);
            snprintf(s_mbody[s_mcount], CAPS_MACRO_NAME_MAX, "%s", val);
            s_mcount++;
        }
        return true;
    }

    // `cycle:` appears only under an axis's homing block; a cycle of 0 means
    // that axis is excluded from homing.
    if (strcmp(key, "cycle") == 0 && atoi(val) >= 1) s_has_cycle = true;

    // limit_neg_pin / limit_pos_pin / limit_all_pin, per motor. One assigned
    // switch anywhere is enough for homing to have something to seek.
    if (strncmp(key, "limit_", 6) == 0 && strstr(key, "_pin") && value_is_set(val))
        s_has_limit_pin = true;

    // soft_limits sits per axis, hard_limits per motor. One of either, on any
    // axis, is enough to stop this reading as a wholly unprotected machine.
    if ((strcmp(key, "soft_limits") == 0 || strcmp(key, "hard_limits") == 0) &&
        strcmp(val, "true") == 0) s_limits = CAP_YES;

    return true;
}
