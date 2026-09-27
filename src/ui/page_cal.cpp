#include "ui/pages.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "model/FluidNCModel.h"
#include "model/calibration.h"
#include "GrblParserC.h"
#include <stdio.h>

// Steps-per-mm calibration: command a known move, measure it, apply the ratio.
//
// The measured distance is entered with coarse/fine bump keys rather than the
// keyboard. w_keyboard() is a singleton and two pages attaching their own done
// handler to it fight (see docs/HANDOFF.md); avoiding it here costs nothing,
// since the value is always a small nudge away from the commanded distance.

static const char* AXIS_LBL[CAL_AXES] = { "X", "Y", "Z", "A" };
static const char  AXIS_CFG[CAL_AXES] = { 'x', 'y', 'z', 'a' };
static const float DISTANCE[3] = { 10.0f, 50.0f, 100.0f };

static int   s_axis = 0;
static int   s_dist = 0;
static float s_measured = 10.0f;

static lv_obj_t* s_axis_btn[CAL_AXES];
static lv_obj_t* s_dist_btn[3];
static lv_obj_t* s_cur_lbl;
static lv_obj_t* s_meas_lbl;
static lv_obj_t* s_new_lbl;
static lv_obj_t* s_note_lbl;

static float commanded() { return DISTANCE[s_dist]; }

static lv_obj_t* s_move_btn;

static void refresh() {
    char b[40];

    for (int i = 0; i < CAL_AXES; i++)
        lv_obj_set_style_bg_color(s_axis_btn[i], i == s_axis ? C_ACCENT : C_RAISE, 0);
    for (int i = 0; i < 3; i++)
        lv_obj_set_style_bg_color(s_dist_btn[i], i == s_dist ? C_ACCENT : C_RAISE, 0);

    if (cal_have_steps(s_axis)) snprintf(b, sizeof(b), "%.4f", (double)cal_steps(s_axis));
    else                        snprintf(b, sizeof(b), "--  (press READ)");
    lv_label_set_text(s_cur_lbl, b);

    snprintf(b, sizeof(b), "%.2f", (double)s_measured);
    lv_label_set_text(s_meas_lbl, b);

    const float nv = cal_new_steps(cal_steps(s_axis), commanded(), s_measured);
    if (nv > 0.0f) {
        snprintf(b, sizeof(b), "%.4f", (double)nv);
        lv_label_set_text(s_new_lbl, b);
        lv_obj_set_style_text_color(s_new_lbl, C_OK, 0);
    } else {
        lv_label_set_text(s_new_lbl, cal_have_steps(s_axis) ? "invalid" : "--");
        lv_obj_set_style_text_color(s_new_lbl, C_DIM, 0);
    }
}

// Asking with no '=' makes FluidNC echo the current value, which cal_feed_line
// picks up off the normal reply stream.
static void read_cb(lv_event_t*) {
    cal_reset();
    send_linef("$/axes/%c/steps_per_mm", AXIS_CFG[s_axis]);
}

static void axis_cb(lv_event_t* e) {
    s_axis = (int)(intptr_t)lv_event_get_user_data(e);
    s_measured = commanded();          // a fresh axis has not been measured yet
    read_cb(nullptr);
    refresh();
}

static void dist_cb(lv_event_t* e) {
    s_dist = (int)(intptr_t)lv_event_get_user_data(e);
    s_measured = commanded();
    refresh();
}

// G91 for the move, then straight back to G90: leaving the parser in relative
// mode is how the next absolute move from any other page goes somewhere absurd.
static void move_cb(lv_event_t*) {
    send_linef("G91 G0 %s%.3f", AXIS_LBL[s_axis], (double)commanded());
    send_line("G90");
}

static void meas_cb(lv_event_t* e) {
    // user data packs the signed hundredths of a millimetre per press
    s_measured += (float)(intptr_t)lv_event_get_user_data(e) / 100.0f;
    if (s_measured < 0.0f) s_measured = 0.0f;
    refresh();
}

static void apply_cb(lv_event_t*) {
    const float nv = cal_new_steps(cal_steps(s_axis), commanded(), s_measured);
    if (nv <= 0.0f) {
        lv_label_set_text(s_note_lbl, "Nothing to apply - press READ, move, then enter the measurement");
        lv_obj_set_style_text_color(s_note_lbl, C_WARN, 0);
        return;
    }
    send_linef("$/axes/%c/steps_per_mm=%.4f", AXIS_CFG[s_axis], (double)nv);
    read_cb(nullptr);   // read it back rather than assume it took
    lv_label_set_text(s_note_lbl,
        "Applied to the running controller only - NOT saved. Put it in config.yaml or it is gone at reboot.");
    lv_obj_set_style_text_color(s_note_lbl, C_WARN, 0);
}

