#include "hw/fw_update.h"
#include "hw/sdcard.h"
#include "hw/display_hw.h"
#include "hw/stc8.h"
#include "ui/theme.h"
#include "model/console_log.h"
#include "model/settings.h"

#include <Arduino.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

// Two ways in, one flash path out: /update.bin from the SD card, or the same
// file fetched over WiFi from the repo. WiFi exists ONLY inside the network
// update: the RGB panel ISR is not IRAM-safe, so the radio (and the OTA
// writes themselves) glitch the scanout — which is fine, because the whole
// affair ends in a reboot. The radio is forced off on every exit path.
//
// Both updates are blocking, and a power loss mid-write is safe — the boot
// selector keeps pointing at the running slot until Update.end() commits.

// ---------------- progress modal ----------------
static lv_obj_t *s_m, *s_bar, *s_lbl, *s_note, *s_spin, *s_tool;

// The update paths are blocking, so animations only move if we pump the LVGL
// timer loop ourselves. Called at every progress step AND in every wait loop:
// a spinner that keeps turning while the network stalls is the difference
// between "working on it" and "it's frozen, pull the plug" — which is exactly
// how updates get bricked.
static void ui_pump() {
    // WiFi activity and OTA flash writes blank the flash cache; the RGB ISR is
    // not IRAM-safe, so the panel DMA desyncs and the screen turns to garbage.
    // Restart the DMA on a frame boundary a few times a second — it desyncs
    // again immediately during heavy writes, but this keeps the modal readable
    // instead of leaving the user staring at static and reaching for the plug.
    static uint32_t s_last_resync = 0;
    uint32_t now = millis();
    if (now - s_last_resync > 300) {
        s_last_resync = now;
        display_resync();
    }
    lv_timer_handler();
}

static void tool_bob_cb(void* obj, int32_t v) { lv_obj_set_y((lv_obj_t*)obj, v); }

static void ui_open(const char* title) {
    s_m = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_m);
    lv_obj_set_size(s_m, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(s_m, C_BG, 0);
    lv_obj_set_style_bg_opa(s_m, LV_OPA_COVER, 0);

    lv_obj_t* t = lv_label_create(s_m);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(t, C_TEXT, 0);
    lv_obj_align(t, LV_ALIGN_CENTER, 0, -U * 8);

    s_note = lv_label_create(s_m);
    lv_label_set_text(s_note, "updating — do not power off — reboots itself when done");
    lv_obj_set_style_text_font(s_note, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_note, C_WARN, 0);
    lv_obj_align(s_note, LV_ALIGN_CENTER, 0, -U * 4);

    s_bar = lv_bar_create(s_m);
    lv_obj_set_size(s_bar, U * 50, U * 2);
    lv_obj_align(s_bar, LV_ALIGN_CENTER, 0, U * 2);
    lv_bar_set_range(s_bar, 0, 100);
    lv_obj_set_style_bg_color(s_bar, C_RAISE, 0);
    lv_obj_set_style_bg_color(s_bar, C_ACCENT, LV_PART_INDICATOR);

    s_lbl = lv_label_create(s_m);
    lv_label_set_text(s_lbl, "");
    lv_obj_set_style_text_font(s_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_lbl, C_DIM, 0);
    lv_obj_align(s_lbl, LV_ALIGN_CENTER, 0, U * 6);

    // Alive-indicator: keeps spinning through stalls (see ui_pump).
    s_spin = lv_spinner_create(s_m, 1200, 60);
    lv_obj_set_size(s_spin, U * 8, U * 8);
    lv_obj_align(s_spin, LV_ALIGN_CENTER, 0, -U * 16);
    lv_obj_set_style_arc_color(s_spin, C_RAISE, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_spin, C_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(s_spin, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_spin, 4, LV_PART_INDICATOR);

    // The carriage rides the fill edge of the bar, bobbing like it's cutting.
    s_tool = lv_obj_create(s_bar);
    lv_obj_remove_style_all(s_tool);
    lv_obj_set_size(s_tool, 12, 12);
    lv_obj_set_style_bg_color(s_tool, C_ACCENT, 0);
    lv_obj_set_style_bg_opa(s_tool, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_tool, 2, 0);
    lv_obj_set_pos(s_tool, 0, 2);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_tool);
    lv_anim_set_exec_cb(&a, tool_bob_cb);
    lv_anim_set_values(&a, -4, 8);
    lv_anim_set_time(&a, 260);
    lv_anim_set_playback_time(&a, 260);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
    ui_pump();
}

