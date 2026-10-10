#include "ui/home_wizard.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "ui/splash.h"
#include "model/FluidNCModel.h"
#include "model/fnc_codes.h"
#include "model/caps.h"
#include "model/homed.h"
#include "GrblParserC.h"
#include <stdio.h>
#include <stdint.h>

// Three steps on one card. The scrim stops short of the safety bar: this card
// starts the motion, so HOLD and RESET must stay one tap away while it runs.
enum step_t { S_CHECK, S_HOMING, S_TLO };

static lv_obj_t* s_root;
static lv_obj_t* s_card;
static lv_obj_t* s_tlo_btn;
static step_t    s_step;
static state_t   s_painted_state = Disconnected;
static int       s_painted_alarm = 0;
// LATER holds until the next loss, not forever.
static uint32_t  s_dismissed = UINT32_MAX;

static void close_wizard() {
    if (!s_root) return;
    lv_obj_del(s_root);
    s_root = s_card = s_tlo_btn = nullptr;
}

static void show(step_t st);

// The card moves on when the controller reports Homing, not on the tap: a
// refused $H must not leave a card with no buttons.
static void home_cb(lv_event_t*)  { send_line("$H"); }
static void later_cb(lv_event_t*) { s_dismissed = homed_epoch(); close_wizard(); }
static void done_cb(lv_event_t*)  { close_wizard(); }
static void tlo_cb(lv_event_t*) {
    const int m = caps_tlo_macro();
    if (m >= 0 && state == Idle) send_linef("$RM=%s", caps_macro_name(m));
    close_wizard();
}

static lv_obj_t* text(const char* t, const lv_font_t* f, lv_color_t c) {
    lv_obj_t* l = w_label(s_card, t, f, c);
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    return l;
}

static lv_obj_t* button_row() {
    lv_obj_t* row = lv_obj_create(s_card);
    w_flatten(row);
    lv_obj_set_size(row, LV_PCT(100), TOUCH_HOLD);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, U, 0);
    return row;
}

static lv_obj_t* grow(lv_obj_t* b) {
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_height(b, LV_PCT(100));
    return b;
}

static void paint_tlo_gate() {
    if (!s_tlo_btn) return;
    const bool have = caps_tlo_macro() >= 0;
    w_gate(s_tlo_btn, have && state == Idle, have ? "wait: machine busy" : "no TLO macro in config");
}

static void show(step_t st) {
    s_step = st;
    s_tlo_btn = nullptr;
    lv_obj_clean(s_card);

    if (st == S_CHECK) {
        char why[96];
        const char* an = state == Alarm ? fnc_alarm_name(lastAlarm) : nullptr;
        if (an) snprintf(why, sizeof(why), "ALARM %d %s: machine position is lost.", lastAlarm, an);
        else    snprintf(why, sizeof(why), "Machine position is unknown since the controller connected.");
        text("HOMING REQUIRED", &lv_font_montserrat_20, C_WARN);
        text(why, &lv_font_montserrat_14, C_TEXT);
        text("1.  Table clear - clamps and stock below the gantry's path\n"
             "2.  Spindle stopped, wrench out of the collet\n"
             "3.  Hand near the e-stop. Homing moves at full rate.",
             &lv_font_montserrat_14, C_DIM);
        lv_obj_t* row = button_row();
        grow(w_button(row, "HOME ALL", "$H", C_RUN, C_BG, home_cb, nullptr));
        lv_obj_t* later = w_button(row, "LATER", "jog unhomed", C_RAISE, C_DIM, later_cb, nullptr);
        lv_obj_set_size(later, U * 20, LV_PCT(100));
    } else if (st == S_HOMING) {
        text("HOMING", &lv_font_montserrat_20, C_RUN);
        text("Homing in progress. HOLD or RESET below stops it.", &lv_font_montserrat_14, C_TEXT);
    } else {
        char sub[48];
        const int m = caps_tlo_macro();
        snprintf(sub, sizeof(sub), "$RM=%s", m >= 0 ? caps_macro_name(m) : "?");
        text("HOMED", &lv_font_montserrat_20, C_OK);
        text("Optional: measure the tool in the spindle on the toolsetter (TLO).",
             &lv_font_montserrat_14, C_TEXT);
        lv_obj_t* row = button_row();
        s_tlo_btn = grow(w_button(row, "MEASURE TLO", sub, C_ACCENT, C_BG, tlo_cb, nullptr));
        grow(w_button(row, "DONE", "skip", C_RAISE, C_TEXT, done_cb, nullptr));
        paint_tlo_gate();
    }
    s_painted_state = state;
    s_painted_alarm = lastAlarm;
}

static void open_wizard() {
    s_root = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_root);
    lv_obj_set_pos(s_root, 0, 0);
    lv_obj_set_size(s_root, SCREEN_W, SCREEN_H - SAFETY_H);
    lv_obj_set_style_bg_color(s_root, C_BG, 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_80, 0);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_CLICKABLE);   // swallow taps; no tap-off close

    s_card = w_panel(s_root, C_SURFACE);
    lv_obj_set_size(s_card, CONTENT_W - U * 8, LV_SIZE_CONTENT);
    lv_obj_center(s_card);
    lv_obj_set_style_pad_all(s_card, U * 2, 0);
    lv_obj_set_style_pad_row(s_card, U, 0);
    lv_obj_set_flex_flow(s_card, LV_FLEX_FLOW_COLUMN);
    show(S_CHECK);
}

void home_wizard_tick() {
    if (!s_root) {
        // Waits for the config dump: no prompt before we know this machine
        // has a homing cycle, none at all on one that doesn't.
        if (!homed() && (state == Idle || state == Alarm) &&
            caps_homing() == CAP_PRESENT && s_dismissed != homed_epoch() && !splash_active())
            open_wizard();
        return;
    }
    if (state == Disconnected) { close_wizard(); return; }   // reopens on relink

    if (homed() && s_step != S_TLO)               show(S_TLO);   // homed here or elsewhere
    else if (s_step == S_HOMING && state == Alarm) show(S_CHECK); // homing failed: say why
    else if (s_step == S_CHECK && state == Homing) show(S_HOMING);
    else if (s_step == S_CHECK && (s_painted_state != state || s_painted_alarm != lastAlarm))
        show(S_CHECK);                            // the alarm number lands after the state
    else if (s_painted_state != state) {
        paint_tlo_gate();
        s_painted_state = state;
    }
}
