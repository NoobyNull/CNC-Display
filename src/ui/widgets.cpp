#include <stdlib.h>
#include "ui/widgets.h"
#include <string.h>
#include "model/FluidNCModel.h"

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

// A button's width is decided by the flex row it lands in, long after it is
// built, so its label cannot be sized at creation time. Left alone, a label
// wider than its button simply overflows and is clipped by the button's
// bounds — which is how CHANNELS lost its C and SAVE CFG lost its G on the
// Diag page, silently and only on the panel.
//
// So the caption is re-fitted whenever the button is resized: take the largest
// of the three caption sizes that actually fits, exactly as dro_layout() picks
// the readout font off cell width. Sizes must be ones lv_conf.h has enabled.
static const lv_font_t* const CAPTION[] = {
    &lv_font_montserrat_14, &lv_font_montserrat_12, &lv_font_montserrat_10
};
static const int CAPTION_N = (int)(sizeof(CAPTION) / sizeof(CAPTION[0]));

static void caption_fit_cb(lv_event_t* e) {
    lv_obj_t* btn = lv_event_get_target(e);
    lv_obj_t* lbl = (lv_obj_t*)lv_event_get_user_data(e);
    if (!lbl) return;

    // Button padding is 2 either side, plus a hair so the glyphs never touch
    // the border.
    const lv_coord_t avail = lv_obj_get_width(btn) - 8;
    if (avail <= 0) return;

    const char* txt = lv_label_get_text(lbl);
    if (!txt || !*txt) return;

    const lv_font_t* pick = CAPTION[CAPTION_N - 1];
    for (int i = 0; i < CAPTION_N; i++) {
        lv_point_t sz;
        lv_txt_get_size(&sz, txt, CAPTION[i], 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (sz.x <= avail) { pick = CAPTION[i]; break; }
    }
    if (lv_obj_get_style_text_font(lbl, LV_PART_MAIN) != pick)
        lv_obj_set_style_text_font(lbl, pick, 0);
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
    // The disabled look is a state style, not a repaint: LVGL restores the
    // original colours by itself when the state clears, so w_gate() never has
    // to remember what the button looked like. Style opacity covers the
    // children too, which is what dims the labels along with the fill.
    lv_obj_set_style_opa(b, OPA_DISABLED, LV_STATE_DISABLED);
    // Neutral when disabled: a greyed RUN must not still read as green, and
    // the reason w_gate() writes has to be legible, which it is not on a
    // dimmed accent fill.
    lv_obj_set_style_bg_color(b, C_RAISE, LV_STATE_DISABLED);
    // No transition into it: the theme's 80ms bg fade, started on a button
    // disabled the moment it is built, left the fill at its enabled colour.
    static const lv_style_prop_t no_props[] = { LV_STYLE_BG_COLOR, LV_STYLE_PROP_INV };
    static lv_style_transition_dsc_t instant;
    static bool inited = false;
    if (!inited) { lv_style_transition_dsc_init(&instant, no_props, lv_anim_path_linear, 0, 0, nullptr); inited = true; }
    lv_obj_set_style_transition(b, &instant, LV_STATE_DISABLED);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);

    lv_obj_t* col = lv_obj_create(b);
    w_flatten(col);
    lv_obj_set_size(col, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* cap = w_label(col, label, CAPTION[0], fg);
    // Backstop: even at the smallest caption size a long enough label would
    // overflow, and bleeding outside the button is worse than being cut inside
    // it. Clipping keeps the damage within the control's own bounds.
    lv_label_set_long_mode(cap, LV_LABEL_LONG_CLIP);
    lv_obj_add_event_cb(b, caption_fit_cb, LV_EVENT_SIZE_CHANGED, cap);

    // The sub-label doubles as the slot w_gate() writes a disabled reason
    // into, so stash it where the gate can find it without walking children.
    lv_obj_t* sublbl = nullptr;
    if (sub) {
        sublbl = w_label(col, sub, &lv_font_montserrat_10, lv_color_mix(fg, C_DIM, 128));
        lv_obj_set_style_text_align(sublbl, LV_TEXT_ALIGN_CENTER, 0);
        // A gate reason is far longer than the "$H" this slot normally holds,
        // and the axis buttons are narrow: wrap rather than clip, or the
        // explanation is cut off exactly where it starts to matter.
        lv_obj_set_width(sublbl, LV_PCT(100));
        lv_label_set_long_mode(sublbl, LV_LABEL_LONG_WRAP);
    }
    lv_obj_set_user_data(b, sublbl);
    return b;
}

void w_set_sub(lv_obj_t* btn, const char* text) {
    lv_obj_t* sub = btn ? (lv_obj_t*)lv_obj_get_user_data(btn) : nullptr;
    if (!sub) return;
    free(lv_obj_get_user_data(sub));             // w_gate()'s remembered text
    lv_obj_set_user_data(sub, (void*)strdup(text));
    if (!lv_obj_has_state(btn, LV_STATE_DISABLED)) lv_label_set_text(sub, text);
}

void w_gate(lv_obj_t* btn, bool enabled, const char* reason) {
    if (!btn) return;

    lv_obj_t* sub = (lv_obj_t*)lv_obj_get_user_data(btn);
    if (sub) {
        // Remember the build-time sub text once, so re-enabling restores the
        // G-code the button sends rather than leaving the reason behind.
        const char* orig = (const char*)lv_obj_get_user_data(sub);
        if (!orig) {
            orig = lv_label_get_text(sub);
            lv_obj_set_user_data(sub, (void*)strdup(orig));
            orig = (const char*)lv_obj_get_user_data(sub);
        }
        lv_label_set_text(sub, (!enabled && reason) ? reason : orig);
    }

    // Label colours are local styles on the children, which do not follow the
    // button's state, so they are swapped here: caption dimmed, reason in
    // amber. The caption's own colour is stashed in its user_data (free on a
    // w_button caption) the first time, and the sub colour is rebuilt from it
    // the way w_button() made it.
    lv_obj_t* col = lv_obj_get_child(btn, 0);
    lv_obj_t* cap = col ? lv_obj_get_child(col, 0) : nullptr;
    if (cap && lv_obj_check_type(cap, &lv_label_class)) {
        if (!lv_obj_get_user_data(cap))
            lv_obj_set_user_data(cap, (void*)(uintptr_t)lv_color_to32(
                lv_obj_get_style_text_color(cap, LV_PART_MAIN)));
        const lv_color_t fg = lv_color_hex((uint32_t)(uintptr_t)lv_obj_get_user_data(cap) & 0xFFFFFF);
        lv_obj_set_style_text_color(cap, enabled ? fg : C_DIM, 0);
        if (sub) lv_obj_set_style_text_color(sub, enabled ? lv_color_mix(fg, C_DIM, 128) : C_WARN, 0);
    }

    if (enabled) {
        lv_obj_clear_state(btn, LV_STATE_DISABLED);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    } else {
        lv_obj_add_state(btn, LV_STATE_DISABLED);
        // Clearing CLICKABLE is what actually stops the press: LV_STATE_DISABLED
        // alone still delivers LV_EVENT_CLICKED to the callback.
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    }
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

bool w_locked() { return state == Alarm || state == ConfigAlarm; }
