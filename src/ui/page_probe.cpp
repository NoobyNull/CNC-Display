#include "ui/pages.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "model/FluidNCModel.h"
#include "GrblParserC.h"
#include <stdio.h>

struct param_t {
    const char* name;
    float       value;
    float       step;
    lv_obj_t*   lbl;
};

static param_t s_params[4] = {
    { "Tool offset", 0.0f,   0.1f,  nullptr },
    { "Max travel",  25.0f,  1.0f,  nullptr },
    { "Feed rate",   100.0f, 10.0f, nullptr },
    { "Retract",     2.0f,   0.5f,  nullptr },
};

static lv_obj_t* s_gcode_lbl;

static void refresh_param(int i) {
    char b[24];
    snprintf(b, sizeof(b), "%.2f", (double)s_params[i].value);
    lv_label_set_text(s_params[i].lbl, b);
}

static void refresh_gcode() {
    char b[64];
    snprintf(b, sizeof(b), "G38.2 G91 F%d Z-%d",
             (int)s_params[2].value, (int)s_params[1].value);
    lv_label_set_text(s_gcode_lbl, b);
}

static void bump_cb(lv_event_t* e) {
    int packed = (int)(intptr_t)lv_event_get_user_data(e);
    int idx = packed >> 1;
    int dir = (packed & 1) ? 1 : -1;
    s_params[idx].value += dir * s_params[idx].step;
    if (s_params[idx].value < 0.0f) s_params[idx].value = 0.0f;
    refresh_param(idx);
    refresh_gcode();
}

// Sequence copied from FluidDial-main/src/ProbingScene.cpp:29 and :51 rather
// than re-derived: probe down, then retract by the configured distance.
static void probe_cb(lv_event_t*) {
    send_linef("G38.2G91F%dZ-%dP%.2f",
               (int)s_params[2].value, (int)s_params[1].value, (double)s_params[0].value);
    send_linef("$J=G91F1000Z%.2f", (double)s_params[3].value);
}

void page_probe_build(lv_obj_t* p) {
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(p, U, 0);

    lv_obj_t* left = lv_obj_create(p);
    w_flatten(left);
    lv_obj_set_size(left, U * 55, LV_PCT(100));
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(left, U, 0);

    w_label(left, "PROBE PARAMETERS - G38.2", &lv_font_montserrat_10, C_DIM);

    for (int i = 0; i < 4; i++) {
        lv_obj_t* row = w_panel(left, C_SURFACE);
        // Share the column: four fixed 48px rows left the parameter list
        // stranded at the top of the taller page. Growing keeps the +/- keys
        // above the touch floor and ends the list level with the right column.
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_flex_grow(row, 1);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_left(row, U + 4, 0);

        lv_obj_t* nm = w_label(row, s_params[i].name, &lv_font_montserrat_14, C_TEXT);
        lv_obj_set_flex_grow(nm, 1);

        lv_obj_t* minus = w_button(row, "-", nullptr, C_RAISE, C_ACCENT,
                                   bump_cb, (void*)(intptr_t)((i << 1) | 0));
        lv_obj_set_size(minus, U * 6, U * 5);

        s_params[i].lbl = w_label(row, "0.00", &lv_font_montserrat_16, C_TEXT);
        lv_obj_set_width(s_params[i].lbl, U * 10);
        lv_obj_set_style_text_align(s_params[i].lbl, LV_TEXT_ALIGN_CENTER, 0);

        lv_obj_t* plus = w_button(row, "+", nullptr, C_RAISE, C_ACCENT,
                                  bump_cb, (void*)(intptr_t)((i << 1) | 1));
        lv_obj_set_size(plus, U * 6, U * 5);

        refresh_param(i);
    }

    lv_obj_t* right = lv_obj_create(p);
    w_flatten(right);
    lv_obj_set_flex_grow(right, 1);
    lv_obj_set_height(right, LV_PCT(100));
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(right, U, 0);

    lv_obj_t* st = w_panel(right, C_SURFACE);
    lv_obj_set_width(st, LV_PCT(100));
    lv_obj_set_flex_grow(st, 1);
    lv_obj_t* stk = w_label(st, "PROBE INPUT", &lv_font_montserrat_10, C_DIM);
    lv_obj_align(stk, LV_ALIGN_CENTER, 0, -12);
    lv_obj_t* stv = w_label(st, "OPEN", &lv_font_montserrat_20, C_OK);
    lv_obj_align(stv, LV_ALIGN_CENTER, 0, 10);

    lv_obj_t* go = lv_btn_create(right);
    lv_obj_set_width(go, LV_PCT(100));
    lv_obj_set_flex_grow(go, 1);
    lv_obj_set_style_bg_color(go, C_WARN, 0);
    lv_obj_set_style_radius(go, 3, 0);
    lv_obj_set_style_shadow_width(go, 0, 0);
    lv_obj_add_event_cb(go, probe_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* gl = w_label(go, "PROBE Z", &lv_font_montserrat_20, C_BG);
    lv_obj_align(gl, LV_ALIGN_CENTER, 0, -10);
    s_gcode_lbl = w_label(go, "", &lv_font_montserrat_12, C_BG);
    lv_obj_align(s_gcode_lbl, LV_ALIGN_CENTER, 0, 14);
    refresh_gcode();
}
