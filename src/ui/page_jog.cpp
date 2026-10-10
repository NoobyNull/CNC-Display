#include "ui/pages.h"
#include "ui/shell.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "model/motors.h"
#include "model/FluidNCModel.h"
#include "model/fnc_settings.h"
#include "model/settings.h"
#include "model/caps.h"
#include "ui/goto_popup.h"
#include "GrblParserC.h"
#include <Arduino.h>
#include <stdio.h>

// The controls fill the page height rather than a fixed 240px. That constant
// exactly filled the CrowPanel's 256px content area and left 120px of dead space
// once the 7B's content area grew to 376. Everything on this page is squared or
// split off PAD, so deriving it here reflows the whole layout.
static const lv_coord_t PAD    = CONTENT_H - U * 2;
static const lv_coord_t ZCOL_W = U * 11;   // 88

// The -/+ ladder. Not the set of legal sizes: tapping the value types any
// number, and the ladder is only what the buttons walk through.
static const float STEPS[] = { 0.001f, 0.01f, 0.1f, 1.0f, 10.0f, 100.0f };
static const int   STEPS_N = (int)(sizeof(STEPS) / sizeof(STEPS[0]));
// The step is a VALUE, not an index into the ladder — a custom size typed on
// the keypad is not on the ladder at all, and every safety test below has to
// ask how far the axis will move rather than which button is lit.
static float s_step_mm = 1.0f;
static lv_obj_t* s_step_dn;
static lv_obj_t* s_step_up;
static lv_obj_t* s_mid_lbl;
static lv_obj_t* s_step_val;
static lv_obj_t* s_step_note;

// Continuous jog is FluidDial's idiom (MultiJogScene::start_button_jog): one
// long move -- a fixed distance far beyond any travel -- ended by JogCancel on
// release, at whatever continuous_feed() says below.
//
// History: this used to re-issue 150ms chunks instead, because early bring-up
// saw the 0x85 JogCancel byte ignored by FluidNC v4.0.4. That testing was done
// over the WiFi link; wired UART at 1Mbaud delivers realtime bytes reliably,
// which is the likely difference. Belt and braces stays anyway: the cancel is
// resent every CANCEL_RESEND_MS until the state actually leaves Jog (FluidDial
// does exactly this), so one lost byte cannot run an axis to the end of travel.
static const float    CONT_DIST_MM     = 5000.0f;  // FluidDial's button-jog distance
static const uint32_t CANCEL_RESEND_MS = 80;       // all three mirror MultiJogScene
static const uint32_t CANCEL_MIN_MS    = 250;
static const uint32_t CANCEL_MAX_MS    = 1500;

struct dir_t { int dx, dy, dz; };

static bool        s_jogging      = false;    // a continuous session is live
static lv_timer_t* s_cancel_timer = nullptr;  // the resend pump
static uint32_t    s_cancel_t0    = 0;

// Pad slots in reading order; slot 4 is the centre and is not a button.
static const dir_t PADDIR[9] = {
    { -1,  1, 0 }, { 0,  1, 0 }, { 1,  1, 0 },
    { -1,  0, 0 }, { 0,  0, 0 }, { 1,  0, 0 },
    { -1, -1, 0 }, { 0, -1, 0 }, { 1, -1, 0 },
};
static const dir_t ZUP   = { 0, 0,  1 };
static const dir_t ZDOWN = { 0, 0, -1 };

static void jog_send(const dir_t* d, float feed_mm_min, float dist_mm) {
    char buf[80];
    int n = snprintf(buf, sizeof(buf), "$J=G91F%.3f", (double)feed_mm_min);
    if (d->dx) n += snprintf(buf + n, sizeof(buf) - n, "X%.4f", (double)(d->dx * dist_mm));
    if (d->dy) n += snprintf(buf + n, sizeof(buf) - n, "Y%.4f", (double)(d->dy * dist_mm));
    if (d->dz) n += snprintf(buf + n, sizeof(buf) - n, "Z%.4f", (double)(d->dz * dist_mm));
    send_jog_line(buf);
}

// Used only until the settings dump lands. Deliberately slow: an unknown
// machine should creep, not traverse.
static const float JOG_FALLBACK_RATE = 500.0f;

