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

// Enable or disable a w_button(), and say why.
//
// A control the machine cannot honour must not look like one it can: pressing
// HOME on a machine with no endstops earns an error:5 and no motion, which
// reads as a broken pendant rather than an unconfigured machine. Disabled
// buttons keep their place in the layout — the reason is the point, so hiding
// them would just raise the same question later.
//
// `reason` replaces the button's sub-label while disabled and is restored on
// re-enable; it needs the button to have been given a `sub` at build time.
void w_gate(lv_obj_t* btn, bool enabled, const char* reason);

// One on-screen keyboard shared by every page that needs text entry. Created on
// the screen rather than inside a page so it can cover the whole panel.
lv_obj_t* w_keyboard();

// Strip padding, border and scrollbars so a container is pure layout.
void w_flatten(lv_obj_t* o);

// FluidNC refuses G-code and motion while in Alarm (error:9). Controls that
// would send either are disabled with this reason rather than left looking
// live and answering with errors. UNLOCK, RESET, HOME, STEPPERS, the
// overrides, settings and the console stay usable.
bool w_locked();
static const char* const LOCK_REASON = "home or unlock";   // the chip already says ALARM
