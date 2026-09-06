#include "model/caps.h"
#include "model/fnc_settings.h"
#include "model/FluidNCModel.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static cap_state_t s_homing      = CAP_UNKNOWN;
static cap_state_t s_hard_limits = CAP_UNKNOWN;
static cap_state_t s_soft_limits = CAP_UNKNOWN;
static cap_state_t s_probe       = CAP_UNKNOWN;
static cap_state_t s_toolsetter  = CAP_UNKNOWN;
static cap_state_t s_flood       = CAP_UNKNOWN;
static cap_state_t s_mist        = CAP_UNKNOWN;
static cap_state_t s_door        = CAP_UNKNOWN;
static cap_state_t s_estop       = CAP_UNKNOWN;

static uint32_t s_gen = 0;
static bool     s_settled = false;

static size_t s_mcount = 0;
static char   s_mname[CAPS_MACRO_MAX][CAPS_MACRO_NAME_MAX];
static char   s_mbody[CAPS_MACRO_MAX][CAPS_MACRO_NAME_MAX];

struct axis_t {
    char  letter;
    float v[CAPS_FIELD_COUNT];
    bool  seen;
};
static axis_t s_axes[CAPS_MAX_AXES];
static int    s_naxes = 0;

// $CD scan state: which top-level YAML section the scanner is inside.
static bool     s_dumping  = false;   // $CD sent, reply not finished
static bool     s_started  = false;   // the YAML itself has begun arriving
static uint32_t s_deadline = 0;       // give up waiting for it to begin
static char     s_section[24] = "";

static const char* AXIS_LETTERS = "XYZABC";

void caps_reset() {
    s_homing = s_hard_limits = s_soft_limits = CAP_UNKNOWN;
    s_probe = s_toolsetter = s_flood = s_mist = CAP_UNKNOWN;
    s_door = s_estop = CAP_UNKNOWN;
    s_naxes = 0;
    memset(s_axes, 0, sizeof(s_axes));
    s_dumping = false;
    s_section[0] = 0;
    s_settled = false;
    s_mcount = 0;
    s_gen++;
}

uint32_t caps_generation() { return s_gen; }
bool     caps_settled()    { return s_settled; }

size_t      caps_macro_count() { return s_mcount; }
const char* caps_macro_name(size_t i) { return i < s_mcount ? s_mname[i] : ""; }
const char* caps_macro_body(size_t i) { return i < s_mcount ? s_mbody[i] : ""; }

cap_state_t caps_homing()      { return s_homing; }
cap_state_t caps_probe()       { return s_probe; }
cap_state_t caps_toolsetter()  { return s_toolsetter; }
cap_state_t caps_flood()       { return s_flood; }
cap_state_t caps_mist()        { return s_mist; }
cap_state_t caps_hard_limits() { return s_hard_limits; }
cap_state_t caps_soft_limits() { return s_soft_limits; }
cap_state_t caps_safety_door() { return s_door; }
cap_state_t caps_estop()       { return s_estop; }

const char* caps_why(cap_state_t c, const char* what) {
    if (c != CAP_ABSENT) return nullptr;
    static char b[64];
    snprintf(b, sizeof(b), "no %s configured", what ? what : "hardware");
    return b;
}

// --- $S half ---------------------------------------------------------------

static const char* setting_value(const char* name) {
    for (size_t i = 0; i < fncset_count(); i++) {
        const fnc_setting_t* s = fncset_at(i);
        if (s && strcmp(s->name, name) == 0) return s->value;
    }
    return nullptr;
}

static cap_state_t bool_setting(const char* name) {
    const char* v = setting_value(name);
    if (!v) return CAP_UNKNOWN;
    return (v[0] == '0' && v[1] == 0) ? CAP_ABSENT : CAP_PRESENT;
}

