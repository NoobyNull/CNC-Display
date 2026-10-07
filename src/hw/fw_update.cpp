#include "hw/fw_update.h"
#include "hw/sdcard.h"
#include "hw/display_hw.h"
#include "hw/stc8.h"
#include "hw/touch.h"
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

// Firmware fetched over WiFi from the repo. (An SD-card /update.bin path used
// to sit alongside this one; it went once network OTA proved out.) WiFi exists
// ONLY inside the update: the RGB panel ISR is not IRAM-safe, so the radio
// (and the OTA writes themselves) glitch the scanout — which is fine, because
// the whole affair ends in a reboot. The radio is forced off on every exit
// path. The update is blocking, and a power loss mid-write is safe — the boot
// selector keeps pointing at the running slot until Update.end() commits.

// ---------------- progress modal ----------------
static lv_obj_t *s_m, *s_bar, *s_lbl, *s_note, *s_spin, *s_tool;
static volatile bool s_cancel = false;

// The updates run INSIDE an LVGL event callback, i.e. inside lv_timer_handler
// — which refuses re-entrant calls, silently. So nothing here may lean on the
// timer loop: painting is lv_refr_now (re-entrant safe), touch is read from
// the GT911 driver directly, and the animations are driven by hand from
// millis(). This is why the modal was once completely invisible.
static void ui_pump() {
    // WiFi activity and OTA flash writes blank the flash cache; the RGB ISR is
    // not IRAM-safe, so the DMA desyncs and the screen turns to garbage.
    // Restart it on a frame boundary a few times a second to stay readable.
    static uint32_t s_last_resync = 0;
    uint32_t now = millis();
    if (now - s_last_resync > 300) {
        s_last_resync = now;
        display_resync();
    }
    touch_poll();                              // its timer is frozen with us
    // spinner: a manually rotated 80-degree arc
    if (s_spin) {
        int a = (int)((now / 3) % 360);
        lv_arc_set_angles(s_spin, a, a + 80);
    }
    // carriage bob: integer triangle wave, no lv_anim (timer-bound)
    if (s_tool) {
        int ph = (int)((now / 40) % 24);
        lv_obj_set_y(s_tool, (ph < 12 ? ph : 24 - ph) - 4);
    }
    lv_refr_now(NULL);
}

