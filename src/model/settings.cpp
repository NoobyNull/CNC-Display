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

// The wifi_en / ssid / pass / host / port keys are deliberately not read back.
// The WiFi transport is gone, and any values left in NVS from an older build are
// dead weight rather than something to migrate.
void settings_load() {
    prefs.begin("cncdisp", /*readOnly=*/true);
    s_uart_en = prefs.getBool("uart_en", true);
    s_bright  = prefs.getUChar("bright", 200);
    s_vibmode  = prefs.getUChar("vibmode", 0);
    s_vib_hold = prefs.getFloat("vibhold", 0.5f);
    s_vib_peak = prefs.getFloat("vibpeak", 4.0f);
    s_vib_jerk = prefs.getFloat("vibjerk", 50.0f);
    s_scroff   = prefs.getUChar("scroff", 0);
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

uint8_t settings_screen_off_min() { return s_scroff; }
void settings_set_screen_off_min(uint8_t m) {
    if (m > 60) m = 60;
    if (m == s_scroff) return;
    s_scroff = m;
    nvs_put_uchar("scroff", m);
}
