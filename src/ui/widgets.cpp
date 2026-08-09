#include "ui/widgets.h"

void w_flatten(lv_obj_t* o) {
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_pad_row(o, 0, 0);
    lv_obj_set_style_pad_column(o, 0, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t* w_panel(lv_obj_t* parent, lv_color_t bg) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(o, bg, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(o, C_LINE, 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_radius(o, 3, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    return o;
}

lv_obj_t* w_label(lv_obj_t* parent, const char* text, const lv_font_t* font, lv_color_t col) {
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, col, 0);
    return l;
}

lv_obj_t* w_button(lv_obj_t* parent, const char* label, const char* sub,
                   lv_color_t bg, lv_color_t fg, lv_event_cb_t cb, void* user) {
    lv_obj_t* b = lv_btn_create(parent);
    lv_obj_set_style_bg_color(b, bg, 0);
    lv_obj_set_style_border_color(b, C_LINE, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_radius(b, 3, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 2, 0);
    // Pressed state must be visible, not just felt: a gloved finger on a
    // capacitive panel gets no tactile confirmation.
    lv_obj_set_style_bg_color(b, lv_color_lighten(bg, 40), LV_STATE_PRESSED);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);

    lv_obj_t* col = lv_obj_create(b);
    w_flatten(col);
    lv_obj_set_size(col, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_CLICKABLE);

    w_label(col, label, &lv_font_montserrat_14, fg);
    if (sub) {
        w_label(col, sub, &lv_font_montserrat_10, lv_color_mix(fg, C_DIM, 128));
    }
    return b;
}

lv_obj_t* w_keyboard() {
    static lv_obj_t* kb = nullptr;
    if (!kb) {
        kb = lv_keyboard_create(lv_scr_act());
        lv_obj_set_size(kb, SCREEN_W, SCREEN_H / 2);
        lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }
    return kb;
}