// The settings table is not a stable source: Diag (All/Changed) and the Motion
// page clear it and re-request $S/$SC, so for a while -- or for good, if $SC
// omits the key -- MaxRate is simply absent. Jogging then fell to the fallback
// and crawled at 500 x jog%. The last rate the controller reported is still the
// machine's rate, so remember it.
static float axis_max_rate(int axis) {
    static float seen[CAPS_MAX_AXES] = {};
    char key[FS_NAME_MAX];
    snprintf(key, sizeof(key), "Grbl/MaxRate/%c", axisNumToChar(axis));
    for (size_t i = 0; i < fncset_count(); i++) {
        const fnc_setting_t* st = fncset_at(i);
        if (st && strcmp(st->name, key) == 0) {
            float v = (float)atof(st->value);
            if (v > 0.0f) return seen[axis] = v;
        }
    }
    return seen[axis] > 0.0f ? seen[axis] : JOG_FALLBACK_RATE;
}

// Feed is a percentage of what the machine says it can do, not a multiple of
// the step size.
//
// It used to be STEPS[s_step] * 300 -- FluidDial's "5x the step per second" --
// which made the step selector secretly a speed selector and pinned 1mm jogging
// to 300mm/min. Now the step sets distance and settings_jog_pct() sets speed,
// which are genuinely different things.
//
// The slowest involved axis governs: a diagonal is coordinated motion, so
// feeding it Z's rate would ask X to exceed its own maximum, and the planner
// would clip the whole move to something nobody chose.
static float continuous_feed(const dir_t* d) {
    float rate = 0.0f;
    if (d->dx) { float r = axis_max_rate(0); if (!rate || r < rate) rate = r; }
    if (d->dy) { float r = axis_max_rate(1); if (!rate || r < rate) rate = r; }
    if (d->dz) { float r = axis_max_rate(2); if (!rate || r < rate) rate = r; }
    if (rate <= 0.0f) rate = JOG_FALLBACK_RATE;
    if (rate > JOG_MAX_MM_MIN) rate = JOG_MAX_MM_MIN;
    return rate * (float)settings_jog_pct() / 100.0f;
}

// Resend 0x85 until the controller confirms it stopped: keep pumping while the
// state is still Jog, for at least CANCEL_MIN_MS (a status report is ~75ms out,
// so the first look at `state` is stale) and at most CANCEL_MAX_MS.
static void cancel_pump(lv_timer_t* t) {
    uint32_t since = millis() - s_cancel_t0;
    if ((state != Jog && since >= CANCEL_MIN_MS) || since >= CANCEL_MAX_MS) {
        lv_timer_del(t);
        s_cancel_timer = nullptr;
        return;
    }
    send_jog_cancel();
}

// Held past LVGL's 400ms long-press, the key becomes a continuous jog at any
// step size: one long move, ended by cancel on release.
static void jog_hold(lv_event_t* e) {
    const dir_t* d = (const dir_t*)lv_event_get_user_data(e);
    if (!d || (!d->dx && !d->dy && !d->dz)) return;
    if (s_jogging || s_cancel_timer) return;   // never stack a move on a cancel in flight
    s_jogging = true;
    jog_send(d, continuous_feed(d), CONT_DIST_MM);
}

static bool end_continuous() {
    if (!s_jogging) return false;
    s_jogging = false;
    send_jog_cancel();
    s_cancel_t0 = millis();
    if (!s_cancel_timer)
        s_cancel_timer = lv_timer_create(cancel_pump, CANCEL_RESEND_MS, nullptr);
    return true;
}

// The discrete step fires on RELEASE, not press. On press it would land right
// before a hold's continuous move -- a surprise lunge at a coarse step -- and
// it is delivered whole, never truncated: the step is the number you are
// trusting for a touch-off. Same speed law as the hold: the machine's own
// max rate scaled by jog%%, not a compiled-in feed.
static void jog_stop(lv_event_t* e) {
    if (end_continuous()) return;       // a hold ends with no extra step
    if (!e) return;
    const dir_t* d = (const dir_t*)lv_event_get_user_data(e);
    if (!d || (!d->dx && !d->dy && !d->dz)) return;
    jog_send(d, continuous_feed(d), s_step_mm);
}

