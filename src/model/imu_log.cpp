#include "model/imu_log.h"
#include "hw/sdcard.h"
#include "hw/imu.h"
#include "model/FluidNCModel.h"
#include "GrblParserC.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifndef FW_TAG
#define FW_TAG "dev"
#endif

// Samples held between the sensor and the card. Both ends run in loop(), so
// this is a single-threaded queue and needs no locking — the ring exists to
// absorb SD write latency, not to cross a thread boundary. 2048 records is a
// little over ten seconds at 200 Hz, which is far more slack than a FAT write
// has ever needed; the point is that an unusually long one costs samples from
// the ring rather than samples from the machine.
#define RING_N 2048

struct rec_t {
    uint32_t t_us;
    float    ax, ay, az;
    int32_t  mpos[IMULOG_AXES];   // e4 mm, MACHINE frame (work + wco)
    uint32_t feed;
    uint8_t  state;
    uint8_t  naxes;
};

static rec_t*   s_ring;
static uint16_t s_head, s_tail;      // tail == head means empty
static uint16_t s_used;

static bool     s_active;            // a card file is open and taking samples
static bool     s_tty;               // echo to the USB console
static char     s_path[32];
static char     s_status[40];
static uint32_t s_count, s_dropped;
static uint32_t s_tty_skipped;       // lines the console could not take in time
static uint32_t s_t0_ms;
static uint32_t s_next_flush_ms;

// Line assembly. One buffer for both sinks; the card gets the full record and
// the console gets a short one, for the reason in the header.
static char     s_buf[4096];
static size_t   s_len;

// Bound on one poll's worth of work. An SD write of this much costs a few
// milliseconds on a healthy card and the loop drains the FluidNC UART right
// afterwards, so the 16K RX ring (~160 ms at 1 Mbaud) is never close to full.
#define FLUSH_MAX   128
#define FLUSH_MIN    48
#define FLUSH_MS   2000   // commit the directory entry at least this often

// Recording rate. The guard runs at 50 Hz, which resolves 25 Hz — fine for
// noticing an impact, too narrow for the thing this recorder exists to measure:
// bench and gantry resonances live in the tens of Hz and a 25 Hz ceiling puts
// them at the edge of the band or outside it. 200 Hz costs several percent of
// the shared I2C bus and is visible in the UI, so it is held only while a sink
// is open and handed back afterwards.
#define IMULOG_RATE_HZ 200

// One owner of the rate, so the two sinks cannot fight over it.
static void rate_hold() {
    imu_set_rate((s_active || s_tty) ? IMULOG_RATE_HZ : 50);
}

// state_t, as the parser's own names. Kept local rather than reaching for
// my_state_string: that is a pointer into the last status report and says
// nothing about the sample it is written next to.
static const char* STATE_NAME[] = {
    "Idle", "Alarm", "Check", "Home", "Run", "Hold", "Jog",
    "DoorOpen", "DoorClosed", "Sleep", "ConfigAlarm", "Critical", "Disconnected"
};
static const int STATE_N = (int)(sizeof(STATE_NAME) / sizeof(STATE_NAME[0]));

// PSRAM first: 80K out of internal DRAM is 80K LVGL does not get, and this ring
// is written once and read once per sample — nothing that cares about the
// slower bus. (On the simulator Arduino.h maps ps_malloc to plain malloc.)
// Machine position for one axis, in e4 fixed point, always in MILLIMETRES.
//
// Two traps here. myAxes/myWco are the DISPLAY unit, not mm — show_dro() stores
// e4 inches on a machine in G20 — so an inch-mode job would otherwise log
// numbers 25.4x too small with nothing to say so. And the obvious conversion,
// toMm(), cannot be used: FluidNCModel.h declares it unconditionally but only
// DEFINES it in the non-E4 build, and this project is built -DE4_POS_T. It
// compiles and fails at link.
static int32_t mpos_mm_e4(int axis) {
    e4_t v = myAxes[axis] + myWco[axis];
    return inInches ? e4_inch_to_mm(v) : v;
}

static void* log_alloc(size_t n) {
    void* p = ps_malloc(n);
    return p ? p : malloc(n);
}

bool     imulog_active()  { return s_active; }
bool     imulog_tty()     { return s_tty; }
uint32_t imulog_count()   { return s_count; }
uint32_t imulog_dropped() { return s_dropped; }
const char* imulog_path() { return s_path; }

static void ring_reset() { s_head = s_tail = s_used = 0; }

// /imu/0001.csv, /imu/0002.csv, ... The panel has no clock, so a run cannot be
// named by when it happened; a counter at least keeps them in order and never
// silently overwrites the last session's data.
static bool next_path(char* out, size_t n) {
    if (!sd_mkdir("/imu")) return false;
    for (int i = 1; i <= 9999; i++) {
        snprintf(out, n, "/imu/%04d.csv", i);
        if (!sd_exists(out)) return true;
    }
    return false;
}

