#include "ui/pages.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "hw/display_hw.h"
#include "model/FluidNCModel.h"
#include "model/settings.h"
#include "transport/transport.h"
#include "GrblParserC.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t* s_bright_lbl;
static lv_obj_t* s_uart_sw;
static lv_obj_t* s_wifi_sw;
static lv_obj_t* s_status_lbl;
static lv_obj_t* s_ssid_ta;
static lv_obj_t* s_pass_ta;
static lv_obj_t* s_kb;

static void bright_cb(lv_event_t* e) {
    int dir = (int)(intptr_t)lv_event_get_user_data(e);
    int b = (int)get_backlight() + dir * 25;
    // Never fully dark: an unreadable panel next to a running spindle is a hazard.
    if (b < 25)  b = 25;
    if (b > 255) b = 255;
    set_backlight((uint8_t)b);
    settings_set_brightness((uint8_t)b);
    char t[8];
    snprintf(t, sizeof(t), "%d", b);
    lv_label_set_text(s_bright_lbl, t);
}

static void uart_sw_cb(lv_event_t* e) {
    settings_set_uart_enabled(lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED));
    transport_settings_changed();
}

static void wifi_sw_cb(lv_event_t* e) {
    settings_set_wifi_enabled(lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED));
    transport_settings_changed();
}

// The keyboard covers the lower half of the screen while typing, so it is
// created hidden and only raised for the field being edited.
static void ta_focus_cb(lv_event_t* e) {
    lv_obj_t* ta = lv_event_get_target(e);
    lv_keyboard_set_textarea(s_kb, ta);
    lv_obj_clear_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_kb);
}

static void kb_done_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        settings_set_wifi_creds(lv_textarea_get_text(s_ssid_ta),
                                lv_textarea_get_text(s_pass_ta));
        lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(s_kb, nullptr);
        transport_settings_changed();   // reconnect with the new credentials
    }
}

static void restart_ctrl_cb(lv_event_t*) { send_line("$Bye"); }

static lv_obj_t* toggle_row(lv_obj_t* parent, const char* name, const char* sub,
                            bool on, lv_event_cb_t cb, lv_obj_t** sw_out) {
    lv_obj_t* row = w_panel(parent, C_SURFACE);
    lv_obj_set_size(row, LV_PCT(100), U * 7);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(row, U + 4, 0);

    lv_obj_t* col = lv_obj_create(row);
    w_flatten(col);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_height(col, LV_PCT(100));
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    w_label(col, name, &lv_font_montserrat_16, C_TEXT);
    if (sub) w_label(col, sub, &lv_font_montserrat_10, C_DIM);

    lv_obj_t* sw = lv_switch_create(row);
    lv_obj_set_size(sw, U * 9, U * 5);
    lv_obj_set_style_pad_right(row, U + 4, 0);
    lv_obj_set_style_bg_color(sw, C_RAISE, 0);
    lv_obj_set_style_bg_color(sw, C_ACCENT, LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (on) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, nullptr);
    *sw_out = sw;
    return row;
}

