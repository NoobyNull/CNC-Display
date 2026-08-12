#include "ui/pages.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "model/FluidNCModel.h"
#include "GrblParserC.h"

static void home_all_cb(lv_event_t*) { send_line("$H"); }

static void home_axis_cb(lv_event_t* e) {
    int axis = (int)(intptr_t)lv_event_get_user_data(e);
    send_linef("$H%c", axisNumToChar(axis));
}

static void unlock_cb(lv_event_t*) { send_line("$X"); }

void page_home_build(lv_obj_t* p) {
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(p, U, 0);

    lv_obj_t* left = lv_obj_create(p);
    w_flatten(left);
    lv_obj_set_size(left, U * 44, LV_PCT(100));
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(left, U, 0);

    // Both halves grow: they were equal fixed heights that filled the old 256px
    // content area exactly, so sharing the taller one keeps the design's 50/50
    // split instead of stacking them at the top over a band of background.
    lv_obj_t* all = w_button(left, "HOME ALL", "$H", C_RUN, C_BG, home_all_cb, nullptr);
    lv_obj_set_width(all, LV_PCT(100));
    lv_obj_set_flex_grow(all, 1);

    lv_obj_t* row = lv_obj_create(left);
    w_flatten(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_flex_grow(row, 1);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, U, 0);
    static const char* AXL[3] = { "HOME X", "HOME Y", "HOME Z" };
    for (int i = 0; i < 3; i++) {
        lv_obj_t* b = w_button(row, AXL[i], nullptr, C_SURFACE, C_TEXT,
                               home_axis_cb, (void*)(intptr_t)i);
        lv_obj_set_flex_grow(b, 1);
        lv_obj_set_height(b, LV_PCT(100));
    }

    lv_obj_t* right = lv_obj_create(p);
    w_flatten(right);
    lv_obj_set_flex_grow(right, 1);
    lv_obj_set_height(right, LV_PCT(100));
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(right, U, 0);

    lv_obj_t* unlock = w_button(right, "UNLOCK", "$X", C_WARN, C_BG, unlock_cb, nullptr);
    lv_obj_set_width(unlock, LV_PCT(100));
    lv_obj_set_flex_grow(unlock, 1);

    lv_obj_t* note = w_panel(right, C_SURFACE);
    lv_obj_set_width(note, LV_PCT(100));
    lv_obj_set_flex_grow(note, 1);
    lv_obj_t* nl = w_label(note, "Homing moves at full rate.\nClear the table first.",
                           &lv_font_montserrat_12, C_DIM);
    lv_obj_set_style_text_align(nl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(nl);
}
