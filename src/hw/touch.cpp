#include "touch.h"
#include "io_expander.h"
#include "model/screensaver.h"
#include "model/corner_gesture.h"
#include "ui/theme.h"

#include <Arduino.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Adapted from Elcrow-Display-hotkeys/display/touch.cpp with its gesture layer
// removed. That layer held LVGL's press report back by 120ms to tell taps from
// swipes, and suppressed input entirely once a finger moved 30px. Both are wrong
// here: a jog key must register the instant it is touched, and a swipe starting
// on a jog key would be a page change with the spindle running. The GT911 read
// path itself is unchanged.

// ============================================================
// I2C mutex — protects all Wire operations (GT911 shares the bus
// with the io-extender on SDA=8 / SCL=9)
// ============================================================
static SemaphoreHandle_t i2c_mutex = NULL;

bool i2c_take(uint32_t timeout_ms) {
    if (!i2c_mutex) return false;
    return xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void i2c_give() {
    if (i2c_mutex) xSemaphoreGive(i2c_mutex);
}

// Tear the I2C master down and bring it back up. A device that holds SDA or SCL
// low — a miswired breakout on the I2C terminal will do it — leaves the ng
// driver returning ESP_ERR_INVALID_STATE on every transaction from then on, and
// since the GT911 shares this bus that silently costs every touch input on the
// panel, safety bar included. Observed on hardware, not hypothetical.
//
// Caller must hold the mutex.
void i2c_bus_recover() {
    Wire.end();
    Wire.begin(GT911_SDA_PIN, GT911_SCL_PIN);
    Wire.setClock(400000);
}

// ============================================================
// GT911 touch — direct I2C via Wire
// ============================================================
static uint8_t gt911_addr = 0;

static volatile bool     touch_down = false;
static volatile uint16_t touch_x = 0;
static volatile uint16_t touch_y = 0;

static uint32_t touch_err_timer = 0;   // rate-limit error logging
static uint16_t s_err_run = 0;         // consecutive I2C failures
static uint8_t  s_last_status = 0;     // last 0x814E byte, for the stall log

// THIS CHIP REPORTS ONLY WHEN SOMETHING HAPPENS. Idle silence is normal: no
// finger, no frames, indefinitely. Measured on this panel — untouched it emits
// three frames after reset and then nothing at all, while a finger on the glass
// produces frames at the configured 8ms refresh (status 0x81, INT asserted) and
// lifts cleanly. Nothing is wrong with it and nothing needs restarting.
//
// There used to be a watchdog here that hard-reset the controller after 5s
// without a frame. It was built on the opposite belief — that a healthy GT911
// free-runs at 5-10 frames/s untouched — and that belief was measured from a
// panel already assumed broken. It was never true. The watchdog therefore reset
// a working chip every 5 seconds for ever: 300ms of blocked UI each time and a
// forced touch_down = false that ate any press in flight. Its own backoff could
// never save it, because each reset yielded three frames, which looked like
// progress and cleared the futile-reset counter.
//
// So: do not add a no-frames watchdog back. A quiet GT911 is an idle GT911. Real
// bus faults are a different signal entirely and the s_err_run recovery below
// catches those, because they show up as I2C errors rather than as silence.
static uint32_t s_fresh_count = 0;

// Where a stall actually is. Without these, "scans stopped" has three different
// causes that look identical from outside: the poll not running at all, the poll
// losing the I2C mutex to the IMU hammering the same bus at 200Hz, or the chip
// answering cleanly while having stopped scanning. Each wants a different fix,
// so guessing between them is how an afternoon disappears.
static uint32_t s_poll_count = 0;
static uint32_t s_lock_fail  = 0;
static uint32_t s_stat_err   = 0;

// INT tells us a frame is ready instead of us asking 50 times a second on a bus
// the IMU is already using. The ISR only raises a flag — I2C cannot be done from
// an ESP32 ISR — so the timer still does the reading, it just stops reading for
// nothing.
static volatile bool s_int_flag = false;
static uint32_t      s_last_read_ms = 0;

// Interrupts are the latency path, not the only path. A missed or mis-polarised
// edge would otherwise be indistinguishable from a wedged controller, and the
// stall watchdog needs a heartbeat to measure against. 100ms idle polling keeps
// both honest and still cuts idle bus traffic fivefold.
static const uint32_t IDLE_POLL_MS = 100;

// A physical tap can begin and end between two LVGL reads and never be seen:
// touch polls at 50Hz, LVGL reads at 30ms, and nothing keeps them in phase. The
// latch guarantees every press reaches LVGL at least once, which is the
// difference between a button that works and one that "sometimes" works.
static volatile bool s_press_latched = false;

// Frames still to log after a reset. "Three frames then silence" is the whole
// mystery, so the interval between those three is the evidence that separates
// "the chip powers down after ~300ms" from "it emits a burst and quits".
static uint8_t s_frame_log = 0;

// Four fingers in the four corners opens the console log. Detection lives here
// rather than in LVGL because LVGL's pointer input is single-contact by design
// -- it never sees fingers two through five.
static cg_state_t      s_corner;
static volatile bool   s_corner_fired = false;

bool touch_take_corner_gesture() {
    if (!s_corner_fired) return false;
    s_corner_fired = false;   // read-and-clear: one open per gesture
    return true;
}

static void IRAM_ATTR gt911_isr() { s_int_flag = true; }

static void gt911_int_attach() {
    pinMode(GT911_INT_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(GT911_INT_PIN), gt911_isr, CHANGE);
}

static void gt911_int_detach() {
    detachInterrupt(digitalPinToInterrupt(GT911_INT_PIN));
}

bool     touch_is_down()      { return touch_down; }
uint32_t touch_fresh_count()  { return s_fresh_count; }
uint32_t touch_poll_count()   { return s_poll_count; }
uint32_t touch_lock_fails()   { return s_lock_fail; }
uint32_t touch_stat_errs()    { return s_stat_err; }
uint8_t  touch_int_level()    { return digitalRead(GT911_INT_PIN) ? 1 : 0; }
uint8_t  touch_last_status()  { return s_last_status; }

void touch_init() {
    cg_init(&s_corner, SCREEN_W, SCREEN_H);
    i2c_mutex = xSemaphoreCreateMutex();
    configASSERT(i2c_mutex != NULL);
    Serial.println("I2C mutex created");
}

// The GT911 latches its I2C address from the INT pin at the moment reset is
// released: INT low picks 0x5D, INT high picks 0x14. RST hangs off the
// io-extender, INT is a real GPIO, so both have to be driven by hand. Must run
// before gt911_discover() and before anything else on the bus expects an
// answer from the touch controller.
void gt911_reset() {
    // The address strap needs INT driven, so the interrupt cannot stay attached
    // across a reset — and the watchdog resets while running.
    gt911_int_detach();
    pinMode(GT911_INT_PIN, OUTPUT);
    digitalWrite(GT911_INT_PIN, LOW);
    ioexp_write(EXIO_TP_RST, false);
    delay(100);
    digitalWrite(GT911_INT_PIN, LOW);       // hold it through the release edge
    ioexp_write(EXIO_TP_RST, true);
    delay(200);                             // datasheet wants >50ms before I2C
    gt911_int_attach();                     // let the controller drive it again
    s_int_flag = true;                      // read once now, do not wait for an edge
    s_frame_log = 8;
}

// Probe 0x5D and 0x14. Call AFTER gt911_reset().
void gt911_discover() {
    uint8_t addrs[] = { 0x5D, 0x14 };
    for (int attempt = 0; attempt < 10; attempt++) {
        for (int i = 0; i < 2; i++) {
            if (!i2c_take(50)) continue;
            Wire.beginTransmission(addrs[i]);
            uint8_t err = Wire.endTransmission();
            i2c_give();

            if (err == 0) {
                gt911_addr = addrs[i];
                Serial.printf("GT911 found at 0x%02X (attempt %d)\n", gt911_addr, attempt);
                return;
            }
        }
        delay(100);
    }
    Serial.println("GT911 not found!");
}

// Read `len` bytes from 16-bit register `reg`, in Wire-buffer-sized chunks.
// Caller must hold the mutex.
static bool gt911_read(uint16_t reg, uint8_t* buf, size_t len) {
    while (len) {
        uint8_t n = len > 32 ? 32 : (uint8_t)len;
        Wire.beginTransmission(gt911_addr);
        Wire.write(reg >> 8);
        Wire.write(reg & 0xFF);
        if (Wire.endTransmission() != 0) return false;
        if (Wire.requestFrom(gt911_addr, n) != n) return false;
        for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
        buf += n; reg += n; len -= n;
    }
    return true;
}

// This driver has never written a GT911 configuration — it runs on whatever the
// chip has in its own storage, and we drive INT as an interrupt without knowing
// what mode the chip thinks it is in. Print the stored config before ever
// considering writing one: a bad checksum at 0x80FF bricks touch until the chip
// is re-flashed, so nothing goes back the other way on a guess.
//
// The bytes that matter for the stall: 0x804D bits 0-1 (INT trigger mode) and
// 0x8055 (seconds of no touch before the chip enters low power, where it answers
// I2C perfectly while having stopped scanning — exactly the observed symptom).
void gt911_dump_config() {
    if (gt911_addr == 0) { Serial.println("[gt911] no chip to dump"); return; }
    if (!Serial) return;            // no host reading; do not stall boot for it

    static const uint16_t CFG_BASE = 0x8047, CFG_END = 0x80FF;
    uint8_t id[11];
    uint8_t cfg[CFG_END - CFG_BASE + 1];

    if (!i2c_take(200)) { Serial.println("[gt911] dump: bus busy"); return; }
    bool ok = gt911_read(0x8140, id, sizeof(id)) &&
              gt911_read(CFG_BASE, cfg, sizeof(cfg));
    i2c_give();
    if (!ok) { Serial.println("[gt911] dump: read failed"); return; }

    // Serial.setTxTimeoutMs(0) in setup() drops writes rather than blocking the
    // UI, which would silently eat half the dump. Block just for this.
    Serial.setTxTimeoutMs(100);

    Serial.printf("[gt911] id=%c%c%c%c fw=0x%02X%02X res=%ux%u vendor=0x%02X\n",
                  id[0], id[1], id[2], id[3], id[5], id[4],
                  (unsigned)(id[6] | (id[7] << 8)), (unsigned)(id[8] | (id[9] << 8)), id[10]);

    static const char* int_mode[] = { "rising", "falling", "low-level", "high-level" };
    uint8_t sw1 = cfg[0x804D - CFG_BASE];
    Serial.printf("[gt911] cfgver=0x%02X x=%u y=%u fingers=%u sw1=0x%02X int=%s sw2=0x%02X\n",
                  cfg[0], (unsigned)(cfg[1] | (cfg[2] << 8)), (unsigned)(cfg[3] | (cfg[4] << 8)),
                  cfg[0x804C - CFG_BASE], sw1, int_mode[sw1 & 3], cfg[0x804E - CFG_BASE]);

    // Checksum is two's complement of 0x8047..0x80FE, so the whole block sums to
    // 0 mod 256 when it is intact.
    uint8_t sum = 0;
    for (size_t i = 0; i < sizeof(cfg); i++) sum += cfg[i];
    Serial.printf("[gt911] lowpower=%us refresh=%ums touchlvl=%u leavelvl=%u chksum=0x%02X sum=%u%s\n",
                  cfg[0x8055 - CFG_BASE], 5 + cfg[0x8056 - CFG_BASE],
                  cfg[0x8053 - CFG_BASE], cfg[0x8054 - CFG_BASE],
                  cfg[0x80FF - CFG_BASE], sum, sum == 0 ? " OK" : " BAD");

    for (size_t i = 0; i < sizeof(cfg); i += 16) {
        Serial.printf("[gt911] %04X:", (unsigned)(CFG_BASE + i));
        for (size_t j = 0; j < 16 && i + j < sizeof(cfg); j++) Serial.printf(" %02X", cfg[i + j]);
        Serial.println();
    }

    Serial.setTxTimeoutMs(0);
}

void touch_poll() {
    if (gt911_addr == 0) return;

    // Read on an edge, or on the idle heartbeat. Nothing else.
    const uint32_t now = millis();
    if (!s_int_flag && (now - s_last_read_ms) < IDLE_POLL_MS) return;
    s_int_flag     = false;
    s_last_read_ms = now;

    ++s_poll_count;
    if (!i2c_take(10)) { ++s_lock_fail; return; }   // bus busy, skip this cycle

    // Status register 0x814E
    Wire.beginTransmission(gt911_addr);
    Wire.write(0x81);
    Wire.write(0x4E);
    int err = Wire.endTransmission();
    if (err != 0) {
        ++s_stat_err;
        // A run of failures means the bus is wedged, not that one read was
        // unlucky — most likely a device on the I2C terminal holding a line low.
        // Recover it rather than losing touch until the next power cycle: the
        // safety bar lives behind this input. 25 polls is about half a second,
        // long enough that a transient never triggers it.
        if (++s_err_run >= 25) {
            s_err_run = 0;
            i2c_bus_recover();
            Serial.println("[i2c] bus wedged — re-initialised");
        }
        i2c_give();
        if (millis() - touch_err_timer > 2000) {
            touch_err_timer = millis();
            Serial.printf("GT911 i2c err: %d\n", err);
        }
        touch_down = false;
        return;
    }
    s_err_run = 0;

    Wire.requestFrom(gt911_addr, (uint8_t)1);
    if (!Wire.available()) {
        i2c_give();
        touch_down = false;
        return;
    }

    uint8_t status  = Wire.read();
    uint8_t touches = status & 0x0F;
    s_last_status   = status;

    // Bit 7 is "a fresh scan is waiting". Clear means the controller has nothing
    // new to say — either it has not re-scanned since we emptied the buffer, or
    // nothing is happening at all, which is its resting state. That is NOT a
    // finger lift, so the previous state has to stand: treating it as a release
    // drops touch_down for a poll at a time, which flickers the press faster
    // than LVGL samples the input and makes press-and-hold on a jog key
    // impossible. A real lift arrives as a fresh scan reporting zero points.
    if (!(status & 0x80)) {
        i2c_give();
        return;
    }
    ++s_fresh_count;
    if (s_frame_log) {
        --s_frame_log;
        Serial.printf("[touch] frame %lu t=%lu status=0x%02X\n",
                      (unsigned long)s_fresh_count, (unsigned long)now, status);
    }

    if (touches > 0) {
        // Points start at 0x8150, eight bytes each:
        //   track_id, x_lo, x_hi, y_lo, y_hi, size_lo, size_hi, reserved
        // Point 0 still feeds LVGL exactly as before; the rest exist only so
        // the corner gesture can see all four contacts. Reading 8*n instead of
        // 4 costs a few hundred microseconds and only when fingers are down.
        if (touches > CG_MAX_POINTS) touches = CG_MAX_POINTS;
        const uint8_t want = (uint8_t)(touches * 8);

        Wire.beginTransmission(gt911_addr);
        Wire.write(0x81);
        Wire.write(0x50);
        Wire.endTransmission();
        Wire.requestFrom(gt911_addr, want);
        if (Wire.available() >= want) {
            cg_point_t pts[CG_MAX_POINTS];
            for (uint8_t i = 0; i < touches; i++) {
                Wire.read();                                    // track id
                pts[i].x = Wire.read() | (Wire.read() << 8);
                pts[i].y = Wire.read() | (Wire.read() << 8);
                Wire.read(); Wire.read();                       // size
                Wire.read();                                    // reserved
            }
            touch_x = pts[0].x;
            touch_y = pts[0].y;
            if (!touch_down) {
                s_press_latched = true;   // a new press must not be missable
                Serial.printf("[touch] press %u,%u t=%lu\n",
                              touch_x, touch_y, (unsigned long)now);
            }
            touch_down = true;

            // Not while blanked: the wake tap is swallowed for LVGL, so a
            // gesture that still fired would act on a screen nobody can see.
            if (!screensaver_is_off() && cg_update(&s_corner, pts, touches, now)) {
                s_corner_fired = true;
                Serial.printf("[touch] four-corner hold t=%lu\n", (unsigned long)now);
            }
        }
    } else {
        if (touch_down) Serial.printf("[touch] lift t=%lu\n", (unsigned long)now);
        touch_down = false;         // fresh scan, zero points = a real lift
        cg_reset(&s_corner);        // all fingers gone: re-arm
    }

    // Clear the status register, but only having consumed a fresh scan.
    Wire.beginTransmission(gt911_addr);
    Wire.write(0x81);
    Wire.write(0x4E);
    Wire.write(0x00);
    Wire.endTransmission();

    i2c_give();
}

// LVGL input driver callback. Reports cached state with no I2C and no delay:
// press is reported on the first poll that sees a finger.
void touch_read_cb(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    (void)drv;
    if (!screensaver_touch_allowed(touch_down, millis())) {
        // Blanked, or still holding the press that woke it. A finger on the
        // glass is not idleness, so keep the clock fresh -- otherwise the panel
        // would re-blank underneath a held finger.
        if (touch_down) lv_disp_trig_activity(NULL);
        s_press_latched = false;   // a swallowed wake tap must not resurface later
        data->state = LV_INDEV_STATE_REL;
        return;
    }

    data->point.x = touch_x;
    data->point.y = touch_y;

    if (touch_down) {
        s_press_latched = false;         // LVGL is seeing it live
        data->state = LV_INDEV_STATE_PR;
        return;
    }
    if (s_press_latched) {
        // Pressed and released between two reads. Report the press at the
        // coordinates it happened at; the release follows on the next read and
        // LVGL turns the pair into a click.
        s_press_latched = false;
        data->state = LV_INDEV_STATE_PR;
        return;
    }
    data->state = LV_INDEV_STATE_REL;
}
