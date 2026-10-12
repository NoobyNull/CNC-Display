#include "ui/tool_popup.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "model/FluidNCModel.h"
#include "model/toolchange.h"
#include "GrblParserC.h"
#include <stdio.h>

static lv_obj_t* s_root;
static int       s_shown_tool;
static bool      s_shown_measured;

static void close_popup() {
    if (!s_root) return;
    lv_obj_del(s_root);
    s_root = nullptr;
}

static void changed_cb(lv_event_t*) { fnc_realtime(CycleStart); }

static lv_obj_t* text(lv_obj_t* card, const char* t, const lv_font_t* f, lv_color_t c) {
    lv_obj_t* l = w_label(card, t, f, c);
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    return l;
}

static void open_popup() {
    s_shown_tool     = tc_tool();
    s_shown_measured = tc_measured();

    // Same shape as the homing card: stops short of the safety bar.
    s_root = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, SCREEN_W, SCREEN_H - SAFETY_H);
    lv_obj_set_style_bg_color(s_root, C_BG, 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_80, 0);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* card = w_panel(s_root, C_SURFACE);
    lv_obj_set_size(card, CONTENT_W - U * 8, LV_SIZE_CONTENT);
    lv_obj_center(card);
    lv_obj_set_style_pad_all(card, U * 2, 0);
    lv_obj_set_style_pad_row(card, U, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);

    char b[16];
    snprintf(b, sizeof(b), "T%d", s_shown_tool);
    text(card, "TOOL CHANGE", &lv_font_montserrat_14, C_WARN);
    text(card, b, &lv_font_montserrat_40, C_TEXT);
    if (*tc_name()) text(card, tc_name(), &lv_font_montserrat_16, C_TEXT);
    text(card, s_shown_measured
        ? "Install it, then TOOL CHANGED. The machine measures it on the toolsetter, "
          "sets the length offset and goes back to the cut. Leave Z zero alone."
        : "Install it, then TOOL CHANGED to resume. This file was posted without M6, "
          "so the new tool will NOT be measured.",
        &lv_font_montserrat_14, s_shown_measured ? C_DIM : C_WARN);

    lv_obj_t* go = w_button(card, "TOOL CHANGED", s_shown_measured ? "measure + resume" : "resume",
                            C_OK, C_BG, changed_cb, nullptr);
    lv_obj_set_size(go, LV_PCT(100), TOUCH_HOLD);
}

void tool_popup_tick() {
    // Job over: nothing pending. On the edge only -- the message can land
    // before the status report that says the job started.
    static state_t was = Disconnected;
    if (state != was && (state == Idle || state == Alarm)) tc_clear();
    was = state;

    if (state != Hold) {
        // Resumed (or reset): this request is done. A later one arrives fresh.
        if (s_root) { close_popup(); tc_clear(); }
        return;
    }
    if (!tc_tool()) return;
    // ponytail: rebuilt rather than updated when atc_manual's message lands on an open card
    if (s_root && (s_shown_tool != tc_tool() || s_shown_measured != tc_measured())) close_popup();
    if (!s_root) open_popup();
}
