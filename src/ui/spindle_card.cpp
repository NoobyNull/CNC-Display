#include "ui/spindle_card.h"
#include "ui/home_wizard.h"
#include "ui/splash.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "model/FluidNCModel.h"
#include "model/caps.h"
#include "model/homed.h"
#include "model/settings.h"
#include "model/toolchange.h"
#include "GrblParserC.h"
#include <stdio.h>
#include <stdint.h>

static const int      POCKETS = 5;                 // as page_tools.cpp: the rack's
static const uint32_t TAP_START_MS = 3000;         // a tap that never leaves Idle was refused
static const char* const KEY = "tool_last";        // survives the controller forgetting

static lv_obj_t* s_root;
static lv_obj_t* s_tool[POCKETS + 1];              // [0] is CUSTOM: T0 is the custom tool
static lv_obj_t* s_none;
static lv_obj_t* s_tap_btn;
static lv_obj_t* s_msg;
static bool      s_tapping;
static bool      s_tap_left_idle;
static uint32_t  s_tap_at;

static void close_card() {
    if (!s_root) return;
    lv_obj_del(s_root);
    s_root = nullptr;
    s_tapping = false;
}

static void answer_cb(lv_event_t* e) {
    const int t = (int)(intptr_t)lv_event_get_user_data(e);
    send_linef("M61 Q%d", t);
    send_line(TC_ASK_CURRENT);
    tc_assume(t);
    close_card();
}

static void tap_cb(lv_event_t*) {
    tc_tap_clear();
    s_tapping = true;
    s_tap_left_idle = false;
    s_tap_at = lv_tick_get();
    send_line("$LocalFS/Run=openatc_tap.nc");
}

static void paint() {
    const int   tap  = tc_tap();
    const bool  idle = state == Idle && !s_tapping;
    const char* busy = s_tapping ? "checking" : "machine busy";

    for (int t = 0; t <= POCKETS; t++)
        w_gate(s_tool[t], idle && tap != TC_TAP_EMPTY, idle ? "toolsetter found nothing" : busy);
    w_gate(s_none, idle && tap != TC_TAP_TOOL, idle ? "toolsetter touched a tool" : busy);
    w_gate(s_tap_btn, idle && homed(), !idle ? busy : "home first");

    char b[96];
    const int last = (int)settings_get_float(KEY, -1.0f);
    if      (s_tapping)                snprintf(b, sizeof(b), "Checking toolsetter...");
    else if (tap == TC_TAP_TOOL)       snprintf(b, sizeof(b), "Toolsetter touched something: which tool is it?");
    else if (tap == TC_TAP_EMPTY)      snprintf(b, sizeof(b), "Toolsetter found nothing: the spindle is empty.");
    else if (last == TC_EMPTY)         snprintf(b, sizeof(b), "Last known: empty.");
    else if (last == 0)                snprintf(b, sizeof(b), "Last known: custom tool.");
    else if (last > 0)                 snprintf(b, sizeof(b), "Last known: T%d.", last);
    else                               snprintf(b, sizeof(b), "Nothing on record.");
    lv_label_set_text(s_msg, b);
}

static lv_obj_t* row(lv_obj_t* card) {
    lv_obj_t* r = lv_obj_create(card);
    w_flatten(r);
    lv_obj_set_size(r, LV_PCT(100), TOUCH_HOLD);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(r, U, 0);
    return r;
}

static lv_obj_t* grow(lv_obj_t* b) {
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_height(b, LV_PCT(100));
    return b;
}

static void open_card() {
    tc_tap_clear();

    // Same shape as the homing card: stops short of the safety bar.
    s_root = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, SCREEN_W, SCREEN_H - SAFETY_H);
    lv_obj_set_style_bg_color(s_root, C_BG, 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_80, 0);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* card = w_panel(s_root, C_SURFACE);
    lv_obj_set_size(card, CONTENT_W - U * 4, LV_SIZE_CONTENT);
    lv_obj_center(card);
    lv_obj_set_style_pad_all(card, U * 2, 0);
    lv_obj_set_style_pad_row(card, U, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);

    w_label(card, "WHAT'S IN THE SPINDLE?", &lv_font_montserrat_20, C_WARN);
    lv_obj_t* why = w_label(card, "The controller does not know. The rack needs the truth: "
                                  "a wrong answer sends a cartridge to a full pocket.",
                            &lv_font_montserrat_14, C_TEXT);
    lv_obj_set_width(why, LV_PCT(100));
    lv_label_set_long_mode(why, LV_LABEL_LONG_WRAP);
    s_msg = w_label(card, "", &lv_font_montserrat_14, C_DIM);

    lv_obj_t* tools = row(card);
    for (int t = 1; t <= POCKETS; t++) {
        char b[8];
        snprintf(b, sizeof(b), "T%d", t);
        s_tool[t] = grow(w_button(tools, b, " ", C_RAISE, C_TEXT, answer_cb, (void*)(intptr_t)t));
    }
    lv_obj_t* others = row(card);
    s_none    = grow(w_button(others, "NONE / SKIP", "assume empty", C_RAISE, C_TEXT, answer_cb, (void*)(intptr_t)TC_EMPTY));
    s_tool[0] = grow(w_button(others, "CUSTOM", "by hand, rack full", C_RAISE, C_TEXT, answer_cb, (void*)(intptr_t)0));
    s_tap_btn = grow(w_button(others, "TOOLSETTER", "tool or no tool", C_ACCENT, C_BG, tap_cb, nullptr));
    paint();
}

void spindle_card_tick() {
    if (caps_atc() != CAPS_ATC_MACRO) { close_card(); return; }

    // Ask at rest, and again after anything that may have reset the controller.
    static state_t  was   = Disconnected;
    static uint32_t epoch = UINT32_MAX;
    static bool     macro = false;
    if (state == Idle && (was != Idle || epoch != homed_epoch() || !macro)) send_line(TC_ASK_CURRENT);
    const bool changed = state != was || epoch != homed_epoch();
    was = state;
    epoch = homed_epoch();
    macro = true;

    static int saved = (int)settings_get_float(KEY, -1.0f);
    if (tc_current() >= 0 && tc_current() != saved) {
        saved = tc_current();
        settings_set_float(KEY, (float)saved);
    }

    if (!s_root) {
        if (tc_current_known() && tc_current() < 0 && state == Idle &&
            !home_wizard_open() && !splash_active()) open_card();
        return;
    }
    if (tc_current() >= 0 || state == Disconnected || (state != Idle && !s_tapping)) {
        close_card();
        return;
    }
    if (s_tapping) {
        if (state != Idle) s_tap_left_idle = true;
        const bool done = tc_tap() != TC_TAP_NONE || state == Alarm || (state == Idle && s_tap_left_idle) ||
                          (!s_tap_left_idle && lv_tick_elaps(s_tap_at) > TAP_START_MS);
        if (done) { s_tapping = false; paint(); }
    } else if (changed) {
        paint();
    }
}
