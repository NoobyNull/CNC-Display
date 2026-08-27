#include "model/settings.h"
#include "hw/display_hw.h"
#include <Preferences.h>
#include <string.h>

static Preferences prefs;

static bool     s_uart_en = true;
static uint8_t  s_bright   = 200;
static uint8_t  s_vibmode  = 0;   // monitor until deliberately armed
static float    s_vib_hold = 0.5f;    // CNC-TW THRESHOLD_HOLD_RMS_G
static float    s_vib_peak = 4.0f;    // CNC-TW THRESHOLD_ESTOP_PEAK_G
static float    s_vib_jerk = 50.0f;   // CNC-TW THRESHOLD_ESTOP_JERK_GPS
static uint8_t  s_scroff   = 0;       // minutes; 0 = never blank

// Rail page visibility. The controller is asked what hardware exists, but it
// does not get the last word: an operator who never probes can put the page
// away, and one whose config under-reports can put it back. All on by default.
static uint16_t s_pages = 0xFFFF;

// Half speed by default: fast enough to be useful, slow enough that the
// one-tick release coast stays small on an unfamiliar machine.
static uint8_t s_jogpct = 50;

// WiFi creds exist ONLY for the network firmware update (fw_update.cpp): the
// radio never runs alongside the live display. The old WiFi-transport keys
// (wifi_en / host / port) stay dead.
// Defaults are THIS machine's shop network and the repo's tokenized
// latest-firmware URL (read-only token, revocable in Gitea as "cnc-panel-fw").
// A wifi.txt on the SD card overrides all three — see fw_update.cpp.
static char s_ssid[33] = "YAX-Net";
static char s_pass[65] = "REDACTED-WIFI";
static char s_url[161] = "https://git.yax.family/api/v1/repos/matthew/CNC-Disply/raw/update.bin?ref=fw&token=REDACTEDREDACTEDREDACTEDREDACTEDREDACTED";

void settings_load() {
    prefs.begin("cncdisp", /*readOnly=*/true);
    s_uart_en = prefs.getBool("uart_en", true);
    s_bright  = prefs.getUChar("bright", 200);
    s_vibmode  = prefs.getUChar("vibmode", 0);
    s_vib_hold = prefs.getFloat("vibhold", 0.5f);
    s_vib_peak = prefs.getFloat("vibpeak", 4.0f);
    s_vib_jerk = prefs.getFloat("vibjerk", 50.0f);
    s_scroff   = prefs.getUChar("scroff", 0);
    s_pages    = (uint16_t)prefs.getUShort("pages", 0xFFFF);
    s_jogpct   = prefs.getUChar("jogpct", 50);
    // An empty or absent NVS key must never shadow the baked defaults.
    char tmp[161];
    if (prefs.getString("upd_ssid", tmp, sizeof(tmp)) && tmp[0]) strncpy(s_ssid, tmp, sizeof(s_ssid) - 1);
    if (prefs.getString("upd_pass", tmp, sizeof(tmp)) && tmp[0]) strncpy(s_pass, tmp, sizeof(s_pass) - 1);
    if (prefs.getString("upd_url",  tmp, sizeof(tmp)) && tmp[0]) strncpy(s_url,  tmp, sizeof(s_url)  - 1);
    prefs.end();
}

// An NVS write disables the flash cache while it erases and programs, and the
// RGB panel's ISR is not IRAM-safe in arduino-esp32's prebuilt libs, so the DMA
// loses its place and the frame shifts. Resync after every write rather than at
// the call sites: brightness is adjusted with +/- buttons that each write a key,
// so this path is hit far more often than "a few times in a machine's life".
static void nvs_put_bool(const char* k, bool v) {
    prefs.begin("cncdisp", false); prefs.putBool(k, v); prefs.end();
    display_resync();
}

static void nvs_put_uchar(const char* k, uint8_t v) {
    prefs.begin("cncdisp", false); prefs.putUChar(k, v); prefs.end();
    display_resync();
}

static void nvs_put_str(const char* k, const char* v) {
    prefs.begin("cncdisp", false); prefs.putString(k, v); prefs.end();
    display_resync();
}

const char* settings_wifi_ssid()  { return s_ssid; }
const char* settings_wifi_pass()  { return s_pass; }
const char* settings_update_url() { return s_url; }