void caps_refresh_from_settings() {
    // Homing: FluidNC reports 0 when no axis declares a homing cycle, which is
    // exactly the bring-up Corgi's state. $H answers error:5 there.
    s_homing      = bool_setting("Grbl/HomingCycleEnable");
    s_hard_limits = bool_setting("Grbl/HardLimitsEnable");
    s_soft_limits = bool_setting("Grbl/SoftLimitsEnable");

    s_naxes = 0;
    memset(s_axes, 0, sizeof(s_axes));
    for (int i = 0; AXIS_LETTERS[i]; i++) {
        char name[32];
        bool any = false;
        axis_t a{};
        a.letter = AXIS_LETTERS[i];
        for (int f = 0; f < CAPS_FIELD_COUNT; f++) {
            snprintf(name, sizeof(name), "Grbl/%s/%c", caps_field_name((caps_field_t)f), a.letter);
            const char* v = setting_value(name);
            if (v) { a.v[f] = strtof(v, nullptr); any = true; }
        }
        // An axis exists in $S only if the controller has one configured, so
        // presence of any of its four keys is the axis count.
        if (any && s_naxes < CAPS_MAX_AXES) {
            a.seen = true;
            s_axes[s_naxes++] = a;
        }
    }
    s_gen++;
}

void caps_poll() {
    // The settings table only ever grows within a dump, so its size is a
    // sufficient change signal — and unlike fncset_dirty_take() it can be read
    // by any number of consumers.
    static size_t last = (size_t)-1;
    size_t n = fncset_count();
    if (n != last) {
        last = n;
        caps_refresh_from_settings();
    }

    // A $CD that never produced a dump must not leave the scanner armed
    // forever, or a stray YAML-shaped console line months later would be read
    // as configuration. Firmware without $CD leaves every pin CAP_UNKNOWN,
    // which keeps the affected controls enabled.
    if (s_dumping && !s_started && (int32_t)(milliseconds() - s_deadline) > 0) {
        s_dumping = false;
        s_probe = s_toolsetter = s_flood = s_mist = CAP_UNKNOWN;
        s_door = s_estop = CAP_UNKNOWN;
        s_settled = true;      // gave up: the splash gate must not wait for ever
        s_gen++;
    }
}

int  caps_axis_count() { return s_naxes; }
char caps_axis_letter(int i) { return (i >= 0 && i < s_naxes) ? s_axes[i].letter : '?'; }

float caps_axis_value(caps_field_t f, int i) {
    if (i < 0 || i >= s_naxes || f < 0 || f >= CAPS_FIELD_COUNT) return 0.0f;
    return s_axes[i].v[f];
}

float caps_axis_steps(int i)  { return caps_axis_value(CAPS_STEPS, i); }
float caps_axis_rate(int i)   { return caps_axis_value(CAPS_RATE, i); }
float caps_axis_accel(int i)  { return caps_axis_value(CAPS_ACCEL, i); }
float caps_axis_travel(int i) { return caps_axis_value(CAPS_TRAVEL, i); }

const char* caps_field_name(caps_field_t f) {
    switch (f) {
        case CAPS_STEPS:  return "Resolution";
        case CAPS_RATE:   return "MaxRate";
        case CAPS_ACCEL:  return "Acceleration";
        case CAPS_TRAVEL: return "MaxTravel";
        default:          return "";
    }
}

const char* caps_setting_for(caps_field_t f, int axis) {
    static char b[32];
    snprintf(b, sizeof(b), "Grbl/%s/%c", caps_field_name(f), caps_axis_letter(axis));
    return b;
}

// --- $CD half --------------------------------------------------------------

void caps_request_config() {
    s_dumping  = true;
    s_started  = false;
    s_deadline = (uint32_t)milliseconds() + 15000;
    s_section[0] = 0;
    // Assume absent, then let the dump prove otherwise: a configured pin always
    // appears in the dump, a missing one never does, so "absent" could not be
    // reached by waiting for a line that will not come.
    s_probe = s_toolsetter = s_flood = s_mist = CAP_ABSENT;
    s_door = s_estop = CAP_ABSENT;
    s_mcount = 0;              // empty slots are the common case, dump proves otherwise
    send_line("$CD");
}

// "NO_PIN", empty, or absent all mean not configured.
static bool pin_set(const char* v) {
    if (!v) return false;
    while (*v == ' ') v++;
    if (!*v) return false;
    // Values can be quoted when they carry modifiers: 'I2SO.4:low'.
    if (*v == '\'' || *v == '"') v++;
    return strncasecmp(v, "NO_PIN", 6) != 0;
}

// Value after "key:" on a YAML line, or nullptr if this line is not that key.
static const char* yaml_value(const char* line, const char* key) {
    const char* p = line;
    while (*p == ' ') p++;
    size_t n = strlen(key);
    if (strncmp(p, key, n) != 0 || p[n] != ':') return nullptr;
    p += n + 1;
    while (*p == ' ') p++;
    return p;
}