void page_setup_build(lv_obj_t* p) {
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(p, U, 0);

    // --- left: transports ---
    lv_obj_t* left = lv_obj_create(p);
    w_flatten(left);
    lv_obj_set_size(left, U * 44, LV_PCT(100));
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(left, U, 0);

    toggle_row(left, "Wired UART", "GPIO 43/44, auto 1M or 115K",
               settings_uart_enabled(), uart_sw_cb, &s_uart_sw);
    toggle_row(left, "WiFi", "Telnet, found via mDNS",
               settings_wifi_enabled(), wifi_sw_cb, &s_wifi_sw);

    lv_obj_t* st = w_panel(left, C_RAISE);
    lv_obj_set_size(st, LV_PCT(100), U * 7);
    lv_obj_t* stk = w_label(st, "LINK", &lv_font_montserrat_10, C_DIM);
    lv_obj_align(stk, LV_ALIGN_TOP_LEFT, U + 4, 5);
    s_status_lbl = w_label(st, "no link", &lv_font_montserrat_14, C_TEXT);
    lv_obj_align(s_status_lbl, LV_ALIGN_BOTTOM_LEFT, U + 4, -6);

    // --- right: credentials, brightness, restart ---
    lv_obj_t* right = lv_obj_create(p);
    w_flatten(right);
    lv_obj_set_flex_grow(right, 1);
    lv_obj_set_height(right, LV_PCT(100));
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(right, U, 0);

    s_ssid_ta = lv_textarea_create(right);
    lv_textarea_set_one_line(s_ssid_ta, true);
    lv_textarea_set_placeholder_text(s_ssid_ta, "wifi network");
    lv_textarea_set_text(s_ssid_ta, settings_wifi_ssid());
    lv_obj_set_size(s_ssid_ta, LV_PCT(100), U * 6);
    lv_obj_set_style_bg_color(s_ssid_ta, C_SURFACE, 0);
    lv_obj_set_style_text_color(s_ssid_ta, C_TEXT, 0);
    lv_obj_set_style_border_color(s_ssid_ta, C_LINE, 0);
    lv_obj_add_event_cb(s_ssid_ta, ta_focus_cb, LV_EVENT_CLICKED, nullptr);

    s_pass_ta = lv_textarea_create(right);
    lv_textarea_set_one_line(s_pass_ta, true);
    lv_textarea_set_password_mode(s_pass_ta, true);
    lv_textarea_set_placeholder_text(s_pass_ta, "wifi password");
    lv_textarea_set_text(s_pass_ta, settings_wifi_pass());
    lv_obj_set_size(s_pass_ta, LV_PCT(100), U * 6);
    lv_obj_set_style_bg_color(s_pass_ta, C_SURFACE, 0);
    lv_obj_set_style_text_color(s_pass_ta, C_TEXT, 0);
    lv_obj_set_style_border_color(s_pass_ta, C_LINE, 0);
    lv_obj_add_event_cb(s_pass_ta, ta_focus_cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* row = w_panel(right, C_SURFACE);
    lv_obj_set_size(row, LV_PCT(100), U * 7);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(row, U + 4, 0);
    lv_obj_t* nm = w_label(row, "Brightness", &lv_font_montserrat_16, C_TEXT);
    lv_obj_set_flex_grow(nm, 1);
    lv_obj_t* dn = w_button(row, "-", nullptr, C_RAISE, C_ACCENT, bright_cb, (void*)(intptr_t)-1);
    lv_obj_set_size(dn, U * 7, U * 6);
    char b0[8];
    snprintf(b0, sizeof(b0), "%d", (int)settings_brightness());
    s_bright_lbl = w_label(row, b0, &lv_font_montserrat_16, C_TEXT);
    lv_obj_set_width(s_bright_lbl, U * 7);
    lv_obj_set_style_text_align(s_bright_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t* up = w_button(row, "+", nullptr, C_RAISE, C_ACCENT, bright_cb, (void*)(intptr_t)1);
    lv_obj_set_size(up, U * 7, U * 6);

    lv_obj_t* rst = w_button(right, "RESTART CONTROLLER", "$Bye", C_RAISE, C_WARN,
                             restart_ctrl_cb, nullptr);
    lv_obj_set_size(rst, LV_PCT(100), U * 7);

    // Keyboard is a child of the screen, not this page, so it can cover the
    // whole panel while typing rather than being clipped to the content area.
    s_kb = w_keyboard();
    lv_obj_add_event_cb(s_kb, kb_done_cb, LV_EVENT_READY, nullptr);
    lv_obj_add_event_cb(s_kb, kb_done_cb, LV_EVENT_CANCEL, nullptr);
}

void page_setup_tick() {
    lv_label_set_text(s_status_lbl, transport_detail());
}
