#include "ui/pages.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "model/caps.h"
#include "model/fnc_settings.h"
#include "model/FluidNCModel.h"
#include "GrblParserC.h"
#include <stdio.h>
#include <string.h>

// Feed rate, acceleration, resolution and travel, per axis — the four numbers
// that decide how the machine moves, and the ones the panel previously had no
// way to touch. They were reachable only by scrolling ~55 rows of Diag and
// typing a raw value into a text box, which is a way to mistype a
// steps_per_mm.
//
// These are the FluidNC v4 $S keys ($Grbl/MaxRate/X and friends), so a write
// takes effect immediately and is rejected by the controller if it is invalid.
// Nothing here is persisted to config.yaml: FluidNC keeps these in NVS, and
// they override the YAML until $Grbl/... is reset. That is FluidNC's model,
// not a shortcut taken here.

struct field_def_t {
    caps_field_t field;
    const char*  label;
    const char*  unit;
    float        step;      // one tap of - / +
    float        lo, hi;    // refuse to send outside this
};

// Step sizes are per-field because the magnitudes are three orders of
// magnitude apart: a 100 mm/min nudge is sensible on a rate of 5000 and
// absurd on a resolution of 8.889. Limits are sanity rails, not machine
// limits — the controller is still the authority and will reject the rest.
static const field_def_t FIELDS[] = {
    { CAPS_RATE,   "MAX RATE",   "mm/min",  100.0f,  1.0f,   60000.0f },
    { CAPS_ACCEL,  "ACCEL",      "mm/s²", 25.0f,   1.0f,   10000.0f },
    { CAPS_STEPS,  "RESOLUTION", "steps/mm",  0.1f,  0.001f, 100000.0f },
    { CAPS_TRAVEL, "MAX TRAVEL", "mm",       10.0f,  1.0f,  100000.0f },
};
static const int NFIELDS = (int)(sizeof(FIELDS) / sizeof(FIELDS[0]));

static int s_field = 0;                       // which of FIELDS is being edited
static lv_obj_t* s_tab[NFIELDS];
static lv_obj_t* s_row[CAPS_MAX_AXES];
static lv_obj_t* s_ax_lbl[CAPS_MAX_AXES];
static lv_obj_t* s_val_lbl[CAPS_MAX_AXES];
static lv_obj_t* s_unit_lbl;
static lv_obj_t* s_note;

// A write is optimistic on screen; $S is re-read once the taps stop so the
// display ends up showing what the controller actually accepted rather than
// what was asked for.
static uint32_t s_refresh_at = 0;
static bool     s_pending    = false;

static void paint();

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static void write_axis(int axis, float v) {
    const field_def_t& f = FIELDS[s_field];
    v = clampf(v, f.lo, f.hi);

    char buf[24];
    // %g would emit 1e+06 for a big travel, which FluidNC's parser rejects.
    snprintf(buf, sizeof(buf), "%.3f", (double)v);
    fncset_write(caps_setting_for(f.field, axis), buf);

    s_pending    = true;
    s_refresh_at = (uint32_t)milliseconds() + 1200;
    paint();
}

static void bump_cb(lv_event_t* e) {
    int packed = (int)(intptr_t)lv_event_get_user_data(e);
    int axis = packed >> 1;
    int dir  = (packed & 1) ? 1 : -1;
    write_axis(axis, caps_axis_value(FIELDS[s_field].field, axis) + dir * FIELDS[s_field].step);
}

static void tab_cb(lv_event_t* e) {
    s_field = (int)(intptr_t)lv_event_get_user_data(e);
    paint();
}

