#include "ui/pages.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "hw/display_hw.h"
#include "model/FluidNCModel.h"
#include "model/settings.h"
#include "transport/transport.h"
#include "hw/imu.h"
#include "hw/fw_update.h"
#include "model/vibration.h"
#include "GrblParserC.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t* s_bright_lbl;
static lv_obj_t* s_uart_sw;
static lv_obj_t* s_status_lbl;
static lv_obj_t* s_vib_btn[3];
// The jerk histogram chart is gone. model/vibration.cpp still accumulates the
// bins and still has its tests; nothing displays them now. Left in place rather
// than ripped out, because the data is cheap to keep and the next person who
// wants a distribution will want it back.
static lv_obj_t* s_vib_lbl;

// Shown as a percentage. The hardware is 0-255 and stays that way in NVS and in
// set_backlight(); only the display and the step are in human units, because
// "180" means nothing to anyone standing at the machine.
static const int BRIGHT_MIN_PCT = 10;   // never fully dark - see below
static const int BRIGHT_STEP_PCT = 5;

static int bright_pct() { return ((int)get_backlight() * 100 + 127) / 255; }

static void bright_cb(lv_event_t* e) {
    int dir = (int)(intptr_t)lv_event_get_user_data(e);
    int pct = bright_pct() + dir * BRIGHT_STEP_PCT;
    // Round to the step so the first press off an odd stored value lands tidily.
    pct = (pct / BRIGHT_STEP_PCT) * BRIGHT_STEP_PCT;
    // Never fully dark: an unreadable panel next to a running spindle is a hazard.
    if (pct < BRIGHT_MIN_PCT) pct = BRIGHT_MIN_PCT;
    if (pct > 100)            pct = 100;

    const uint8_t level = (uint8_t)((pct * 255 + 50) / 100);
    set_backlight(level);
    settings_set_brightness(level);
    char t[8];
    snprintf(t, sizeof(t), "%d%%", pct);
    lv_label_set_text(s_bright_lbl, t);
}

static lv_obj_t* s_scroff_lbl;

static void scroff_fmt(char* b, size_t n) {
    uint8_t m = settings_screen_off_min();
    if (m == 0) snprintf(b, n, "Off");
    else        snprintf(b, n, "%u min", (unsigned)m);
}

static void scroff_cb(lv_event_t* e) {
    int dir = (int)(intptr_t)lv_event_get_user_data(e);
    int m = (int)settings_screen_off_min() + dir * 5;
    if (m < 0)  m = 0;
    if (m > 60) m = 60;
    settings_set_screen_off_min((uint8_t)m);
    char b[12];
    scroff_fmt(b, sizeof(b));
    lv_label_set_text(s_scroff_lbl, b);
}

static void led_sw_cb(lv_event_t* e) {
    settings_set_status_led(lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED));
}

static void blankmd_sw_cb(lv_event_t* e) {
    settings_set_blank_disables_motors(lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED));
}

static void uart_sw_cb(lv_event_t* e) {
    settings_set_uart_enabled(lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED));
    transport_settings_changed();
}

// --- sensitivity ------------------------------------------------------------
// Three thresholds, stepped by a percentage rather than a fixed amount: they
// span 0.5g to 50g/s, so one absolute step size cannot suit all three.
enum { VT_HOLD = 0, VT_PEAK, VT_JERK };
static lv_obj_t* s_vt_thr[3];   // the threshold, under the name
static lv_obj_t* s_vt_val[3];   // the live reading, right of the bar
static lv_obj_t* s_vt_bar[3];

// The bar shows the live reading measured against its own threshold, not
// against the setting's absolute range. A cut at 0.5g means nothing on a 0-5g
// scale; what matters is how much of the margin is used up. The threshold
// therefore sits at a FIXED fraction of every bar, so all three read the same
// way and the space past the tick is the overshoot you are not supposed to see.
static const int BAR_RANGE   = 1000;
static const int BAR_THR_PCT = 70;

static void vt_refresh() {
    char b[20];
    snprintf(b, sizeof(b), "thr %.2f g",   (double)settings_vib_hold()); lv_label_set_text(s_vt_thr[VT_HOLD], b);
    snprintf(b, sizeof(b), "thr %.1f g",   (double)settings_vib_peak()); lv_label_set_text(s_vt_thr[VT_PEAK], b);
    snprintf(b, sizeof(b), "thr %.0f g/s", (double)settings_vib_jerk()); lv_label_set_text(s_vt_thr[VT_JERK], b);
}

