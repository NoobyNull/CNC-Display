#include "ui/pages.h"
#include "ui/shell.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "model/FluidNCModel.h"
#include "GrblParserC.h"
#include <Arduino.h>
#include <stdio.h>

// The controls fill the page height rather than a fixed 240px. That constant
// exactly filled the CrowPanel's 256px content area and left 120px of dead space
// once the 7B's content area grew to 376. Everything on this page is squared or
// split off PAD, so deriving it here reflows the whole layout.
static const lv_coord_t PAD    = CONTENT_H - U * 2;
static const lv_coord_t ZCOL_W = U * 11;   // 88

static const float STEPS[4] = { 0.01f, 0.1f, 1.0f, 10.0f };
static int s_step = 2;
static lv_obj_t* s_step_btn[4];
static lv_obj_t* s_mid_lbl;

// Hold-to-jog is offered at 1mm and below only. At 10mm a press is a deliberate
// discrete move; a held key that ran away at coarse resolution is the wrong
// thing to have under a thumb.
static const float CONTINUOUS_MAX_MM = 1.0f;

// Continuous jog re-issues short moves while the key is held, rather than
// sending one long move and cancelling it on release (FluidDial's idiom, see
// MultiJogScene::start_button_jog).
//
// That is deliberate. Against FluidNC v4.0.4 the JogCancel realtime byte (0x85)
// did not stop a jog in testing -- neither raw, which is what GrblParserC sends
// (GrblParserC.c:572), nor UTF-8 encoded -- while feed hold on the same channel
// stopped it every time. Until that is understood on real hardware, nothing that
// moves the machine may depend on that byte arriving. Releasing the key stops
// the axis by *not asking for more*, which holds whatever the controller does
// with realtime commands.
//
// Each tick asks for exactly one tick's worth of travel, so the planner stays fed
// without the queue growing, and a release overshoots by at most one chunk. At
// the 1mm step -- the fastest continuous setting -- that is 0.75mm.
static const uint32_t JOG_TICK_MS = 150;

struct dir_t { int dx, dy, dz; };

static lv_timer_t* s_jog_timer = nullptr;
static dir_t       s_jog_dir   = { 0, 0, 0 };

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

// A tap is one step, delivered whole.
static void jog_start(lv_event_t* e) {
    const dir_t* d = (const dir_t*)lv_event_get_user_data(e);
    if (!d || (!d->dx && !d->dy && !d->dz)) return;
    jog_send(d, 2000.0f, STEPS[s_step]);
}

// Feed tracks the selected step -- FluidDial's "5x the step per second" scaling
// -- so the same key creeps at 0.01mm and traverses at 1mm, and the step
// selector doubles as a speed selector.
static float continuous_feed() { return STEPS[s_step] * 300.0f; }

static void jog_stop(lv_event_t*);

static void jog_chunk(lv_timer_t*) {
    // Leaving the page with a key held would otherwise keep the timer running
    // and the axis moving somewhere nobody is looking.
    if (shell_current_page() != PAGE_JOG) {
        jog_stop(nullptr);
        return;
    }
    const float feed = continuous_feed();
    // send_jog_line() is ack-gated on UART (jog_flow_control() == Blocking), so a
    // chunk can sit in fnc_send_line()'s `while (_ackwait)` for its full timeout.
    // This runs inside lv_timer_handler(), which also drives touch_poll and
    // LVGL's input read -- so a slow chunk is a frozen panel, not a slow jog.
    uint32_t t0 = millis();
    jog_send(&s_jog_dir, feed, feed / 60.0f * (JOG_TICK_MS / 1000.0f));
    uint32_t dt = millis() - t0;
    if (dt > 20) Serial.printf("[jog] chunk blocked %lums\n", (unsigned long)dt);
}

// Held past LVGL's 400ms long-press, the discrete step gives way to a continuous
// jog in the same direction -- but only at 1mm and below. At 10mm a press stays
// a deliberate discrete move: that cap is what keeps the worst case of a lost
// release down to one 0.75mm chunk instead of a coarse runaway.
static void jog_hold(lv_event_t* e) {
    const dir_t* d = (const dir_t*)lv_event_get_user_data(e);
    if (!d || (!d->dx && !d->dy && !d->dz)) return;
    if (STEPS[s_step] > CONTINUOUS_MAX_MM) return;
    if (s_jog_timer) return;

    s_jog_dir   = *d;
    s_jog_timer = lv_timer_create(jog_chunk, JOG_TICK_MS, nullptr);
    jog_chunk(nullptr);   // first chunk now, not one tick from now
}

// Stop on release AND on press-lost -- press-lost is the finger sliding off the
// key, and without it the axis keeps moving once the thumb reaches the bezel.
//
// Only a hold gets stopped. A tap has to deliver its whole step: cancelling
// every release truncated the move to however long the finger happened to be
// down, which is precisely wrong when the step is the number you are trusting
// for a touch-off.
//
// The jog cancel is a bonus, not the mechanism. Stopping the chunks is what
// actually halts the axis; if the realtime byte does land it just stops sooner.
static void jog_stop(lv_event_t*) {
    if (!s_jog_timer) return;
    lv_timer_del(s_jog_timer);
    s_jog_timer = nullptr;
    send_jog_cancel();
}

// Every key attach_jog() touched, so the link guard below has one list to walk.
static lv_obj_t* s_jog_keys[10];
static uint8_t   s_jog_key_n = 0;

