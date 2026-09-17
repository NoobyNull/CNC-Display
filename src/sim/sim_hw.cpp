// Hardware stubs for [env:sim]. One file per concern would be five files that
// each say "there is no hardware here"; this says it once.
//
// The rule for everything below: report ABSENT/idle rather than fake success,
// unless a fake is what puts pixels on the glass. A stub that pretends the IMU
// is present would make the Setup page render a state the panel can only reach
// with a sensor plugged in — the simulator would be lying about the UI, which
// is the one thing it must not do. The SD card is the exception, and it is
// deliberate: the Run page is an empty box without a file list, and an empty
// box tells you nothing about the layout you came here to look at.

#include "hw/display_hw.h"
#include "hw/stc8.h"
#include "hw/touch.h"
#include "hw/sdcard.h"
#include "hw/imu.h"
#include "hw/fw_update.h"

#include <Arduino.h>
#include <string.h>
#include <stdio.h>

// ---------------- display ----------------
// The RGB panel, its DMA resync and the backlight all live in sim_main.cpp's
// SDL window instead. display_resync() is a no-op by definition here: there is
// no scanout to lose its place.
void display_init() {}
void lvgl_init() {}
void lvgl_tick() {}
void display_resync() {}

static uint8_t s_bl = 255;
void    set_backlight(uint8_t level) { s_bl = level; }
uint8_t get_backlight() { return s_bl; }

// Idle blanking is a backlight effect, and the simulator has no backlight to
// dim. Left inert so the window never goes black on you mid-inspection.
void screen_idle_tick(bool) {}

// ---------------- STC8 housekeeping MCU ----------------
bool stc8_init() { return false; }
bool stc8_present() { return false; }
bool stc8_cmd(uint8_t) { return false; }
void stc8_backlight(uint8_t) {}
void stc8_display(bool) {}

// ---------------- touch ----------------
// The GT911 is replaced by the SDL mouse indev in sim_main.cpp, so these are
// only here to satisfy the Diag page, which reads the counters directly.
void touch_init() {}
void gt911_reset() {}
void gt911_discover() {}
void touch_poll() {}
void touch_read_cb(lv_indev_drv_t*, lv_indev_data_t*) {}
void gt911_dump_config() {}
void i2c_bus_recover() {}
bool i2c_take(uint32_t) { return true; }
void i2c_give() {}

// Driven by sim_main.cpp so the Diag page's touch counters move under the
// mouse the way they move under a finger.
bool     sim_touch_down = false;
uint32_t sim_touch_scans = 0;
// A mouse has one pointer, so the two-finger gesture needs an explicit stand-in
// in the simulator. Right-click sets this (see sim_main.cpp).
uint8_t  sim_touch_pts = 0;

bool     touch_is_down() { return sim_touch_down; }
uint8_t  touch_points() { return sim_touch_pts; }
uint32_t touch_fresh_count() { return sim_touch_scans; }
uint32_t touch_poll_count() { return sim_touch_scans; }
uint32_t touch_lock_fails() { return 0; }
uint32_t touch_stat_errs() { return 0; }
uint8_t  touch_int_level() { return sim_touch_down ? 0 : 1; }
uint8_t  touch_last_status() { return sim_touch_down ? 0x81 : 0x00; }

// ---------------- SD card ----------------
// Only /wifi.txt is ever read from the panel card; the sim has none.
bool   sd_begin() { return true; }
bool   sd_present() { return true; }


// No file bodies exist, so a read is immediate EOF. The XModem senders treat
// that as a zero-length file and give up cleanly rather than hanging.
bool     sd_open(const char*) { return false; }
size_t   sd_read(uint8_t*, size_t) { return 0; }
void     sd_close() {}

// Writes are refused rather than redirected to a host file: a sim that quietly
// filled ./imu/0001.csv would make a recording look like it worked on hardware
// that has no card in it.
bool   sd_log_open(const char*) { return false; }
size_t sd_log_write(const void*, size_t n) { return n; }
void   sd_log_flush() {}
void   sd_log_close() {}
bool   sd_log_is_open() { return false; }
bool   sd_mkdir(const char*) { return false; }
bool   sd_exists(const char*) { return false; }

// ---------------- IMU ----------------
// Absent, which is the panel's own default: no IMU is fitted unless one is
// wired to the I2C-OUT connector. The vibration guard stays inert, and the
// Setup page shows the same "no sensor" state a bare panel shows.
bool    imu_begin() { return false; }
bool    imu_retry() { return false; }
bool    imu_present() { return false; }
void    imu_poll() {}
float   imu_last_magnitude() { return 0.0f; }
uint8_t imu_address() { return 0; }
void    imu_last_axes(float* ax, float* ay, float* az) {
    if (ax) *ax = 0.0f;
    if (ay) *ay = 0.0f;
    if (az) *az = 1.0f;
}
// The recorder is built into the sim so the Diag button's states can be laid
// out and clicked, but it can never start: sd_log_open() below refuses, which
// is the same path a panel with no card takes.
static uint16_t s_sim_rate = 50;
void     imu_set_rate(uint16_t hz) { s_sim_rate = hz; }
uint16_t imu_rate() { return s_sim_rate; }
uint32_t imu_skips() { return 0; }

// ---------------- firmware update ----------------
// Refused, not simulated. fwupd_net() ends in ESP.restart() on success; there
// is nothing here to restart, and pretending otherwise would leave the window
// in a state the panel never reaches.
bool fwupd_net() {
    Serial.println("[sim] network update is not simulated");
    return false;
}