static void ui_note_set(const char* txt, lv_color_t c) {
    lv_label_set_text(s_note, txt);
    lv_obj_set_style_text_color(s_note, c, 0);
    ui_pump();
}

static void ui_progress(size_t done, size_t total) {
    char b[48];
    if (total) {
        int pct = (int)((uint64_t)done * 100 / total);
        lv_bar_set_value(s_bar, pct, LV_ANIM_OFF);
        // carriage tracks the fill edge
        lv_obj_set_x(s_tool, (lv_obj_get_width(s_bar) - 12) * pct / 100);
        snprintf(b, sizeof(b), "%u / %u KB", (unsigned)(done / 1024), (unsigned)(total / 1024));
    } else {
        snprintf(b, sizeof(b), "%u KB", (unsigned)(done / 1024));
    }
    lv_label_set_text(s_lbl, b);
    ui_pump();
}

static void ui_close() {
    if (s_m) { lv_obj_del(s_m); s_m = nullptr; }
}

static void fail(const char* what) {
    char e[96];
    snprintf(e, sizeof(e), "[MSG:UPDATE: %s]", what);
    clog_add(e);
    Update.abort();
    WiFi.mode(WIFI_OFF);
    ui_close();
    delay(50);
    display_resync();      // leave the screen synced, not glitching
}

// ---------------- SD card update ----------------
bool fwupd_run() {
    if (!sd_open("/update.bin")) {
        clog_add("[MSG:UPDATE: no /update.bin on the SD card]");
        return false;
    }
    size_t total = sd_size();
    if (total < 0x40000) {
        sd_close();
        clog_add("[MSG:UPDATE: /update.bin too small to be firmware]");
        return false;
    }
    ui_open("UPDATING FIRMWARE");
    ui_note_set("updating — do not power off — reboots itself when done", C_WARN);
    bool ok = Update.begin(total);
    if (ok) {
        static uint8_t buf[4096];
        size_t done = 0, n;
        while ((n = sd_read(buf, sizeof(buf))) > 0) {
            if (Update.write(buf, n) != n) { ok = false; break; }
            done += n;
            if ((done % 0x10000) < sizeof(buf)) ui_progress(done, total);
            else ui_pump();
        }
        ok = ok && done == total && Update.end(true);
    }
    sd_close();
    if (ok) {
        ui_progress(total, total);
        ui_note_set("done — rebooting", C_OK);
        delay(800);
        ESP.restart();
    }
    fail(Update.errorString());
    return false;
}

// ---------------- network update ----------------

// One-time credential bootstrap: /wifi.txt on the SD card, lines
//   ssid
//   password
//   url            (optional; default is the repo's fw branch)
// cached into NVS so later network updates need no card at all.
static bool creds_from_sd() {
    if (!sd_open("/wifi.txt")) return false;
    static char buf[256];
    size_t n = sd_read((uint8_t*)buf, sizeof(buf) - 1);
    sd_close();
    if (!n) return false;
    buf[n] = 0;
    char* lines[3] = { nullptr, nullptr, nullptr };
    int li = 0;
    lines[li++] = buf;
    for (char* p = buf; *p && li < 3; p++) {
        if (*p == '\n') { *p = 0; lines[li++] = p + 1; }
    }
    for (int i = 0; i < li; i++) {              // trim CR and trailing junk
        char* e = lines[i] + strlen(lines[i]);
        while (e > lines[i] && (e[-1] == '\r' || e[-1] == '\n' || e[-1] == ' ')) *--e = 0;
    }
    if (!lines[0] || !*lines[0] || !lines[1]) return false;
    settings_set_wifi(lines[0], lines[1]);
    if (li > 2 && lines[2] && *lines[2]) settings_set_update_url(lines[2]);
    clog_add("[MSG:UPDATE: wifi credentials read from SD]");
    return true;
}

