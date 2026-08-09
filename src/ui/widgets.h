#pragma once
#include <lvgl.h>
#include "ui/theme.h"

// A flat panel: surface fill, hairline border, small radius, no scroll.
lv_obj_t* w_panel(lv_obj_t* parent, lv_color_t bg);

// A labelled action button. `sub` may be nullptr; when present it renders small
// and dim beneath the label, which is how buttons that write persistent machine
// state show the G-code they send.
lv_obj_t* w_button(lv_obj_t* parent, const char* label, const char* sub,
                   lv_color_t bg, lv_color_t fg, lv_event_cb_t cb, void* user);

lv_obj_t* w_label(lv_obj_t* parent, const char* text, const lv_font_t* font, lv_color_t col);

// Strip padding, border and scrollbars so a container is pure layout.
void w_flatten(lv_obj_t* o);