void settings_set_wifi(const char* ssid, const char* pass) {
    if (!ssid || !pass) return;
    strncpy(s_ssid, ssid, sizeof(s_ssid) - 1); s_ssid[sizeof(s_ssid) - 1] = 0;
    strncpy(s_pass, pass, sizeof(s_pass) - 1); s_pass[sizeof(s_pass) - 1] = 0;
    nvs_put_str("upd_ssid", s_ssid);
    nvs_put_str("upd_pass", s_pass);
}

void settings_set_update_url(const char* url) {
    if (!url || !*url) return;
    strncpy(s_url, url, sizeof(s_url) - 1); s_url[sizeof(s_url) - 1] = 0;
    nvs_put_str("upd_url", s_url);
}

bool settings_uart_enabled() { return s_uart_en; }
void settings_set_uart_enabled(bool on) {
    if (on == s_uart_en) return;
    s_uart_en = on; nvs_put_bool("uart_en", on);
}

uint8_t settings_vib_mode() { return s_vibmode; }
void settings_set_vib_mode(uint8_t m) {
    if (m > 2 || m == s_vibmode) return;
    s_vibmode = m;
    nvs_put_uchar("vibmode", m);
}

float settings_vib_hold() { return s_vib_hold; }
float settings_vib_peak() { return s_vib_peak; }
float settings_vib_jerk() { return s_vib_jerk; }

// Clamped rather than trusted. These decide whether a running machine gets
// stopped: a zero or negative threshold would either fire constantly or never,
// and both are worse than a badly-chosen but sane number.
void settings_set_vib_thresholds(float hold_g, float peak_g, float jerk_gps) {
    if (hold_g   < 0.05f) hold_g   = 0.05f;   if (hold_g   > 5.0f)   hold_g   = 5.0f;
    if (peak_g   < 0.5f)  peak_g   = 0.5f;    if (peak_g   > 16.0f)  peak_g   = 16.0f;
    if (jerk_gps < 5.0f)  jerk_gps = 5.0f;    if (jerk_gps > 500.0f) jerk_gps = 500.0f;

    if (hold_g == s_vib_hold && peak_g == s_vib_peak && jerk_gps == s_vib_jerk) return;
    s_vib_hold = hold_g; s_vib_peak = peak_g; s_vib_jerk = jerk_gps;

    prefs.begin("cncdisp", false);
    prefs.putFloat("vibhold", s_vib_hold);
    prefs.putFloat("vibpeak", s_vib_peak);
    prefs.putFloat("vibjerk", s_vib_jerk);
    prefs.end();
    display_resync();
}

uint8_t settings_brightness() { return s_bright; }
void settings_set_brightness(uint8_t b) {
    if (b == s_bright) return;      // the +/- buttons clamp, so this repeats at the ends
    s_bright = b;
    nvs_put_uchar("bright", b);
}

bool settings_page_enabled(int page_id) {
    if (page_id < 0 || page_id > 15) return true;
    return (s_pages >> page_id) & 1u;
}

void settings_set_page_enabled(int page_id, bool on) {
    if (page_id < 0 || page_id > 15) return;
    const uint16_t bit = (uint16_t)(1u << page_id);
    const uint16_t next = on ? (uint16_t)(s_pages | bit) : (uint16_t)(s_pages & ~bit);
    if (next == s_pages) return;
    s_pages = next;
    prefs.begin("cncdisp", false); prefs.putUShort("pages", s_pages); prefs.end();
    display_resync();
}

uint8_t settings_jog_pct() { return s_jogpct; }

// Clamped, not trusted. 0% would make a held jog key do nothing at all, which
// reads as a dead panel; above 100% would ask for more than the axis can do
// and the controller would clip it anyway.
void settings_set_jog_pct(uint8_t pct) {
    if (pct < 5)   pct = 5;
    if (pct > 100) pct = 100;
    if (pct == s_jogpct) return;
    s_jogpct = pct;
    nvs_put_uchar("jogpct", pct);
}

uint8_t settings_screen_off_min() { return s_scroff; }
void settings_set_screen_off_min(uint8_t m) {
    if (m > 60) m = 60;
    if (m == s_scroff) return;
    s_scroff = m;
    nvs_put_uchar("scroff", m);
}