bool fwupd_net() {
    if (!*settings_wifi_ssid() && !creds_from_sd()) {
        clog_add("[MSG:UPDATE: no wifi creds - put wifi.txt (ssid/pass) on the SD]");
        return false;
    }
    ui_open("NETWORK UPDATE");
    ui_note_set("joining wifi...", C_DIM);

    WiFi.mode(WIFI_STA);
    WiFi.begin(settings_wifi_ssid(), settings_wifi_pass());
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - t0 > 20000) { fail("wifi join timed out"); return false; }
        ui_pump();
        delay(50);
    }
    char ip[48];
    snprintf(ip, sizeof(ip), "%s  fetching...", WiFi.localIP().toString().c_str());
    ui_note_set(ip, C_DIM);

    const char* url = settings_update_url();
    WiFiClientSecure tls;
    WiFiClient      plain;
    HTTPClient http;
    bool https = strncmp(url, "https:", 6) == 0;
    if (https) tls.setInsecure();               // Cloudflare cert chain, no store
    if (!http.begin(https ? (WiFiClient&)tls : plain, url)) { fail("bad update URL"); return false; }
    // Cloudflare fronting the Gitea returns error 1010 without a browser-ish UA.
    http.setUserAgent("Mozilla/5.0 (X11; Linux) CNC-Console");
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(15000);
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        char e[48]; snprintf(e, sizeof(e), "HTTP %d from update URL", code);
        http.end(); fail(e); return false;
    }
    int total = http.getSize();                 // -1 when chunked
    if (total > 0 && total < 0x40000) { http.end(); fail("file too small to be firmware"); return false; }

    if (!Update.begin(total > 0 ? (size_t)total : UPDATE_SIZE_UNKNOWN)) {
        http.end(); fail(Update.errorString()); return false;
    }
    // Phase 1: download the whole image into PSRAM. No flash writes happen
    // here, so the screen stays essentially clean while the radio runs.
    size_t cap = total > 0 ? (size_t)total : 4 * 1024 * 1024;
    uint8_t* fwbuf = (uint8_t*)ps_malloc(cap);
    if (!fwbuf) { http.end(); fail("no PSRAM for download buffer"); return false; }

    ui_note_set("downloading — do not power off", C_WARN);
    WiFiClient* stream = http.getStreamPtr();
    size_t done = 0;
    uint32_t idle = millis();
    bool ok = true;
    while (http.connected() && done < cap && (total < 0 || done < (size_t)total)) {
        size_t avail = stream->available();
        if (!avail) {
            if (millis() - idle > 15000) { ok = false; break; }   // stalled
            ui_pump();
            delay(10);
            continue;
        }
        idle = millis();
        size_t want = avail > 4096 ? 4096 : avail;
        if (want > cap - done) want = cap - done;
        size_t n = stream->readBytes(fwbuf + done, want);
        if (!n) continue;
        done += n;
        if ((done % 0x10000) < 4096) ui_progress(done, total > 0 ? (size_t)total : 0);
    }
    http.end();
    WiFi.mode(WIFI_OFF);
    ok = ok && (total < 0 ? done > 0x40000 : done == (size_t)total);
    if (!ok) { free(fwbuf); fail("incomplete download"); return false; }

    // Phase 2: flash from RAM. The writes glitch the scanout no matter what,
    // so say what happens, cut the backlight, and burn it in the dark — the
    // reboot into the splash is the "done" signal.
    ui_progress(done, done);
    ui_note_set("flashing — screen goes dark — reboots itself when done", C_WARN);
    uint32_t t_note = millis();
    while (millis() - t_note < 1800) { ui_pump(); delay(30); }    // let it be read
    stc8_display(false);

    ok = Update.begin(done);
    if (ok) {
        size_t w = 0;
        while (w < done) {
            size_t n = done - w > 0x10000 ? 0x10000 : done - w;
            if (Update.write(fwbuf + w, n) != n) { ok = false; break; }
            w += n;
        }
        ok = ok && Update.end(true);
    }
    free(fwbuf);
    if (ok) {
        delay(200);
        ESP.restart();                     // boot restores the backlight
    }
    stc8_display(true);
    display_resync();
    fail(Update.errorString());
    return false;
}