// Live values into the bars. Called from the tick, so it must stay cheap.
static void vt_bars_update(bool live) {
    const vib_result_t* v = live ? vib_result() : nullptr;
    // Smoothed figures, not raw: the raw peak and jerk twitch far faster than
    // anyone can read, and a bar that blurs is worse than no bar.
    const float reading[3] = {
        v ? v->smoothed_rms : 0.0f, v ? v->smoothed_peak : 0.0f, v ? v->jerk : 0.0f
    };
    const float thr[3] = {
        settings_vib_hold(), settings_vib_peak(), settings_vib_jerk()
    };

    for (int i = 0; i < 3; i++) {
        const float ratio = thr[i] > 0.0f ? reading[i] / thr[i] : 0.0f;
        int fill = (int)(ratio * BAR_THR_PCT * (BAR_RANGE / 100.0f));
        if (fill < 0)         fill = 0;
        if (fill > BAR_RANGE) fill = BAR_RANGE;
        lv_bar_set_value(s_vt_bar[i], fill, LV_ANIM_OFF);

        // Amber before the tick, not at it: a guard you only notice once it has
        // already fired is a guard you cannot tune.
        lv_obj_set_style_bg_color(s_vt_bar[i],
            !live      ? C_LINE :
            ratio >= 1.0f ? C_CRIT :
            ratio >= 0.75f ? C_WARN : C_OK,
            LV_PART_INDICATOR);

        char b[16];
        if (!live)      snprintf(b, sizeof(b), "--");
        else if (i == VT_JERK) snprintf(b, sizeof(b), "%.0f", (double)reading[i]);
        else                   snprintf(b, sizeof(b), "%.2f", (double)reading[i]);
        lv_label_set_text(s_vt_val[i], b);
        lv_obj_set_style_text_color(s_vt_val[i], live ? C_TEXT : C_DIM, 0);
    }
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

// name / threshold | bar with a fixed threshold tick | live value | - | +
static lv_obj_t* vt_row(lv_obj_t* parent, const char* name, int which) {
    lv_obj_t* row = w_panel(parent, C_SURFACE);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_flex_grow(row, 1);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(row, U + 4, 0);
    lv_obj_set_style_pad_right(row, U + 4, 0);
    lv_obj_set_style_pad_column(row, 4, 0);

    lv_obj_t* col = lv_obj_create(row);
    w_flatten(col);
    lv_obj_set_size(col, U * 11, LV_PCT(100));
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    w_label(col, name, &lv_font_montserrat_14, C_TEXT);
    s_vt_thr[which] = w_label(col, "thr", &lv_font_montserrat_10, C_DIM);

    // The tick is a sibling of the bar rather than a style on it, because LVGL
    // bars have no marker of their own and drawing one costs a custom draw hook.
    lv_obj_t* holder = lv_obj_create(row);
    w_flatten(holder);
    lv_obj_set_flex_grow(holder, 1);
    lv_obj_set_height(holder, LV_PCT(100));

    lv_obj_t* bar = lv_bar_create(holder);
    lv_obj_set_size(bar, LV_PCT(100), U * 3);
    lv_obj_align(bar, LV_ALIGN_LEFT_MID, 0, 0);
    lv_bar_set_range(bar, 0, BAR_RANGE);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, C_BG, 0);
    lv_obj_set_style_bg_color(bar, C_OK, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 2, 0);
    lv_obj_set_style_radius(bar, 2, LV_PART_INDICATOR);
    s_vt_bar[which] = bar;

    lv_obj_t* tick = lv_obj_create(holder);
    w_flatten(tick);
    lv_obj_set_size(tick, 2, U * 5);
    lv_obj_set_style_bg_color(tick, C_TEXT, 0);
    lv_obj_set_style_bg_opa(tick, LV_OPA_COVER, 0);
    lv_obj_align(tick, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_x(tick, lv_pct(BAR_THR_PCT));

    s_vt_val[which] = w_label(row, "--", &lv_font_montserrat_16, C_TEXT);
    lv_obj_set_width(s_vt_val[which], U * 7);
    lv_obj_set_style_text_align(s_vt_val[which], LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t* dn = w_button(row, "-", nullptr, C_RAISE, C_ACCENT,
                            vt_bump_cb, (void*)(intptr_t)((which << 1) | 0));
    lv_obj_set_size(dn, U * 6, U * 6);
    lv_obj_t* up = w_button(row, "+", nullptr, C_RAISE, C_ACCENT,
                            vt_bump_cb, (void*)(intptr_t)((which << 1) | 1));
    lv_obj_set_size(up, U * 6, U * 6);
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

// The pages an operator can turn off. JOG and RUN are the job; SETUP and DIAG
// are how the panel gets fixed. Neither set is negotiable.
static const int   PAGE_OPT_ID[]    = { PAGE_HOME, PAGE_PROBE, PAGE_MACRO, PAGE_OFFSETS, PAGE_CAL };
static const char* PAGE_OPT_LABEL[] = { "Home",    "Probe",    "Macro",    "Offsets",    "Calibrate" };
static const int   PAGE_OPT_N = (int)(sizeof(PAGE_OPT_ID) / sizeof(PAGE_OPT_ID[0]));

static void page_cb(lv_event_t* e) {
    lv_obj_t* cb = lv_event_get_target(e);
    const int id = (int)(intptr_t)lv_event_get_user_data(e);
    settings_set_page_enabled(id, lv_obj_has_state(cb, LV_STATE_CHECKED));
    // shell_tick() notices the change on its next pass and rebuilds the rail.
}

static lv_obj_t* toggle_row(lv_obj_t* parent, const char* name, const char* sub,
                            bool on, lv_event_cb_t cb, lv_obj_t** sw_out) {
    // Shares its parent strip with the other switch: stacked, two rows cost
    // the PAGES box below its second line of checkboxes.
    lv_obj_t* row = w_panel(parent, C_SURFACE);
    lv_obj_set_height(row, LV_PCT(100));
    lv_obj_set_flex_grow(row, 1);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(row, U, 0);

    lv_obj_t* col = lv_obj_create(row);
    w_flatten(col);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_height(col, LV_PCT(100));
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    w_label(col, name, &lv_font_montserrat_14, C_TEXT);
    if (sub) w_label(col, sub, &lv_font_montserrat_10, C_DIM);

    lv_obj_t* sw = lv_switch_create(row);
    lv_obj_set_size(sw, U * 6, U * 4);
    lv_obj_set_style_pad_right(row, U, 0);
    lv_obj_set_style_bg_color(sw, C_RAISE, 0);
    lv_obj_set_style_bg_color(sw, C_ACCENT, LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (on) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, nullptr);
    // The whole card is the switch: the slider itself is smaller than the
    // touch floor now that two share a strip.
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, [](lv_event_t* e) {
        lv_obj_t* s = (lv_obj_t*)lv_event_get_user_data(e);
        if (lv_obj_has_state(s, LV_STATE_CHECKED)) lv_obj_clear_state(s, LV_STATE_CHECKED);
        else                                       lv_obj_add_state(s, LV_STATE_CHECKED);
        lv_event_send(s, LV_EVENT_VALUE_CHANGED, nullptr);
    }, LV_EVENT_CLICKED, sw);
    *sw_out = sw;
    return row;
}

// Name on the left, -/+ either side of a value. The callback gets -1 or +1 as
// its user data. Returns the value label so the caller can update it.
static lv_obj_t* bump_row(lv_obj_t* parent, const char* name, const char* init,
                          lv_event_cb_t cb) {
    lv_obj_t* row = w_panel(parent, C_SURFACE);
    lv_obj_set_size(row, LV_PCT(100), U * 7);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(row, U + 4, 0);

    lv_obj_t* nm = w_label(row, name, &lv_font_montserrat_16, C_TEXT);
    lv_obj_set_flex_grow(nm, 1);
    lv_obj_t* dn = w_button(row, "-", nullptr, C_RAISE, C_ACCENT, cb, (void*)(intptr_t)-1);
    lv_obj_set_size(dn, U * 7, U * 6);
    // Wide enough for "60 min", which is the longest thing either row shows.
    lv_obj_t* val = w_label(row, init, &lv_font_montserrat_16, C_TEXT);
    lv_obj_set_width(val, U * 9);
    lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t* up = w_button(row, "+", nullptr, C_RAISE, C_ACCENT, cb, (void*)(intptr_t)1);
    lv_obj_set_size(up, U * 7, U * 6);
    return val;
}

void page_setup_build(lv_obj_t* p) {
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(p, U, 0);

    // --- left: transport ---
    lv_obj_t* left = lv_obj_create(p);
    w_flatten(left);
    lv_obj_set_size(left, U * 44, LV_PCT(100));
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(left, U, 0);

    lv_obj_t* sws = lv_obj_create(left);
    w_flatten(sws);
    lv_obj_set_size(sws, LV_PCT(100), U * 9);
    lv_obj_set_flex_flow(sws, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(sws, U, 0);
    toggle_row(sws, "Wired UART", "off: USB debug",
               settings_uart_enabled(), uart_sw_cb, &s_uart_sw);
    static lv_obj_t* s_led_sw;
    toggle_row(sws, "Status lights", "light bar",
               settings_status_led(), led_sw_cb, &s_led_sw);
    static lv_obj_t* s_blankmd_sw;
    toggle_row(sws, "Blank frees", "steppers $MD",
               settings_blank_disables_motors(), blankmd_sw_cb, &s_blankmd_sw);

    // Rail contents. One switch per optional page, because which pages a
    // machine needs is a property of how it is used, not something the panel
    // should be guessing from a config dump.
    lv_obj_t* pages = w_panel(left, C_SURFACE);
    lv_obj_set_width(pages, LV_PCT(100));
    lv_obj_set_flex_grow(pages, 1);
    lv_obj_set_flex_flow(pages, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(pages, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(pages, U / 2, 0);
    lv_obj_set_style_pad_column(pages, U / 2, 0);
    lv_obj_set_style_pad_row(pages, U / 2, 0);
    lv_obj_t* ptitle = w_label(pages, "PAGES", &lv_font_montserrat_10, C_DIM);
    lv_obj_set_width(ptitle, LV_PCT(100));

    for (int i = 0; i < PAGE_OPT_N; i++) {
        lv_obj_t* cb = lv_checkbox_create(pages);
        lv_checkbox_set_text(cb, PAGE_OPT_LABEL[i]);
        lv_obj_set_style_text_font(cb, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(cb, C_TEXT, 0);
        lv_obj_set_style_bg_color(cb, C_RAISE, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(cb, C_ACCENT, LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_set_style_border_color(cb, C_LINE, LV_PART_INDICATOR);
        lv_obj_set_style_pad_all(cb, U / 2, 0);
        if (settings_page_enabled(PAGE_OPT_ID[i])) lv_obj_add_state(cb, LV_STATE_CHECKED);
        lv_obj_add_event_cb(cb, page_cb, LV_EVENT_VALUE_CHANGED, (void*)(intptr_t)PAGE_OPT_ID[i]);
    }
    lv_obj_t* st = w_panel(left, C_RAISE);
    lv_obj_set_size(st, LV_PCT(100), U * 7);
    lv_obj_t* stk = w_label(st, "LINK", &lv_font_montserrat_10, C_DIM);
    lv_obj_align(stk, LV_ALIGN_TOP_LEFT, U + 4, 5);
#ifndef FW_TAG
#define FW_TAG "dev"    // set from git by platformio.ini
#endif
    lv_obj_t* fw = w_label(st, "fw " FW_TAG, &lv_font_montserrat_10, C_DIM);
    lv_obj_align(fw, LV_ALIGN_TOP_RIGHT, -(U + 4), 5);
    s_status_lbl = w_label(st, "no link", &lv_font_montserrat_14, C_TEXT);
    lv_obj_align(s_status_lbl, LV_ALIGN_BOTTOM_LEFT, U + 4, -6);

    // --- right: credentials, brightness, restart ---
    lv_obj_t* right = lv_obj_create(p);
    w_flatten(right);
    lv_obj_set_flex_grow(right, 1);
    lv_obj_set_height(right, LV_PCT(100));
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(right, U, 0);

    char b0[12];
    snprintf(b0, sizeof(b0), "%d%%", ((int)settings_brightness() * 100 + 127) / 255);
    s_bright_lbl = bump_row(right, "Brightness", b0, bright_cb);

    char s0[12];
    scroff_fmt(s0, sizeof(s0));
    s_scroff_lbl = bump_row(right, "Screen off", s0, scroff_cb);

    lv_obj_t* sys = lv_obj_create(right);
    w_flatten(sys);
    lv_obj_set_size(sys, LV_PCT(100), U * 6);
    lv_obj_set_flex_flow(sys, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(sys, U, 0);
    lv_obj_t* rst = w_button(sys, "RESTART CTRL", "$Bye", C_RAISE, C_WARN,
                             restart_ctrl_cb, nullptr);
    lv_obj_set_flex_grow(rst, 1);
    lv_obj_set_height(rst, LV_PCT(100));
    // Firmware from the repo: WiFi up, fetch, flash, WiFi off, reboot.
    // Creds: NVS or the built-in defaults (wifi.txt too, in a HAS_SD_CARD build).
    // Plain tap: long-press proved unreliable on the glass, and the update
    // modal itself arms with a tap-to-cancel countdown.
    lv_obj_t* net = w_button(sys, "UPDATE NET", "wifi", C_RAISE, C_WARN,
                             [](lv_event_t*) { fwupd_net(); }, nullptr);
    lv_obj_set_flex_grow(net, 1);
    lv_obj_set_height(net, LV_PCT(100));

    // --- vibration sensitivity ---
    w_label(right, "SENSITIVITY   live reading vs threshold", &lv_font_montserrat_10, C_DIM);
    vt_row(right, "Sustained", VT_HOLD);
    vt_row(right, "Impact",    VT_PEAK);
    vt_row(right, "Jerk",      VT_JERK);
    vt_refresh();
    vt_bars_update(false);

    // --- vibration guard: 2x2 rather than a row of four ---
    // The histogram that used to sit under this is gone. The bars on the right
    // say the same thing about the cut happening now, and say it continuously.
    w_label(left, "VIBRATION GUARD", &lv_font_montserrat_10, C_DIM);
    lv_obj_t* vgrid = lv_obj_create(left);
    w_flatten(vgrid);
    lv_obj_set_width(vgrid, LV_PCT(100));
    lv_obj_set_flex_grow(vgrid, 1);
    static lv_coord_t vc[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
    static lv_coord_t vr[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
    lv_obj_set_grid_dsc_array(vgrid, vc, vr);
    lv_obj_set_layout(vgrid, LV_LAYOUT_GRID);
    lv_obj_set_style_pad_column(vgrid, U, 0);
    lv_obj_set_style_pad_row(vgrid, U, 0);

    static const char* VM[3] = { "MONITOR", "SOFT", "HARD" };
    static const char* VS[3] = { "watch only", "feed hold", "hold + reset" };
    for (int i = 0; i < 3; i++) {
        s_vib_btn[i] = w_button(vgrid, VM[i], VS[i], C_RAISE, C_TEXT,
                                vib_mode_cb, (void*)(intptr_t)i);
        lv_obj_set_grid_cell(s_vib_btn[i], LV_GRID_ALIGN_STRETCH, i % 2, 1,
                                           LV_GRID_ALIGN_STRETCH, i / 2, 1);
    }
    // Fourth cell, and deliberately not styled like a mode: it does something
    // once rather than selecting a state.
    lv_obj_t* srch = w_button(vgrid, "SEARCH", "find IMU", C_SURFACE, C_ACCENT,
                              imu_search_cb, nullptr);
    lv_obj_set_grid_cell(srch, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
    vib_mode_paint();   // one owner of the armed/unarmed colours

    s_vib_lbl = w_label(left, "no sensor", &lv_font_montserrat_12, C_DIM);
}

void page_setup_tick() {
    lv_label_set_text(s_status_lbl, transport_detail());

    if (!imu_present()) {
        lv_label_set_text(s_vib_lbl,
            settings_vib_mode() == 0 ? "no sensor - press SEARCH"
                                     : "NO SENSOR - guard not active");
        lv_obj_set_style_text_color(s_vib_lbl,
            settings_vib_mode() == 0 ? C_DIM : C_WARN, 0);
        vt_bars_update(false);   // grey and dashed, not a confident zero
        return;
    }

    vt_bars_update(true);

    const vib_result_t* v = vib_result();
    // The raw figures still get a line: the bars carry the smoothed values, and
    // seeing raw next to smoothed is how you tell a real impact from an EMA
    // still catching up.
    char b[96];
    snprintf(b, sizeof(b), "raw  rms %.2f  peak %.2f  jerk %.0f",
             (double)v->rms, (double)v->peak, (double)v->jerk);
    lv_label_set_text(s_vib_lbl, b);
    lv_obj_set_style_text_color(s_vib_lbl,
        v->level == VIB_ESTOP ? C_CRIT : (v->level == VIB_HOLD ? C_WARN : C_DIM), 0);
}