void page_cal_build(lv_obj_t* p) {
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(p, U, 0);

    // --- row 1: axis, distance, move ---
    lv_obj_t* r1 = lv_obj_create(p);
    w_flatten(r1);
    lv_obj_set_size(r1, LV_PCT(100), U * 8);
    lv_obj_set_flex_flow(r1, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(r1, U, 0);

    for (int i = 0; i < CAL_AXES; i++) {
        s_axis_btn[i] = w_button(r1, AXIS_LBL[i], nullptr, C_RAISE, C_TEXT,
                                 axis_cb, (void*)(intptr_t)i);
        lv_obj_set_size(s_axis_btn[i], U * 8, LV_PCT(100));
    }
    static const char* DLBL[3] = { "10", "50", "100" };
    for (int i = 0; i < 3; i++) {
        s_dist_btn[i] = w_button(r1, DLBL[i], "mm", C_RAISE, C_TEXT,
                                 dist_cb, (void*)(intptr_t)i);
        lv_obj_set_size(s_dist_btn[i], U * 10, LV_PCT(100));
    }
    lv_obj_t* rd = w_button(r1, "READ", "current", C_SURFACE, C_ACCENT, read_cb, nullptr);
    lv_obj_set_size(rd, U * 12, LV_PCT(100));
    lv_obj_t* mv = s_move_btn = w_button(r1, "MOVE", "G91 G0", C_ACCENT, C_BG, move_cb, nullptr);
    lv_obj_set_flex_grow(mv, 1);
    lv_obj_set_height(mv, LV_PCT(100));

    // --- row 2: measured, entered with coarse/fine keys ---
    lv_obj_t* r2 = w_panel(p, C_SURFACE);
    lv_obj_set_size(r2, LV_PCT(100), U * 9);
    lv_obj_set_flex_flow(r2, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r2, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(r2, U + 4, 0);
    lv_obj_set_style_pad_column(r2, 4, 0);

    lv_obj_t* mlbl = w_label(r2, "Measured", &lv_font_montserrat_16, C_TEXT);
    lv_obj_set_flex_grow(mlbl, 1);

    struct { const char* t; int hundredths; } BUMP[6] = {
        { "-1",    -100 }, { "-.1", -10 }, { "-.01", -1 },
        { "+.01",     1 }, { "+.1",  10 }, { "+1",  100 },
    };
    for (int i = 0; i < 3; i++) {
        lv_obj_t* b = w_button(r2, BUMP[i].t, nullptr, C_RAISE, C_ACCENT,
                               meas_cb, (void*)(intptr_t)BUMP[i].hundredths);
        lv_obj_set_size(b, U * 7, U * 7);
    }
    s_meas_lbl = w_label(r2, "10.00", &lv_font_montserrat_28, C_TEXT);
    lv_obj_set_width(s_meas_lbl, U * 14);
    lv_obj_set_style_text_align(s_meas_lbl, LV_TEXT_ALIGN_CENTER, 0);
    for (int i = 3; i < 6; i++) {
        lv_obj_t* b = w_button(r2, BUMP[i].t, nullptr, C_RAISE, C_ACCENT,
                               meas_cb, (void*)(intptr_t)BUMP[i].hundredths);
        lv_obj_set_size(b, U * 7, U * 7);
    }
    lv_obj_set_style_pad_right(r2, U + 4, 0);

    // --- row 3: current -> new, and apply ---
    lv_obj_t* r3 = lv_obj_create(p);
    w_flatten(r3);
    lv_obj_set_width(r3, LV_PCT(100));
    lv_obj_set_flex_grow(r3, 1);
    lv_obj_set_flex_flow(r3, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(r3, U, 0);

    lv_obj_t* cur = w_panel(r3, C_SURFACE);
    lv_obj_set_flex_grow(cur, 1);
    lv_obj_set_height(cur, LV_PCT(100));
    lv_obj_t* ck = w_label(cur, "CURRENT steps/mm", &lv_font_montserrat_10, C_DIM);
    lv_obj_align(ck, LV_ALIGN_TOP_LEFT, U, 6);
    s_cur_lbl = w_label(cur, "--", &lv_font_montserrat_28, C_TEXT);
    lv_obj_align(s_cur_lbl, LV_ALIGN_LEFT_MID, U, 6);

    lv_obj_t* nw = w_panel(r3, C_SURFACE);
    lv_obj_set_flex_grow(nw, 1);
    lv_obj_set_height(nw, LV_PCT(100));
    lv_obj_t* nk = w_label(nw, "NEW steps/mm", &lv_font_montserrat_10, C_DIM);
    lv_obj_align(nk, LV_ALIGN_TOP_LEFT, U, 6);
    s_new_lbl = w_label(nw, "--", &lv_font_montserrat_28, C_DIM);
    lv_obj_align(s_new_lbl, LV_ALIGN_LEFT_MID, U, 6);

    lv_obj_t* ap = w_button(r3, "APPLY", "$/axes/../steps_per_mm", C_WARN, C_BG,
                            apply_cb, nullptr);
    lv_obj_set_size(ap, U * 22, LV_PCT(100));

    s_note_lbl = w_label(p, "Runtime only: FluidNC does not persist this. Copy it into config.yaml.",
                         &lv_font_montserrat_10, C_DIM);

    refresh();
}

void page_cal_tick() {
    // MOVE is motion, refused in Alarm; READ and APPLY are $ settings and
    // stay usable.
    static int was_locked = -1;
    const bool locked = w_locked();
    if (locked != was_locked) {
        was_locked = locked;
        w_gate(s_move_btn, !locked, LOCK_REASON);
    }
    // cal_feed_line() fills in behind us off the reply stream, so the display
    // only becomes correct on the tick after READ.
    static bool s_had;
    bool have = cal_have_steps(s_axis);
    if (have != s_had) {
        s_had = have;
        refresh();
    }
}
