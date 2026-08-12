#include "ui/pages.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "model/FluidNCModel.h"
#include "GrblParserC.h"
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

struct dir_t { int dx, dy, dz; };

// Pad slots in reading order; slot 4 is the centre and is not a button.
static const dir_t PADDIR[9] = {
    { -1,  1, 0 }, { 0,  1, 0 }, { 1,  1, 0 },
    { -1,  0, 0 }, { 0,  0, 0 }, { 1,  0, 0 },
    { -1, -1, 0 }, { 0, -1, 0 }, { 1, -1, 0 },
};
static const dir_t ZUP   = { 0, 0,  1 };
static const dir_t ZDOWN = { 0, 0, -1 };

static void jog_start(lv_event_t* e) {
    const dir_t* d = (const dir_t*)lv_event_get_user_data(e);
    if (!d || (!d->dx && !d->dy && !d->dz)) return;
    char buf[72];
    float s = STEPS[s_step];
    int n = snprintf(buf, sizeof(buf), "$J=G91F2000");
    if (d->dx) n += snprintf(buf + n, sizeof(buf) - n, "X%.3f", (double)(d->dx * s));
    if (d->dy) n += snprintf(buf + n, sizeof(buf) - n, "Y%.3f", (double)(d->dy * s));
    if (d->dz) n += snprintf(buf + n, sizeof(buf) - n, "Z%.3f", (double)(d->dz * s));
    send_jog_line(buf);
}

// Cancel on release AND on press-lost. Press-lost is the finger sliding off the
// key; without it the axis keeps moving after the thumb reaches the bezel.
static void jog_stop(lv_event_t*) { send_jog_cancel(); }

static void attach_jog(lv_obj_t* btn, const dir_t* d) {
    lv_obj_add_event_cb(btn, jog_start, LV_EVENT_PRESSED,    (void*)d);
    lv_obj_add_event_cb(btn, jog_stop,  LV_EVENT_RELEASED,   (void*)d);
    lv_obj_add_event_cb(btn, jog_stop,  LV_EVENT_PRESS_LOST, (void*)d);
}

static void step_cb(lv_event_t* e) {
    s_step = (int)(intptr_t)lv_event_get_user_data(e);
    for (int i = 0; i < 4; i++) {
        lv_obj_set_style_bg_color(s_step_btn[i], i == s_step ? C_ACCENT : C_RAISE, 0);
    }
    char b[12];
    snprintf(b, sizeof(b), "%g", (double)STEPS[s_step]);
    lv_label_set_text(s_mid_lbl, b);
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
            lv_obj_t* cap = w_label(mid, "STEP", &lv_font_montserrat_10, C_DIM);
            lv_obj_align(cap, LV_ALIGN_CENTER, 0, -12);
            s_mid_lbl = w_label(mid, "1", &lv_font_montserrat_20, C_ACCENT);
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
        s_step_btn[i] = w_button(steps, SLBL[i], "mm",
                                 i == s_step ? C_ACCENT : C_RAISE, C_TEXT,
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
}

void page_jog_tick() {}