// Press-lost is the finger sliding off the key onto the bezel: cancel a running
// jog, and send nothing -- a move you half-made is not a move you meant.
static void jog_abort(lv_event_t*) {
    end_continuous();
}

// Every key attach_jog() touched, so the link guard below has one list to walk.
static lv_obj_t* s_jog_keys[10];
static uint8_t   s_jog_key_n = 0;

static void attach_jog(lv_obj_t* btn, const dir_t* d) {
    lv_obj_add_event_cb(btn, jog_hold,  LV_EVENT_LONG_PRESSED, (void*)d);
    lv_obj_add_event_cb(btn, jog_stop,  LV_EVENT_RELEASED,     (void*)d);
    lv_obj_add_event_cb(btn, jog_abort, LV_EVENT_PRESS_LOST,   (void*)d);
    // Dim rather than restyle: opa covers the glyph and the background together,
    // so a disabled key reads as unavailable without a second colour to keep in
    // step with the theme.
    lv_obj_set_style_opa(btn, LV_OPA_40, LV_STATE_DISABLED);
    if (s_jog_key_n < (uint8_t)(sizeof(s_jog_keys) / sizeof(s_jog_keys[0]))) {
        s_jog_keys[s_jog_key_n++] = btn;
    }
}

// LVGL's symbol font has no diagonal arrow, and the montserrat subset carries
// no Unicode one either — U+2196 would render as a missing-glyph box, the same
// way the em dashes did. So the four corners draw their own: a shaft along the
// travel direction and a two-barb head at the tip.
//
// Points must outlive the call: lv_line_set_points() keeps the pointer rather
// than copying, so these are static per corner and never rebuilt.
static lv_point_t s_diag_head[4][3];
static lv_point_t s_diag_shaft[4][2];
static int        s_diag_n = 0;

static void diag_arrow(lv_obj_t* parent, const dir_t* d) {
    if (s_diag_n >= 4) return;
    const int k = s_diag_n++;

    // Sized off the caption font the cardinal chevrons are drawn in, so the two
    // stay matched if that font ever changes. Not the grid quantum: at U*4 these
    // came out roughly double the weight of the glyphs beside them, because an
    // arrow spans its box corner to corner — 1.41x the box edge — while a glyph
    // sits inside its own. Scaling by ~2/3 cancels that out.
    const lv_coord_t S = (lv_coord_t)(lv_font_montserrat_14.line_height * 2 / 3);
    const lv_coord_t b = S / 2;

    // dy is +1 for "up the table", and screen y grows downward, so it inverts.
    const lv_coord_t tipx  = d->dx > 0 ? S : 0;
    const lv_coord_t tipy  = d->dy > 0 ? 0 : S;
    const lv_coord_t tailx = S - tipx;
    const lv_coord_t taily = S - tipy;

    s_diag_shaft[k][0] = { tailx, taily };
    s_diag_shaft[k][1] = { tipx,  tipy  };

    // Barbs run back from the tip along each axis, giving a corner-style head
    // that stays legible at this size where a narrow V does not.
    s_diag_head[k][0] = { (lv_coord_t)(tipx + (tailx > tipx ? b : -b)), tipy };
    s_diag_head[k][1] = { tipx, tipy };
    s_diag_head[k][2] = { tipx, (lv_coord_t)(tipy + (taily > tipy ? b : -b)) };

    lv_obj_t* box = lv_obj_create(parent);
    w_flatten(box);
    lv_obj_set_size(box, S, S);
    lv_obj_center(box);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_CLICKABLE);   // the press belongs to the button

    for (int j = 0; j < 2; j++) {
        lv_obj_t* ln = lv_line_create(box);
        lv_line_set_points(ln, j ? s_diag_head[k] : s_diag_shaft[k], j ? 3 : 2);
        lv_obj_set_style_line_width(ln, 2, 0);   // matches the glyph stroke
        lv_obj_set_style_line_color(ln, C_TEXT, 0);
        lv_obj_set_style_line_rounded(ln, true, 0);
    }
}