static void paint() {
    for (int i = 0; i < NFIELDS; i++) {
        bool on = (i == s_field);
        lv_obj_set_style_bg_color(s_tab[i], on ? C_ACCENT : C_SURFACE, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(s_tab[i], 0), on ? C_BG : C_DIM, 0);
    }
    lv_label_set_text(s_unit_lbl, FIELDS[s_field].unit);

    int n = caps_axis_count();
    for (int a = 0; a < CAPS_MAX_AXES; a++) {
        if (a < n) {
            lv_obj_clear_flag(s_row[a], LV_OBJ_FLAG_HIDDEN);
            char ax[2] = { caps_axis_letter(a), 0 };
            lv_label_set_text(s_ax_lbl[a], ax);
            char b[24];
            float v = caps_axis_value(FIELDS[s_field].field, a);
            // Resolution needs the decimals; a rate of 5000.000 is just noise.
            snprintf(b, sizeof(b), FIELDS[s_field].field == CAPS_STEPS ? "%.3f" : "%.0f", (double)v);
            lv_label_set_text(s_val_lbl[a], b);
        } else {
            lv_obj_add_flag(s_row[a], LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (n == 0) {
        lv_label_set_text(s_note, "Waiting for the controller's settings...");
    } else if (s_pending) {
        lv_label_set_text(s_note, "Written. Re-reading $S to confirm...");
    } else {
        lv_label_set_text(s_note,
            "Live controller settings. Applied immediately\n"
            "and kept in the controller's NVS, not config.yaml.");
    }
}

void page_motion_build(lv_obj_t* p) {
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(p, U, 0);

    // --- field tabs ---
    lv_obj_t* tabs = lv_obj_create(p);
    w_flatten(tabs);
    lv_obj_set_width(tabs, LV_PCT(100));
    lv_obj_set_height(tabs, TOUCH_TAP);
    lv_obj_set_flex_flow(tabs, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(tabs, U, 0);
    for (int i = 0; i < NFIELDS; i++) {
        s_tab[i] = w_button(tabs, FIELDS[i].label, nullptr, C_SURFACE, C_DIM,
                            tab_cb, (void*)(intptr_t)i);
        lv_obj_set_flex_grow(s_tab[i], 1);
        lv_obj_set_height(s_tab[i], LV_PCT(100));
    }

    // --- one row per axis ---
    lv_obj_t* list = lv_obj_create(p);
    w_flatten(list);
    lv_obj_set_width(list, LV_PCT(100));
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, U / 2, 0);

    for (int a = 0; a < CAPS_MAX_AXES; a++) {
        s_row[a] = w_panel(list, C_SURFACE);
        lv_obj_set_width(s_row[a], LV_PCT(100));
        lv_obj_set_flex_grow(s_row[a], 1);
        lv_obj_set_flex_flow(s_row[a], LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(s_row[a], LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_left(s_row[a], U + 4, 0);
        lv_obj_set_style_pad_column(s_row[a], U / 2, 0);

        s_ax_lbl[a] = w_label(s_row[a], "?", &lv_font_montserrat_20, C_ACCENT);
        lv_obj_set_width(s_ax_lbl[a], U * 4);

        s_val_lbl[a] = w_label(s_row[a], "0", &lv_font_montserrat_20, C_TEXT);
        lv_obj_set_flex_grow(s_val_lbl[a], 1);
        lv_obj_set_style_text_align(s_val_lbl[a], LV_TEXT_ALIGN_RIGHT, 0);

        if (a == 0) {
            s_unit_lbl = w_label(s_row[a], "", &lv_font_montserrat_10, C_DIM);
            lv_obj_set_width(s_unit_lbl, U * 9);
        } else {
            lv_obj_t* pad = w_label(s_row[a], "", &lv_font_montserrat_10, C_DIM);
            lv_obj_set_width(pad, U * 9);
        }

        lv_obj_t* minus = w_button(s_row[a], "-", nullptr, C_RAISE, C_ACCENT,
                                   bump_cb, (void*)(intptr_t)((a << 1) | 0));
        lv_obj_set_size(minus, U * 7, LV_PCT(90));
        lv_obj_t* plus = w_button(s_row[a], "+", nullptr, C_RAISE, C_ACCENT,
                                  bump_cb, (void*)(intptr_t)((a << 1) | 1));
        lv_obj_set_size(plus, U * 7, LV_PCT(90));
    }

    s_note = w_label(p, "", &lv_font_montserrat_10, C_DIM);
    lv_obj_set_width(s_note, LV_PCT(100));

    paint();
}

void page_motion_tick() {
    // Re-read once the taps stop, so the row shows what the controller kept
    // rather than what was asked for. $S is 55 lines; firing it on every tap
    // of a +/- key would flood the link.
    if (s_pending && (int32_t)(milliseconds() - s_refresh_at) > 0) {
        s_pending = false;
        fncset_request_all();
    }

    static uint32_t seen = (uint32_t)-1;
    if (seen == caps_generation()) return;
    seen = caps_generation();
    paint();
}
