#include "ui/pages.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "hw/display_hw.h"
#include "model/FluidNCModel.h"
#include "GrblParserC.h"
#include <stdio.h>

static lv_obj_t* s_bright_lbl;

static void bright_cb(lv_event_t* e) {
    int dir = (int)(intptr_t)lv_event_get_user_data(e);
    int b = (int)get_backlight() + dir * 25;
    // Never fully dark: an unreadable panel next to a running spindle is a hazard.
    if (b < 25)  b = 25;
    if (b > 255) b = 255;
    set_backlight((uint8_t)b);
    char t[8];
    snprintf(t, sizeof(t), "%d", b);
    lv_label_set_text(s_bright_lbl, t);
}

static void restart_ctrl_cb(lv_event_t*) { send_line("$Bye"); }

void page_setup_build(lv_obj_t* p) {
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(p, U, 0);

    lv_obj_t* row = w_panel(p, C_SURFACE);
    lv_obj_set_size(row, LV_PCT(100), U * 10);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(row, U + 4, 0);

    lv_obj_t* nm = w_label(row, "Brightness", &lv_font_montserrat_16, C_TEXT);
    lv_obj_set_flex_grow(nm, 1);

    lv_obj_t* dn = w_button(row, "-", nullptr, C_RAISE, C_ACCENT, bright_cb, (void*)(intptr_t)-1);
    lv_obj_set_size(dn, U * 8, U * 8);

    s_bright_lbl = w_label(row, "200", &lv_font_montserrat_16, C_TEXT);
    lv_obj_set_width(s_bright_lbl, U * 8);
    lv_obj_set_style_text_align(s_bright_lbl, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t* up = w_button(row, "+", nullptr, C_RAISE, C_ACCENT, bright_cb, (void*)(intptr_t)1);
    lv_obj_set_size(up, U * 8, U * 8);

    lv_obj_t* rst = w_button(p, "RESTART CONTROLLER", "$Bye", C_RAISE, C_WARN,
                             restart_ctrl_cb, nullptr);
    lv_obj_set_size(rst, LV_PCT(100), U * 10);
}
