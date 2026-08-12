#include "ui/pages.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "hw/display_hw.h"
#include "model/FluidNCModel.h"
#include "model/settings.h"
#include "transport/transport.h"
#include "hw/imu.h"
#include "model/vibration.h"
#include "GrblParserC.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t* s_bright_lbl;
static lv_obj_t* s_uart_sw;
static lv_obj_t* s_status_lbl;
static lv_obj_t* s_vib_btn[3];
static lv_obj_t* s_vib_chart;
static lv_chart_series_t* s_vib_ser;
static lv_obj_t* s_vib_lbl;

static void bright_cb(lv_event_t* e) {
    int dir = (int)(intptr_t)lv_event_get_user_data(e);
    int b = (int)get_backlight() + dir * 25;
    // Never fully dark: an unreadable panel next to a running spindle is a hazard.
    if (b < 25)  b = 25;
    if (b > 255) b = 255;
    set_backlight((uint8_t)b);
    settings_set_brightness((uint8_t)b);
    char t[8];
    snprintf(t, sizeof(t), "%d", b);
    lv_label_set_text(s_bright_lbl, t);
}

static void uart_sw_cb(lv_event_t* e) {
    settings_set_uart_enabled(lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED));
    transport_settings_changed();
}

// --- sensitivity ------------------------------------------------------------
// Three thresholds, stepped by a percentage rather than a fixed amount: they
// span 0.5g to 50g/s, so one absolute step size cannot suit all three.
enum { VT_HOLD = 0, VT_PEAK, VT_JERK };
static lv_obj_t* s_vt_lbl[3];

static void vt_refresh() {
    char b[16];
    snprintf(b, sizeof(b), "%.2f", (double)settings_vib_hold());
    lv_label_set_text(s_vt_lbl[VT_HOLD], b);
    snprintf(b, sizeof(b), "%.1f", (double)settings_vib_peak());
    lv_label_set_text(s_vt_lbl[VT_PEAK], b);
    snprintf(b, sizeof(b), "%.0f", (double)settings_vib_jerk());
    lv_label_set_text(s_vt_lbl[VT_JERK], b);
}

static void vt_bump_cb(lv_event_t* e) {
    int   code = (int)(intptr_t)lv_event_get_user_data(e);
    int   which = code >> 1;
    float k = (code & 1) ? 1.1f : 1.0f / 1.1f;   // +/-10% per press

    float h = settings_vib_hold(), p = settings_vib_peak(), j = settings_vib_jerk();
    if      (which == VT_HOLD) h *= k;
    else if (which == VT_PEAK) p *= k;
    else                       j *= k;
    settings_set_vib_thresholds(h, p, j);

    // Push straight through to the analyser so the change is live, not pending a
    // reboot, and rebin the histogram because its bin width tracks the jerk
    // threshold — old counts would be plotted against the wrong scale.
    vib_set_thresholds(settings_vib_hold(), settings_vib_peak(), settings_vib_jerk());
    vib_hist_reset();
    vt_refresh();
}