bool imulog_start() {
    if (s_active) return true;

    // Every way this can fail is checked before anything is touched: a console
    // stream may already be running through this same ring, and a failed start
    // must not cost it samples.
    if (!sd_present() || !next_path(s_path, sizeof(s_path))) {
        s_path[0] = 0;
        snprintf(s_status, sizeof(s_status), "no card");
        return false;
    }
    if (!s_ring) {
        s_ring = (rec_t*)log_alloc(sizeof(rec_t) * RING_N);
        if (!s_ring) { snprintf(s_status, sizeof(s_status), "no memory"); return false; }
        ring_reset();
    }
    if (!sd_log_open(s_path)) {
        s_path[0] = 0;
        snprintf(s_status, sizeof(s_status), "open failed");
        return false;
    }
    s_len = 0;
    s_count = s_dropped = s_tty_skipped = 0;

    // Everything needed to read the file without this source in front of you.
    // The units line matters most: the axes are in the SENSOR frame and the
    // sensor is bolted to the spindle in an orientation nobody wrote down, so
    // whoever analyses this has to recover it — and can, from the still period
    // at the head of the file plus one known single-axis move.
    char h[512];
    int n = snprintf(h, sizeof(h),
        "# cnc-disply IMU log  fw=" FW_TAG "  rate=%uHz  range=+/-8g  axes=sensor-frame\r\n"
        "# t_us   micros() at the sample, monotonic from panel boot, NOT wall clock\r\n"
        "# ax/ay/az  g, SENSOR frame (IMU is on the spindle; orientation arbitrary)\r\n"
        "# state  FluidNC state at the sample; mx..ma  MACHINE position in mm\r\n"
        "# Empty position columns mean the panel had no link (standalone sensor);\r\n"
        "# correlate against the host that was driving the machine instead.\r\n"
        "# Hold the machine still for two seconds at the start: that block is the\r\n"
        "# gravity reference the sensor frame is recovered from.\r\n"
        "t_us,ax,ay,az,state,mx,my,mz,ma,feed\r\n",
        (unsigned)imu_rate());
    sd_log_write(h, (size_t)n);
    sd_log_flush();

    s_t0_ms = millis();
    s_next_flush_ms = s_t0_ms + FLUSH_MS;
    s_active = true;
    rate_hold();
    Serial.printf("[imulog] recording %s at %u Hz\n", s_path, (unsigned)imu_rate());
    return true;
}

void imulog_stop() {
    if (!s_active) return;
    // Drain while still active, or the queued records format to nothing and the
    // tail of every run is lost. The console echo comes off for the duration:
    // imulog_poll() stops early when the UART cannot take another line, which
    // would leave this loop spinning on a ring it is no longer allowed to empty.
    bool tty = s_tty;
    s_tty = false;
    while (s_used) imulog_poll();
    if (s_len) { sd_log_write(s_buf, s_len); s_len = 0; }
    s_active = false;
    s_tty = tty;
    rate_hold();
    sd_log_close();
    Serial.printf("[imulog] stopped: %lu samples, %lu dropped, %lu console-skipped, %s\n",
                  (unsigned long)s_count, (unsigned long)s_dropped,
                  (unsigned long)s_tty_skipped, s_path);
}

void imulog_set_tty(bool on) {
    if (s_tty == on) return;
    s_tty = on;
    rate_hold();
    if (!on) return;
    // A capture that does not say what it is cannot be read six months later,
    // and a console stream has no file to put a header in but its own first
    // lines. Short form: in console mode the FluidNC link is off by definition,
    // so there is no machine position to record alongside.
    Serial.printf("# cnc-disply IMU console stream  fw=" FW_TAG "  rate=%uHz  range=+/-8g\r\n"
                  "# t_us,ax,ay,az   g, SENSOR frame (spindle-mounted, orientation arbitrary)\r\n"
                  "# hold the machine still for two seconds first: that is the gravity reference\r\n"
                  "t_us,ax,ay,az\r\n", (unsigned)imu_rate());
}

