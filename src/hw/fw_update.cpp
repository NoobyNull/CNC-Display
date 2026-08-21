#include "hw/fw_update.h"
#include "hw/sdcard.h"
#include "ui/theme.h"
#include "model/console_log.h"

#include <Arduino.h>
#include <Update.h>
#include <lvgl.h>
#include <stdio.h>

// The whole update is blocking, which is fine: nothing else matters while the
// firmware is being replaced, and a power loss mid-write is already safe — the
// boot selector keeps pointing at the running slot until Update.end() commits.

static void paint(lv_obj_t* bar, lv_obj_t* lbl, size_t done, size_t total) {
    lv_bar_set_value(bar, (int32_t)((uint64_t)done * 100 / total), LV_ANIM_OFF);
    char b[40];
    snprintf(b, sizeof(b), "%u / %u KB", (unsigned)(done / 1024), (unsigned)(total / 1024));
    lv_label_set_text(lbl, b);
    lv_refr_now(NULL);
}

bool fwupd_run() {
    if (!sd_open("/update.bin")) {
        clog_add("[MSG:UPDATE: no /update.bin on the SD card]");
        return false;
    }
    size_t total = sd_size();
    if (total < 0x40000) {           // a real app image is megabytes, not KB
        sd_close();
        clog_add("[MSG:UPDATE: /update.bin too small to be firmware]");
        return false;
    }

    lv_obj_t* m = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(m);
    lv_obj_set_size(m, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(m, C_BG, 0);
    lv_obj_set_style_bg_opa(m, LV_OPA_COVER, 0);

    lv_obj_t* t = lv_label_create(m);
    lv_label_set_text(t, "UPDATING FIRMWARE");
    lv_obj_set_style_text_font(t, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(t, C_TEXT, 0);
    lv_obj_align(t, LV_ALIGN_CENTER, 0, -U * 8);

    lv_obj_t* w = lv_label_create(m);
    lv_label_set_text(w, "do not power off");
    lv_obj_set_style_text_font(w, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(w, C_WARN, 0);
    lv_obj_align(w, LV_ALIGN_CENTER, 0, -U * 4);

    lv_obj_t* bar = lv_bar_create(m);
    lv_obj_set_size(bar, U * 50, U * 2);
    lv_obj_align(bar, LV_ALIGN_CENTER, 0, U * 2);
    lv_bar_set_range(bar, 0, 100);
    lv_obj_set_style_bg_color(bar, C_RAISE, 0);
    lv_obj_set_style_bg_color(bar, C_ACCENT, LV_PART_INDICATOR);

    lv_obj_t* lbl = lv_label_create(m);
    lv_label_set_text(lbl, "");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl, C_DIM, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, U * 6);
    lv_refr_now(NULL);

    bool ok = Update.begin(total);
    if (ok) {
        static uint8_t buf[4096];
        size_t done = 0, n;
        while ((n = sd_read(buf, sizeof(buf))) > 0) {
            if (Update.write(buf, n) != n) { ok = false; break; }
            done += n;
            if ((done % 0x10000) < sizeof(buf)) paint(bar, lbl, done, total);
        }
        ok = ok && done == total && Update.end(true);
    }
    sd_close();

    if (ok) {
        paint(bar, lbl, total, total);
        lv_label_set_text(w, "done — rebooting");
        lv_obj_set_style_text_color(w, C_OK, 0);
        lv_refr_now(NULL);
        delay(800);
        ESP.restart();
    }

    char e[64];
    snprintf(e, sizeof(e), "[MSG:UPDATE: failed, %s]", Update.errorString());
    clog_add(e);
    Update.abort();
    lv_obj_del(m);
    return false;
}