static void attach_jog(lv_obj_t* btn, const dir_t* d) {
    lv_obj_add_event_cb(btn, jog_start, LV_EVENT_PRESSED,      (void*)d);
    lv_obj_add_event_cb(btn, jog_hold,  LV_EVENT_LONG_PRESSED, (void*)d);
    lv_obj_add_event_cb(btn, jog_stop,  LV_EVENT_RELEASED,     (void*)d);
    lv_obj_add_event_cb(btn, jog_stop,  LV_EVENT_PRESS_LOST,   (void*)d);
    // Dim rather than restyle: opa covers the glyph and the background together,
    // so a disabled key reads as unavailable without a second colour to keep in
    // step with the theme.
    lv_obj_set_style_opa(btn, LV_OPA_40, LV_STATE_DISABLED);
    if (s_jog_key_n < (uint8_t)(sizeof(s_jog_keys) / sizeof(s_jog_keys[0]))) {
        s_jog_keys[s_jog_key_n++] = btn;
    }
}

static void step_apply(int idx) {
    s_step = idx;
    for (int i = 0; i < 4; i++) {
        lv_obj_set_style_bg_color(s_step_btn[i], i == s_step ? C_ACCENT : C_RAISE, 0);
    }
    char b[12];
    snprintf(b, sizeof(b), "%g", (double)STEPS[s_step]);
    lv_label_set_text(s_mid_lbl, b);
}

static void step_cb(lv_event_t* e) {
    step_apply((int)(intptr_t)lv_event_get_user_data(e));
}

// The dead centre of the pad was a label. It is the one spot a thumb can reach
// without leaving the pad, so tapping it walks the step by a factor of ten and
// wraps: 0.01 -> 0.1 -> 1 -> 10 -> 0.01.
static void step_cycle_cb(lv_event_t*) {
    step_apply((s_step + 1) % 4);
}

static void zero_cb(lv_event_t* e) {
    int axis = (int)(intptr_t)lv_event_get_user_data(e);
    // G10 L20 P0 targets the *active* work system, which is why the DRO shows
    // which one is live on every page.
    send_linef("G10 L20 P0 %c0", axisNumToChar(axis));
}

// Drops holding torque so the gantry can be pushed by hand. Tinted amber: on a
// machine with an unbalanced Z, the head can fall when the steppers release.
static void motors_off_cb(lv_event_t*) { send_line("$MD"); }

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

    static const char* GLYPH[9] = {
        LV_SYMBOL_UP,   LV_SYMBOL_UP,   LV_SYMBOL_UP,
        LV_SYMBOL_LEFT, "",             LV_SYMBOL_RIGHT,
        LV_SYMBOL_DOWN, LV_SYMBOL_DOWN, LV_SYMBOL_DOWN
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
    static const char* SLBL[4] = { "0.01", "0.1", "1.0", "10" };
    for (int i = 0; i < 4; i++) {
        s_step_btn[i] = w_button(steps, SLBL[i], "mm", C_RAISE, C_TEXT,
                                 step_cb, (void*)(intptr_t)i);
        lv_obj_set_flex_grow(s_step_btn[i], 1);
        lv_obj_set_height(s_step_btn[i], LV_PCT(100));
    }

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
    lv_obj_t* b0 = w_button(zeros, "X ZERO", "G10 L20 P0", C_SURFACE, C_TEXT, zero_cb, (void*)(intptr_t)0);
    lv_obj_t* b1 = w_button(zeros, "Y ZERO", "G10 L20 P0", C_SURFACE, C_TEXT, zero_cb, (void*)(intptr_t)1);
    lv_obj_t* b2 = w_button(zeros, "Z ZERO", "G10 L20 P0", C_SURFACE, C_TEXT, zero_cb, (void*)(intptr_t)2);
    lv_obj_t* b3 = w_button(zeros, "STEPPERS OFF", "$MD", C_RAISE, C_WARN, motors_off_cb, nullptr);
    lv_obj_set_grid_cell(b0, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_set_grid_cell(b1, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_set_grid_cell(b2, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
    lv_obj_set_grid_cell(b3, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 1, 1);

    step_apply(s_step);   // paints the selector and the centre from one place
}

// A jog key with no controller behind it is not a harmless no-op. send_jog_line()
// is ack-gated on UART, so every press parks in fnc_send_line()'s `while
// (_ackwait)` until the timeout expires -- inside the same lv_timer_handler()
// pass that reads touch. Held past 400ms it is worse: jog_chunk() re-sends every
// 150ms, each send costing more than the tick that scheduled it, so the timer is
// permanently overdue, the input read is starved, and the release that would
// call jog_stop() never arrives. The panel is then dead until a power cycle.
//
// lv_obj_hit_test() rejects LV_STATE_DISABLED outright, so disabling the keys
// stops the press ever reaching an event handler -- no press, no long press, no
// timer. And a key that cannot work must not look like it can.
void page_jog_tick() {
    static bool was_off = false;
    bool off = (state == Disconnected);
    if (off == was_off) return;
    was_off = off;

    for (uint8_t i = 0; i < s_jog_key_n; i++) {
        if (off) lv_obj_add_state(s_jog_keys[i], LV_STATE_DISABLED);
        else     lv_obj_clear_state(s_jog_keys[i], LV_STATE_DISABLED);
    }
    // A link that drops mid-hold must not leave the chunk timer running: it
    // would keep asking a controller that is no longer answering, at the full
    // ack timeout per tick.
    if (off) jog_stop(nullptr);
}