bool caps_feed_line(const char* line) {
    if (!s_dumping || !line || !*line) return false;

    // A config dump is YAML and contains none of these. Letting them through
    // rather than swallowing them means a mis-timed dump flag can never cost
    // the settings table, the console or the DRO their data — it only costs
    // this scanner a capability it will re-read on the next dump.
    if (line[0] == '$' || line[0] == '[' || line[0] == '<' || line[0] == '{') return false;

    // A top-level key (unindented, ends in ':') opens a section. Everything
    // else is read relative to whichever section is open, which is what makes
    // 'pin:' under 'probe:' distinguishable from any other 'pin:'.
    bool top = (line[0] != ' ' && line[0] != '\t' && line[0] != '#' && strchr(line, ':'));

    // Until the YAML actually starts, nothing is consumed and no terminator is
    // honoured. $CD is queued alongside $#, $S and the file list, and their
    // replies each end in their own "ok" — treating the first one seen as the
    // end of the config dump is what previously left every pin unread.
    if (!s_started) {
        if (!top) return false;
        s_started = true;
    }

    // FluidNC terminates command output with ok / error. Anything after that
    // belongs to the console again.
    if (strcmp(line, "ok") == 0 || strncmp(line, "error", 5) == 0) {
        s_dumping = false;
        s_settled = true;
        s_gen++;
        return true;
    }

    if (top) {
        const char* colon = strchr(line, ':');
        size_t n = (size_t)(colon - line);
        if (n >= sizeof(s_section)) n = sizeof(s_section) - 1;
        memcpy(s_section, line, n);
        s_section[n] = 0;
        return true;
    }

    const char* v;
    if (strcmp(s_section, "probe") == 0) {
        if ((v = yaml_value(line, "pin")))            s_probe      = pin_set(v) ? CAP_PRESENT : CAP_ABSENT;
        if ((v = yaml_value(line, "toolsetter_pin"))) s_toolsetter = pin_set(v) ? CAP_PRESENT : CAP_ABSENT;
    } else if (strcmp(s_section, "coolant") == 0) {
        if ((v = yaml_value(line, "flood_pin"))) s_flood = pin_set(v) ? CAP_PRESENT : CAP_ABSENT;
        if ((v = yaml_value(line, "mist_pin")))  s_mist  = pin_set(v) ? CAP_PRESENT : CAP_ABSENT;
    } else if (strcmp(s_section, "macros") == 0) {
        const char* sp = line;
        while (*sp == ' ') sp++;
        if (strncmp(sp, "Macro", 5) == 0) {
            const char* colon = strchr(sp, ':');
            if (colon && s_mcount < CAPS_MACRO_MAX) {
                char name[CAPS_MACRO_NAME_MAX];
                size_t n = (size_t)(colon - sp);
                if (n >= sizeof(name)) n = sizeof(name) - 1;
                memcpy(name, sp, n); name[n] = 0;
                const char* val = colon + 1;
                while (*val == ' ') val++;
                // Values can be quoted; the subtitle wants the bare g-code.
                char body[CAPS_MACRO_NAME_MAX];
                size_t bn = strlen(val);
                while (bn && (val[bn-1] == ' ' || val[bn-1] == '\r')) bn--;
                if (bn >= 2 && ((val[0] == '\'' && val[bn-1] == '\'') ||
                                (val[0] == '"'  && val[bn-1] == '"'))) { val++; bn -= 2; }
                if (bn >= sizeof(body)) bn = sizeof(body) - 1;
                memcpy(body, val, bn); body[bn] = 0;
                // Empty slots must not become buttons.
                if (body[0]) {
                    snprintf(s_mname[s_mcount], CAPS_MACRO_NAME_MAX, "%s", name);
                    snprintf(s_mbody[s_mcount], CAPS_MACRO_NAME_MAX, "%s", body);
                    s_mcount++;
                }
            }
        }
    } else if (strcmp(s_section, "control") == 0) {
        if ((v = yaml_value(line, "safety_door_pin"))) s_door  = pin_set(v) ? CAP_PRESENT : CAP_ABSENT;
        if ((v = yaml_value(line, "estop_pin")))       s_estop = pin_set(v) ? CAP_PRESENT : CAP_ABSENT;
    }
    return true;
}
