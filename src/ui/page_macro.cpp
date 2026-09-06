#include "ui/pages.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "model/FluidNCModel.h"
#include "model/caps.h"
#include "GrblParserC.h"

#include <stdio.h>
#include <string.h>

// FluidNC macros are named in the controller's config; $RM=<name> runs one.
//
// This page used to draw six buttons labelled macro0..macro5, invented here and
// never checked against anything. FluidNC v4 defines FOUR slots, spelled with a
// capital M, and in a typical config most are empty — so five of those six
// buttons could only ever return an error, and the sixth had the wrong case.
//
// Names now come from the config dump (caps.h). Slots are built once and shown
// on the tick rather than rebuilt, because the dump arrives well after
// shell_build() and the rest of the console works the same way: build the
// widgets up front, move data into them later.

static lv_obj_t* s_btn[CAPS_MACRO_MAX];
static lv_obj_t* s_name[CAPS_MACRO_MAX];
static lv_obj_t* s_body[CAPS_MACRO_MAX];
static lv_obj_t* s_empty;
static size_t    s_shown = (size_t)-1;   // force a first pass

static void macro_cb(lv_event_t* e) {
    size_t i = (size_t)(intptr_t)lv_event_get_user_data(e);
    const char* name = caps_macro_name(i);
    if (name && *name) send_linef("$RM=%s", name);
}

void page_macro_build(lv_obj_t* p) {
    w_flatten(p);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(p, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(p, U, 0);
    lv_obj_set_style_pad_row(p, U, 0);

    for (int i = 0; i < CAPS_MACRO_MAX; i++) {
        s_btn[i] = lv_btn_create(p);
        lv_obj_set_style_bg_color(s_btn[i], C_SURFACE, 0);
        lv_obj_set_style_bg_color(s_btn[i], lv_color_lighten(C_SURFACE, 40), LV_STATE_PRESSED);
        lv_obj_set_style_border_color(s_btn[i], C_LINE, 0);
        lv_obj_set_style_border_width(s_btn[i], 1, 0);
        lv_obj_set_style_radius(s_btn[i], 3, 0);
        lv_obj_set_style_shadow_width(s_btn[i], 0, 0);
        // Two across, half the page high: a macro is a deliberate action, so
        // the target stays well above the discrete-tap floor.
        lv_obj_set_size(s_btn[i], (CONTENT_W - U * 5) / 2, TOUCH_HOLD);
        lv_obj_add_event_cb(s_btn[i], macro_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);

        lv_obj_t* col = lv_obj_create(s_btn[i]);
        w_flatten(col);
        lv_obj_set_size(col, LV_PCT(100), LV_PCT(100));
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(col, LV_OBJ_FLAG_CLICKABLE);

        s_name[i] = w_label(col, "", &lv_font_montserrat_14, C_TEXT);
        // The macro's actual g-code, so the button says what it will do rather
        // than only which slot it lives in.
        s_body[i] = w_label(col, "", &lv_font_montserrat_10, C_DIM);

        lv_obj_add_flag(s_btn[i], LV_OBJ_FLAG_HIDDEN);
    }

    s_empty = w_label(p, "waiting for machine config", &lv_font_montserrat_14, C_DIM);
}

void page_macro_tick() {
    size_t n = caps_macro_count();
    if (n == s_shown) return;
    s_shown = n;

    for (int i = 0; i < CAPS_MACRO_MAX; i++) {
        if ((size_t)i < n) {
            lv_label_set_text(s_name[i], caps_macro_name(i));
            lv_label_set_text(s_body[i], caps_macro_body(i));
            lv_obj_clear_flag(s_btn[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_btn[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    // Only reachable while capabilities are still unknown: once the dump says
    // there are none, the rail drops this page entirely.
    if (n) lv_obj_add_flag(s_empty, LV_OBJ_FLAG_HIDDEN);
    else   lv_obj_clear_flag(s_empty, LV_OBJ_FLAG_HIDDEN);
}