static void step_apply(float mm) {
    if (mm < STEPS[0])            mm = STEPS[0];
    if (mm > STEPS[STEPS_N - 1])  mm = STEPS[STEPS_N - 1];
    s_step_mm = mm;

    char b[16];
    snprintf(b, sizeof(b), "%g", (double)s_step_mm);
    lv_label_set_text(s_step_val, b);
    lv_label_set_text(s_mid_lbl, b);

    lv_label_set_text(s_step_note, "hold to jog");
}

// Walk the ladder. From a custom value off the ladder, step to the next rung
// in that direction rather than snapping to the nearest — a typed 250 then a
// tap of "-" should go down, not jump up to 100.
static void step_cb(lv_event_t* e) {
    const int dir = (int)(intptr_t)lv_event_get_user_data(e);
    if (dir < 0) {
        for (int i = STEPS_N - 1; i >= 0; i--)
            if (STEPS[i] < s_step_mm - 1e-6f) { step_apply(STEPS[i]); return; }
        step_apply(STEPS[0]);
    } else {
        for (int i = 0; i < STEPS_N; i++)
            if (STEPS[i] > s_step_mm + 1e-6f) { step_apply(STEPS[i]); return; }
        step_apply(STEPS[STEPS_N - 1]);
    }
}

// The dead centre of the pad was a label. It is the one spot a thumb can reach
// without leaving the pad, so tapping it walks the step by a factor of ten and
// wraps: 0.01 -> 0.1 -> 1 -> 10 -> 0.01.
// Tapping the value types any size, so the ladder is a convenience rather than
// the limit. Clamped by step_apply().
static void step_custom_cb(lv_event_t*) {
    numpad_open("JOG STEP", "millimetres per press", s_step_mm,
                [](float mm) { step_apply(mm); });
}

static void step_cycle_cb(lv_event_t*) {
    // Wraps, unlike the -/+ buttons: this is the thumb-reachable shortcut, and
    // a control that silently stops at the end reads as broken.
    for (int i = 0; i < STEPS_N; i++)
        if (STEPS[i] > s_step_mm + 1e-6f) { step_apply(STEPS[i]); return; }
    step_apply(STEPS[0]);
}

static void zero_cb(lv_event_t* e) {
    int axis = (int)(intptr_t)lv_event_get_user_data(e);
    // G10 L20 P0 targets the *active* work system, which is why the DRO shows
    // which one is live on every page.
    send_linef("G10 L20 P0 %c0", axisNumToChar(axis));
}

// Drops holding torque so the gantry can be pushed by hand, and $ME puts it
// back. The state (and the hard-limit disarm that goes with it) lives in
// model/motors; this button only shows it: green while held, red released.
static lv_obj_t* s_zero_btn[3];
static lv_obj_t* s_motors_btn;
static bool      s_motors_on = true;   // what is painted

static void motors_paint() {
    s_motors_on = !motors_released();
    lv_color_t bg = s_motors_on ? C_OK : C_CRIT;
    lv_obj_set_style_bg_color(s_motors_btn, bg, 0);
    lv_obj_set_style_bg_color(s_motors_btn, lv_color_lighten(bg, 40), LV_STATE_PRESSED);
    // w_button(): button -> column -> caption, sub-label.
    lv_obj_t* col = lv_obj_get_child(s_motors_btn, 0);
    lv_label_set_text(lv_obj_get_child(col, 0), s_motors_on ? "STEPPERS ON" : "STEPPERS OFF");
    lv_label_set_text(lv_obj_get_child(col, 1), s_motors_on ? "$MD" : "$ME");
}

static void motors_cb(lv_event_t*) {
    if (motors_released()) motors_hold();
    else                   motors_release();
    motors_paint();
}

void page_jog_release_motors() {
    motors_release();
    motors_paint();
}