void imulog_sample(uint32_t t_us, float ax, float ay, float az) {
    if (!s_active && !s_tty) return;

    if (!s_ring) {
        s_ring = (rec_t*)log_alloc(sizeof(rec_t) * RING_N);
        if (!s_ring) return;
        ring_reset();
    }

    // A full ring means the sinks are not keeping up. Drop the NEWEST sample
    // and count it, rather than overwriting the oldest: a log with a hole in it
    // that says so is usable, and one silently resampled at an unknown rate is
    // not. The timestamps make the hole visible either way.
    if (s_used >= RING_N) { s_dropped++; return; }

    rec_t& r = s_ring[s_head];
    r.t_us = t_us;
    r.ax = ax; r.ay = ay; r.az = az;
    r.state = (uint8_t)state;

    // With the Setup "Wired UART" toggle off there is no controller on the far
    // end and therefore no position to record. Zero is not the honest answer —
    // it reads as a real measurement taken at the origin — so those columns go
    // out EMPTY and `state` says Disconnected. Running the panel as a standalone
    // sensor is a supported mode, not a fault, and its files have to say which
    // mode they were taken in.
    bool linked = (state != Disconnected);
    r.naxes = linked ? (uint8_t)(n_axes < IMULOG_AXES ? n_axes : IMULOG_AXES) : 0;
    r.feed  = linked ? myFeed : 0;
    for (int i = 0; i < IMULOG_AXES; i++) {
        r.mpos[i] = (i < r.naxes) ? mpos_mm_e4(i) : 0;
    }

    s_head = (uint16_t)((s_head + 1) % RING_N);
    s_used++;
}

static size_t fmt_full(char* out, size_t n, const rec_t& r) {
    char pos[64];
    size_t pl = 0;
    for (int i = 0; i < IMULOG_AXES; i++) {
        if (i < r.naxes) pl += snprintf(pos + pl, sizeof(pos) - pl, ",%.3f", (double)r.mpos[i] / 10000.0);
        else             pl += snprintf(pos + pl, sizeof(pos) - pl, ",");
    }
    return (size_t)snprintf(out, n, "%lu,%.4f,%.4f,%.4f,%s%s,%lu\r\n",
                            (unsigned long)r.t_us,
                            (double)r.ax, (double)r.ay, (double)r.az,
                            r.state < STATE_N ? STATE_NAME[r.state] : "?",
                            pos, (unsigned long)r.feed);
}

void imulog_poll() {
    if (!s_ring) return;

    // The console is the slow sink by a wide margin: 115200 baud is about
    // 11 KB/s and 200 Hz of short lines is about 6.6 KB/s, so it keeps up on
    // average but not through a burst. Write only what the UART can take right
    // now — blocking here would stall the whole loop, and the backlog is
    // already safe in the ring.
    int tty_budget = s_tty ? Serial.availableForWrite() : 0;

    int drained = 0;
    while (s_used && drained < FLUSH_MAX) {
        const rec_t& r = s_ring[s_tail];

        if (s_tty) {
            char line[48];
            int n = snprintf(line, sizeof(line), "%lu,%.4f,%.4f,%.4f\r\n",
                             (unsigned long)r.t_us, (double)r.ax, (double)r.ay, (double)r.az);
            if (n <= tty_budget) {
                Serial.write((const uint8_t*)line, (size_t)n);
                tty_budget -= n;
            } else if (!s_active) {
                // Console only: leave the record in the ring and come back for
                // it. Back-pressure is the right answer when the console IS the
                // recording — a sample skipped here is simply gone.
                break;
            } else {
                // A card is recording, so the card is the record and the console
                // is a monitor. It must never hold up the file: skip the line,
                // count it, and let the reader see the gap for what it is.
                s_tty_skipped++;
            }
        }

        if (s_active) {
            if (s_len + 160 > sizeof(s_buf)) { sd_log_write(s_buf, s_len); s_len = 0; }
            s_len += fmt_full(s_buf + s_len, sizeof(s_buf) - s_len, r);
        }

        s_tail = (uint16_t)((s_tail + 1) % RING_N);
        s_used--;
        s_count++;
        drained++;
    }

    if (!s_active) { s_len = 0; return; }

    // Dribbles cost a whole 512-byte card sector each; hold short buffers back
    // unless the run is ending or the ring is backing up.
    if (s_len && (s_used >= FLUSH_MIN || s_len > sizeof(s_buf) / 2)) {
        sd_log_write(s_buf, s_len);
        s_len = 0;
    }

    uint32_t now = millis();
    if ((int32_t)(now - s_next_flush_ms) >= 0) {
        s_next_flush_ms = now + FLUSH_MS;
        if (s_len) { sd_log_write(s_buf, s_len); s_len = 0; }
        sd_log_flush();
    }
}

const char* imulog_status() {
    if (s_active) {
        snprintf(s_status, sizeof(s_status), "REC %lus  %lu%s",
                 (unsigned long)((millis() - s_t0_ms) / 1000),
                 (unsigned long)s_count,
                 s_dropped ? "  DROPS" : "");
    } else if (!sd_present()) {
        snprintf(s_status, sizeof(s_status), "no card");
    } else if (s_count) {
        snprintf(s_status, sizeof(s_status), "%lu saved%s",
                 (unsigned long)s_count, s_dropped ? "  DROPS" : "");
    } else {
        snprintf(s_status, sizeof(s_status), "%s", s_tty ? "tty only" : "idle");
    }
    return s_status;
}