static lv_obj_t* vt_row(lv_obj_t* parent, const char* name, const char* unit, int which) {
    lv_obj_t* row = w_panel(parent, C_SURFACE);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_flex_grow(row, 1);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(row, U + 4, 0);

    lv_obj_t* col = lv_obj_create(row);
    w_flatten(col);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_height(col, LV_PCT(100));
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    w_label(col, name, &lv_font_montserrat_14, C_TEXT);
    w_label(col, unit, &lv_font_montserrat_10, C_DIM);

    lv_obj_t* dn = w_button(row, "-", nullptr, C_RAISE, C_ACCENT,
                            vt_bump_cb, (void*)(intptr_t)((which << 1) | 0));
    lv_obj_set_size(dn, U * 7, U * 6);
    s_vt_lbl[which] = w_label(row, "0", &lv_font_montserrat_16, C_TEXT);
    lv_obj_set_width(s_vt_lbl[which], U * 8);
    lv_obj_set_style_text_align(s_vt_lbl[which], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t* up = w_button(row, "+", nullptr, C_RAISE, C_ACCENT,
                            vt_bump_cb, (void*)(intptr_t)((which << 1) | 1));
    lv_obj_set_size(up, U * 7, U * 6);
    lv_obj_set_style_pad_right(row, U + 4, 0);
    return row;
}

// SOFT and HARD are only real when a sensor is answering. Tinting the selected
// mode amber with no IMU present says "selected but not protecting" — an armed
// look with nothing behind it is the wrong failure mode for a guard whose job is
// stopping a moving machine.
static void vib_mode_paint() {
    uint8_t m    = settings_vib_mode();
    bool    live = imu_present();
    for (int i = 0; i < 3; i++) {
        bool sel = (i == m);
        lv_obj_set_style_bg_color(s_vib_btn[i],
            sel ? (live || m == 0 ? C_ACCENT : C_WARN) : C_RAISE, 0);
    }
}

static void vib_mode_cb(lv_event_t* e) {
    uint8_t m = (uint8_t)(intptr_t)lv_event_get_user_data(e);
    settings_set_vib_mode(m);
    vib_mode_paint();
}

// One-shot probe of both address straps. Also re-applies the saved thresholds:
// a sensor found now starts a fresh calibration, and it should run against this
// machine's tuning rather than whatever the analyser was last handed.
static void imu_search_cb(lv_event_t*) {
    if (imu_retry()) {
        vib_set_thresholds(settings_vib_hold(), settings_vib_peak(),
                           settings_vib_jerk());
        vib_hist_reset();
    }
    vib_mode_paint();
}

static void restart_ctrl_cb(lv_event_t*) { send_line("$Bye"); }

static lv_obj_t* toggle_row(lv_obj_t* parent, const char* name, const char* sub,
                            bool on, lv_event_cb_t cb, lv_obj_t** sw_out) {
    // Grows with the column. Fixed at 56px these stacked at the top of the
    // taller page; sharing the height keeps the switch rows evenly spaced.
    lv_obj_t* row = w_panel(parent, C_SURFACE);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_flex_grow(row, 1);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(row, U + 4, 0);

    lv_obj_t* col = lv_obj_create(row);
    w_flatten(col);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_height(col, LV_PCT(100));
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    w_label(col, name, &lv_font_montserrat_16, C_TEXT);
    if (sub) w_label(col, sub, &lv_font_montserrat_10, C_DIM);

    lv_obj_t* sw = lv_switch_create(row);
    lv_obj_set_size(sw, U * 9, U * 5);
    lv_obj_set_style_pad_right(row, U + 4, 0);
    lv_obj_set_style_bg_color(sw, C_RAISE, 0);
    lv_obj_set_style_bg_color(sw, C_ACCENT, LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (on) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, nullptr);
    *sw_out = sw;
    return row;
}

void page_setup_build(lv_obj_t* p) {
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(p, U, 0);

    // --- left: transports ---
    lv_obj_t* left = lv_obj_create(p);
    w_flatten(left);
    lv_obj_set_size(left, U * 44, LV_PCT(100));
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(left, U, 0);

    toggle_row(left, "Wired UART", "UART terminal, auto 1M or 115K",
               settings_uart_enabled(), uart_sw_cb, &s_uart_sw);
    lv_obj_t* st = w_panel(left, C_RAISE);
    lv_obj_set_size(st, LV_PCT(100), U * 7);
    lv_obj_t* stk = w_label(st, "LINK", &lv_font_montserrat_10, C_DIM);
    lv_obj_align(stk, LV_ALIGN_TOP_LEFT, U + 4, 5);
    s_status_lbl = w_label(st, "no link", &lv_font_montserrat_14, C_TEXT);
    lv_obj_align(s_status_lbl, LV_ALIGN_BOTTOM_LEFT, U + 4, -6);

    // --- right: credentials, brightness, restart ---
    lv_obj_t* right = lv_obj_create(p);
    w_flatten(right);
    lv_obj_set_flex_grow(right, 1);
    lv_obj_set_height(right, LV_PCT(100));
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(right, U, 0);

    lv_obj_t* row = w_panel(right, C_SURFACE);
    lv_obj_set_size(row, LV_PCT(100), U * 7);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(row, U + 4, 0);
    lv_obj_t* nm = w_label(row, "Brightness", &lv_font_montserrat_16, C_TEXT);
    lv_obj_set_flex_grow(nm, 1);
    lv_obj_t* dn = w_button(row, "-", nullptr, C_RAISE, C_ACCENT, bright_cb, (void*)(intptr_t)-1);
    lv_obj_set_size(dn, U * 7, U * 6);
    char b0[8];
    snprintf(b0, sizeof(b0), "%d", (int)settings_brightness());
    s_bright_lbl = w_label(row, b0, &lv_font_montserrat_16, C_TEXT);
    lv_obj_set_width(s_bright_lbl, U * 7);
    lv_obj_set_style_text_align(s_bright_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t* up = w_button(row, "+", nullptr, C_RAISE, C_ACCENT, bright_cb, (void*)(intptr_t)1);
    lv_obj_set_size(up, U * 7, U * 6);

    lv_obj_t* rst = w_button(right, "RESTART CONTROLLER", "$Bye", C_RAISE, C_WARN,
                             restart_ctrl_cb, nullptr);
    lv_obj_set_size(rst, LV_PCT(100), U * 6);

    // --- vibration sensitivity ---
    w_label(right, "SENSITIVITY", &lv_font_montserrat_10, C_DIM);
    vt_row(right, "Sustained",  "g RMS -> feed hold", VT_HOLD);
    vt_row(right, "Impact",     "g peak -> e-stop",   VT_PEAK);
    vt_row(right, "Jerk",       "g/s -> e-stop",      VT_JERK);
    vt_refresh();

    // --- vibration guard ---
    lv_obj_t* vrow = lv_obj_create(left);
    w_flatten(vrow);
    lv_obj_set_size(vrow, LV_PCT(100), U * 6);
    lv_obj_set_flex_flow(vrow, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(vrow, U, 0);
    static const char* VM[3]  = { "MONITOR", "SOFT", "HARD" };
    static const char* VS[3]  = { "watch only", "feed hold", "hold + reset" };
    uint8_t cur = settings_vib_mode();
    for (int i = 0; i < 3; i++) {
        s_vib_btn[i] = w_button(vrow, VM[i], VS[i],
                                i == cur ? C_ACCENT : C_RAISE,
                                i == cur ? C_BG : C_TEXT,
                                vib_mode_cb, (void*)(intptr_t)i);
        lv_obj_set_flex_grow(s_vib_btn[i], 1);
        lv_obj_set_height(s_vib_btn[i], LV_PCT(100));
    }
    // Narrower than a mode button so it does not read as a fourth mode.
    lv_obj_t* srch = w_button(vrow, "SEARCH", "find IMU", C_SURFACE, C_ACCENT,
                              imu_search_cb, nullptr);
    lv_obj_set_size(srch, U * 12, LV_PCT(100));
    vib_mode_paint();   // one owner of the armed/unarmed colours

    lv_obj_t* vst = w_panel(left, C_RAISE);
    // Last in the column, so it takes the remainder and the stack ends flush.
    lv_obj_set_width(vst, LV_PCT(100));
    lv_obj_set_flex_grow(vst, 1);
    lv_obj_t* vk = w_label(vst, "JERK HISTOGRAM  g/s", &lv_font_montserrat_10, C_DIM);
    lv_obj_align(vk, LV_ALIGN_TOP_LEFT, U + 4, 4);
    s_vib_lbl = w_label(vst, "no sensor", &lv_font_montserrat_12, C_DIM);
    lv_obj_align(s_vib_lbl, LV_ALIGN_BOTTOM_LEFT, U + 4, -5);

    // lv_chart in bar mode rather than a hand-drawn canvas: LVGL already does
    // the layout, scaling and redraw, and it costs one widget.
    s_vib_chart = lv_chart_create(vst);
    lv_obj_set_size(s_vib_chart, LV_PCT(100) - U * 4, U * 9);
    lv_obj_align(s_vib_chart, LV_ALIGN_CENTER, 0, 0);
    lv_chart_set_type(s_vib_chart, LV_CHART_TYPE_BAR);
    lv_chart_set_point_count(s_vib_chart, VIB_HIST_BINS);
    lv_chart_set_range(s_vib_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 1);
    lv_chart_set_div_line_count(s_vib_chart, 0, 0);
    lv_obj_set_style_bg_opa(s_vib_chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_vib_chart, 0, 0);
    lv_obj_set_style_pad_all(s_vib_chart, 0, 0);
    lv_obj_set_style_pad_column(s_vib_chart, 2, 0);
    s_vib_ser = lv_chart_add_series(s_vib_chart, C_ACCENT, LV_CHART_AXIS_PRIMARY_Y);

}

void page_setup_tick() {
    lv_label_set_text(s_status_lbl, transport_detail());

    if (!imu_present()) {
        lv_label_set_text(s_vib_lbl,
            settings_vib_mode() == 0 ? "no sensor — press SEARCH"
                                     : "NO SENSOR — guard not active");
        lv_obj_set_style_text_color(s_vib_lbl,
            settings_vib_mode() == 0 ? C_DIM : C_WARN, 0);
        return;
    }
    const vib_result_t* v = vib_result();
    // Smoothed figures first: the raw peak and jerk twitch far faster than
    // anyone can read them, the EMAs are what you actually judge a cut by.
    char b[96];
    snprintf(b, sizeof(b), "rms %.2f~%.2f  peak %.2f~%.2f  jerk %.0f",
             (double)v->rms,  (double)v->smoothed_rms,
             (double)v->peak, (double)v->smoothed_peak,
             (double)v->jerk);
    lv_label_set_text(s_vib_lbl, b);
    lv_obj_set_style_text_color(s_vib_lbl,
        v->level == VIB_ESTOP ? C_CRIT : (v->level == VIB_HOLD ? C_WARN : C_DIM), 0);

    // Rescale to the tallest bin so the shape stays readable whether the machine
    // has produced fifty samples or fifty thousand.
    const uint32_t* h   = vib_hist();
    uint32_t        top = vib_hist_max();
    if (top < 1) top = 1;
    lv_chart_set_range(s_vib_chart, LV_CHART_AXIS_PRIMARY_Y, 0, (lv_coord_t)top);
    for (int i = 0; i < VIB_HIST_BINS; i++) {
        lv_chart_set_value_by_id(s_vib_chart, s_vib_ser, i, (lv_coord_t)h[i]);
    }
}