void page_jog_build(lv_obj_t* p) {
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(p, U, 0);

    // --- XY pad, 3x3 of 80px keys ---
    lv_obj_t* pad = lv_obj_create(p);
    w_flatten(pad);
    lv_obj_set_size(pad, PAD, PAD);
    // Fractional, not fixed: the cells divide PAD so the pad stays square and
    // full-height. At 376px content that is ~120px a key, comfortably above the
    // TOUCH_HOLD floor rather than three 80px keys with a gap underneath.
    static lv_coord_t pc[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
    static lv_coord_t pr[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
    lv_obj_set_grid_dsc_array(pad, pc, pr);
    lv_obj_set_layout(pad, LV_LAYOUT_GRID);

    // Corners are diagonals and are left blank here — they get a drawn arrow
    // below. They used to wear the same vertical chevron as the cardinals,
    // which made a diagonal move indistinguishable from a straight one.
    static const char* GLYPH[9] = {
        "",             LV_SYMBOL_UP,   "",
        LV_SYMBOL_LEFT, "",             LV_SYMBOL_RIGHT,
        "",             LV_SYMBOL_DOWN, ""
    };
    for (int i = 0; i < 9; i++) {
        if (i == 4) {
            lv_obj_t* mid = lv_obj_create(pad);
            w_flatten(mid);
            lv_obj_set_grid_cell(mid, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
            // C_SURFACE, not the direction keys' C_RAISE: it reads as tappable
            // without reading as a ninth direction in the middle of the pad.
            lv_obj_set_style_bg_color(mid, C_SURFACE, 0);
            lv_obj_set_style_bg_opa(mid, LV_OPA_COVER, 0);
            lv_obj_add_flag(mid, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(mid, step_cycle_cb, LV_EVENT_CLICKED, nullptr);
            lv_obj_t* cap = w_label(mid, "STEP", &lv_font_montserrat_10, C_DIM);
            lv_obj_align(cap, LV_ALIGN_CENTER, 0, -12);
            // Text set by step_apply() at the end of build, so the label, the
            // selector highlight and s_step have exactly one owner.
            s_mid_lbl = w_label(mid, "", &lv_font_montserrat_20, C_ACCENT);
            lv_obj_align(s_mid_lbl, LV_ALIGN_CENTER, 0, 8);
            continue;
        }
        lv_obj_t* b = w_button(pad, GLYPH[i], nullptr, C_RAISE, C_TEXT, nullptr, nullptr);
        lv_obj_set_grid_cell(b, LV_GRID_ALIGN_STRETCH, i % 3, 1, LV_GRID_ALIGN_STRETCH, i / 3, 1);
        if (!GLYPH[i][0]) diag_arrow(b, &PADDIR[i]);
        attach_jog(b, &PADDIR[i]);
    }

    // --- Z column ---
    lv_obj_t* zc = lv_obj_create(p);
    w_flatten(zc);
    lv_obj_set_size(zc, ZCOL_W, PAD);
    lv_obj_set_flex_flow(zc, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(zc, 2, 0);

    lv_obj_t* zu = w_button(zc, "Z+", nullptr, C_RAISE, C_TEXT, nullptr, nullptr);
    lv_obj_set_size(zu, LV_PCT(100), (PAD - 2) / 2);
    attach_jog(zu, &ZUP);

    lv_obj_t* zd = w_button(zc, "Z-", nullptr, C_RAISE, C_TEXT, nullptr, nullptr);
    lv_obj_set_size(zd, LV_PCT(100), (PAD - 2) / 2);
    attach_jog(zd, &ZDOWN);

    // --- right column: step selector over zero buttons ---
    lv_obj_t* rc = lv_obj_create(p);
    w_flatten(rc);
    lv_obj_set_flex_grow(rc, 1);
    lv_obj_set_height(rc, PAD);
    lv_obj_set_flex_flow(rc, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(rc, U, 0);

    lv_obj_t* steps = lv_obj_create(rc);
    w_flatten(steps);
    lv_obj_set_size(steps, LV_PCT(100), U * 8);
    lv_obj_set_flex_flow(steps, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(steps, U, 0);
    // Four fixed sizes became a ladder you walk. The old row could only ever
    // offer what was compiled into it, which is why crossing 400mm meant forty
    // presses of "10".
    s_step_dn = w_button(steps, "-", nullptr, C_RAISE, C_ACCENT,
                         step_cb, (void*)(intptr_t)-1);
    lv_obj_set_width(s_step_dn, TOUCH_HOLD);
    lv_obj_set_height(s_step_dn, LV_PCT(100));

    lv_obj_t* mid = w_panel(steps, C_SURFACE);
    lv_obj_set_flex_grow(mid, 1);
    lv_obj_set_height(mid, LV_PCT(100));
    lv_obj_add_flag(mid, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(mid, step_custom_cb, LV_EVENT_CLICKED, nullptr);
    s_step_val = w_label(mid, "1", &lv_font_montserrat_28, C_TEXT);
    lv_obj_align(s_step_val, LV_ALIGN_CENTER, 0, -6);
    s_step_note = w_label(mid, "", &lv_font_montserrat_10, C_DIM);
    lv_obj_align(s_step_note, LV_ALIGN_BOTTOM_MID, 0, -4);

    s_step_up = w_button(steps, "+", nullptr, C_RAISE, C_ACCENT,
                         step_cb, (void*)(intptr_t)1);
    lv_obj_set_width(s_step_up, TOUCH_HOLD);
    lv_obj_set_height(s_step_up, LV_PCT(100));

    lv_obj_t* zeros = lv_obj_create(rc);
    w_flatten(zeros);
    // Takes whatever the step selector leaves, so the right column ends level
    // with the pad instead of stopping short at a fixed 168px.
    lv_obj_set_width(zeros, LV_PCT(100));
    lv_obj_set_flex_grow(zeros, 1);
    static lv_coord_t zcs[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
    static lv_coord_t zrs[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
    lv_obj_set_grid_dsc_array(zeros, zcs, zrs);
    lv_obj_set_layout(zeros, LV_LAYOUT_GRID);
    lv_obj_set_style_pad_column(zeros, U, 0);
    lv_obj_set_style_pad_row(zeros, U, 0);

    // $MD takes the fourth slot rather than Zero-All: pushing the gantry by hand
    // is reached far more often than zeroing every axis at once.
    lv_obj_t* b0 = s_zero_btn[0] = w_button(zeros, "X ZERO", "G10 L20 P0", C_SURFACE, C_TEXT, zero_cb, (void*)(intptr_t)0);
    lv_obj_t* b1 = s_zero_btn[1] = w_button(zeros, "Y ZERO", "G10 L20 P0", C_SURFACE, C_TEXT, zero_cb, (void*)(intptr_t)1);
    lv_obj_t* b2 = s_zero_btn[2] = w_button(zeros, "Z ZERO", "G10 L20 P0", C_SURFACE, C_TEXT, zero_cb, (void*)(intptr_t)2);
    lv_obj_t* b3 = s_motors_btn = w_button(zeros, "STEPPERS ON", "$MD", C_OK, C_BG, motors_cb, nullptr);
    motors_paint();
    lv_obj_set_grid_cell(b0, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_set_grid_cell(b1, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_set_grid_cell(b2, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
    lv_obj_set_grid_cell(b3, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 1, 1);

    step_apply(s_step_mm);   // paints the selector and the centre from one place
}

// A jog key with no controller behind it is not a harmless no-op. send_jog_line()
// is ack-gated on UART, so every press parks in fnc_send_line()'s `while
// (_ackwait)` until the timeout expires -- inside the same lv_timer_handler()
// pass that reads touch. Held past 400ms it is worse:
// the release that would stop the jog can starve behind the blocked send.
//
// lv_obj_hit_test() rejects LV_STATE_DISABLED outright, so disabling the keys
// stops the press ever reaching an event handler -- no press, no long press, no
// timer. And a key that cannot work must not look like it can.
void page_jog_tick() {
    if (s_motors_on == motors_released()) motors_paint();

    // Zeroing is G10: refused in Alarm like any other G-code. STEPPERS stays
    // live -- $MD is allowed in Alarm, and freeing the gantry to push it by
    // hand is exactly what an alarm can call for.
    static int was_locked = -1;
    const bool locked = w_locked();
    if (locked != was_locked) {
        was_locked = locked;
        for (lv_obj_t* b : s_zero_btn) w_gate(b, !locked, LOCK_REASON);
    }

    static bool was_off = false;
    bool off = (state == Disconnected) || locked;
    if (off == was_off) return;
    was_off = off;

    for (uint8_t i = 0; i < s_jog_key_n; i++) {
        if (off) lv_obj_add_state(s_jog_keys[i], LV_STATE_DISABLED);
        else     lv_obj_clear_state(s_jog_keys[i], LV_STATE_DISABLED);
    }
    // A link that drops mid-hold must not leave a continuous move armed: end
    // the session so the release logic cannot mistake reconnection for a jog.
    if (off) jog_stop(nullptr);
}