static void ui_open(const char* title) {
    s_cancel = false;
    s_m = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_m);
    lv_obj_set_size(s_m, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(s_m, C_BG, 0);
    lv_obj_set_style_bg_opa(s_m, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_m, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_m, [](lv_event_t*) { s_cancel = true; }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* t = lv_label_create(s_m);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(t, C_TEXT, 0);
    lv_obj_align(t, LV_ALIGN_CENTER, 0, -U * 8);

    s_note = lv_label_create(s_m);
    lv_label_set_text(s_note, "updating - do not power off - reboots itself when done");
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

    // Alive-indicator arc, rotated by hand in ui_pump.
    s_spin = lv_arc_create(s_m);
    lv_obj_set_size(s_spin, U * 8, U * 8);
    lv_obj_align(s_spin, LV_ALIGN_CENTER, 0, -U * 16);
    lv_arc_set_bg_angles(s_spin, 0, 360);
    lv_obj_remove_style(s_spin, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(s_spin, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_color(s_spin, C_RAISE, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_spin, C_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(s_spin, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_spin, 4, LV_PART_INDICATOR);

    // The carriage rides the fill edge of the bar; bobbed by hand in ui_pump.
    s_tool = lv_obj_create(s_bar);
    lv_obj_remove_style_all(s_tool);
    lv_obj_set_size(s_tool, 12, 12);
    lv_obj_set_style_bg_color(s_tool, C_ACCENT, 0);
    lv_obj_set_style_bg_opa(s_tool, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_tool, 2, 0);
    lv_obj_set_pos(s_tool, 0, 2);
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

// Armed-with-escape: buttons fire on a plain tap (long-press proved flaky on
// the glass), so the modal counts down and any tap during it cancels.
static bool ui_countdown() {
    uint32_t start = millis();
    for (int sec = 3; sec > 0; sec--) {
        char b[48];
        snprintf(b, sizeof(b), "starting in %d - touch to cancel", sec);
        ui_note_set(b, C_ACCENT);
        uint32_t t0 = millis();
        while (millis() - t0 < 1000) {
            ui_pump();
            delay(20);
            // Direct read: LVGL's input timer is frozen while we block. The
            // 400ms grace lets the launching tap's finger clear the glass.
            if ((s_cancel || touch_is_down()) && millis() - start > 400) return false;
        }
    }
    return true;
}

static bool cancelled() {
    clog_add("[MSG:UPDATE: cancelled]");
    ui_close();
    display_resync();
    return false;
}

// True from the moment WiFi.begin runs. Once the radio has been up, the only
// reliable way back to a clean screen is a reboot: the radio leaves the RGB
// scanout desynced in ways display_resync cannot always undo (seen live —
// persistent scanline glitches after a failed update, unfixable by resync).
static bool s_radio_used = false;

static void fail(const char* what) {
    char e[96];
    snprintf(e, sizeof(e), "[MSG:UPDATE: %s]", what);
    clog_add(e);
    Update.abort();
    WiFi.mode(WIFI_OFF);
    // The reason goes on the glass: after a radio failure the panel reboots,
    // and the console log dies with it.
    char n[120];
    snprintf(n, sizeof(n), "FAILED: %s%s", what,
             s_radio_used ? " - rebooting" : "");
    ui_note_set(n, C_CRIT);
    uint32_t t0 = millis();
    while (millis() - t0 < 4000) { ui_pump(); delay(30); }
    if (s_radio_used) ESP.restart();
    ui_close();
    delay(50);
    display_resync();      // radio never ran; a resync is enough
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
    // The modal opens FIRST: every failure after this point is on-screen.
    // "Held the button and nothing happened" must not be a reachable state.
    ui_open("NETWORK UPDATE");
    if (!ui_countdown()) return cancelled();
    // wifi.txt on the SD overrides stored creds whenever it is present — not
    // only when none are stored, or a baked-in default with a bad password
    // could never be corrected without a cable.
    creds_from_sd();
    if (!*settings_wifi_ssid()) {
        fail("no wifi creds - put wifi.txt (ssid/pass) on the SD");
        return false;
    }
    ui_note_set("joining wifi...", C_DIM);

    s_radio_used = true;
    display_release_internal_ram();
    WiFi.mode(WIFI_STA);
    WiFi.begin(settings_wifi_ssid(), settings_wifi_pass());
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - t0 > 20000) {
            // A bare timeout is undiagnosable. Scan and say whether the SSID
            // is even visible from here (ESP32 = 2.4GHz only) and how loud:
            // not visible = wrong band / out of range; visible = bad password
            // (or an AP mode the S3 cannot join, e.g. WPA3-only).
            ui_note_set("join failed - scanning...", C_DIM);
            WiFi.disconnect();
            int n = WiFi.scanNetworks();
            char e[96];
            bool found = false;
            int  rssi  = 0;
            for (int i = 0; i < n; i++) {
                if (WiFi.SSID(i) == settings_wifi_ssid()) {
                    found = true;
                    rssi  = (int)WiFi.RSSI(i);
                    break;
                }
            }
            if (found) snprintf(e, sizeof(e), "join timeout - '%s' visible at %ddBm - check password",
                                settings_wifi_ssid(), rssi);
            else       snprintf(e, sizeof(e), "join timeout - '%s' not in scan (%d nets) - 2.4GHz? range?",
                                settings_wifi_ssid(), n);
            fail(e);
            return false;
        }
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
        // -1 is "connect failed" and says nothing more; the TLS error and the
        // internal heap are what tell DNS, handshake and out-of-RAM apart.
        char tl[48] = "";
        if (https && code < 0) tls.lastError(tl, sizeof(tl));
        char e[96]; snprintf(e, sizeof(e), "HTTP %d %s (int heap %u/%u)", code, tl,
                             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
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

    ui_note_set("downloading - do not power off", C_WARN);
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
    ui_note_set("flashing - screen goes dark - reboots itself when done", C_WARN);
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
