#include "touch.h"
#include "io_expander.h"
#include "model/screensaver.h"

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

// A healthy GT911 produces frames continuously — measured, roughly 5-10 a second
// with nothing touching it. So a stalled scan counter IS the fault signal.
//
// Worth recording how that was nearly got wrong: an earlier reading showed three
// scans after each reset and then silence, which looked exactly like "the chip
// only reports changes, so idle silence is normal". It was not. The chip was
// already wedged, and the watchdog was being blamed for the symptom it was
// detecting. Only a genuinely healthy panel showed the counter climbing while
// idle. Never characterise normal behaviour from a device you are debugging
// because it is broken.
//
// The failure mode: scans stop, no I2C errors, no mutex contention, touch_down
// frozen at whatever it last held. The s_err_run recovery below cannot see it
// because the bus is answering perfectly.
//
// That matters more here than on a tablet. The safety bar lives behind this
// input, so silent loss of touch means no HOLD and no RESET on a running
// machine.
//
// The backoff is not optional. A reset that does not revive the chip must not be
// retried for ever: at 300ms of blocked UI each, that is its own failure.
static uint32_t s_last_fresh_ms = 0;
static uint32_t s_fresh_count   = 0;
static uint32_t s_reset_count   = 0;
static const uint32_t FRESH_STALE_MS = 5000;

// Where a stall actually is. Without these, "scans stopped" has three different
// causes that look identical from outside: the poll not running at all, the poll
// losing the I2C mutex to the IMU hammering the same bus at 200Hz, or the chip
// answering cleanly while having stopped scanning. Each wants a different fix,
// so guessing between them is how an afternoon disappears.
static uint32_t s_poll_count = 0;
static uint32_t s_lock_fail  = 0;
static uint32_t s_stat_err   = 0;
static uint32_t s_fresh_at_reset = 0;
static uint8_t  s_futile_resets  = 0;

bool     touch_is_down()      { return touch_down; }
uint32_t touch_fresh_count()  { return s_fresh_count; }
uint32_t touch_reset_count()  { return s_reset_count; }
uint32_t touch_poll_count()   { return s_poll_count; }
uint32_t touch_lock_fails()   { return s_lock_fail; }
uint32_t touch_stat_errs()    { return s_stat_err; }

void touch_init() {
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
    pinMode(GT911_INT_PIN, OUTPUT);
    digitalWrite(GT911_INT_PIN, LOW);
    ioexp_write(EXIO_TP_RST, false);
    delay(100);
    digitalWrite(GT911_INT_PIN, LOW);       // hold it through the release edge
    ioexp_write(EXIO_TP_RST, true);
    delay(200);                             // datasheet wants >50ms before I2C
    pinMode(GT911_INT_PIN, INPUT);          // let the controller drive it again
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

// Full controller reset when it has gone quiet. Must be called with the mutex
// RELEASED: gt911_reset() takes it again via ioexp_write(), and it blocks for
// 300ms, so this only ever runs on the rare recovery path and never in the
// normal poll.
static void touch_watchdog() {
    if (s_last_fresh_ms == 0) return;                       // nothing seen yet
    if (millis() - s_last_fresh_ms < FRESH_STALE_MS) return;

    // Did the last reset achieve anything? If the counter has not moved since,
    // resetting again will not move it either.
    if (s_fresh_count == s_fresh_at_reset) {
        if (++s_futile_resets == 3) {
            Serial.println("[touch] GT911 will not restart — giving up, power cycle needed");
        }
        if (s_futile_resets >= 3) {
            s_last_fresh_ms = millis();   // stay quiet rather than storm
            return;
        }
    } else {
        s_futile_resets = 0;
    }

    s_last_fresh_ms  = millis();   // re-arm before the slow part, not after
    s_fresh_at_reset = s_fresh_count;
    ++s_reset_count;
    Serial.printf("[touch] no frames for %lums — resetting GT911 (%lu)\n",
                  (unsigned long)FRESH_STALE_MS, (unsigned long)s_reset_count);
    touch_down = false;
    gt911_reset();
    gt911_discover();
}

void touch_poll() {
    if (gt911_addr == 0) return;
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

    // Bit 7 is "a fresh scan is waiting". With it clear the controller simply has
    // not re-scanned since we last emptied the buffer, which is the normal case:
    // we poll at 50Hz and the GT911 refreshes slower. That is NOT a finger lift,
    // so the previous state has to stand — treating it as a release drops
    // touch_down for a poll at a time, which flickers the press faster than LVGL
    // samples the input device and makes press-and-hold on a jog key impossible.
    if (!(status & 0x80)) {
        i2c_give();
        touch_watchdog();
        return;
    }
    s_last_fresh_ms = millis();
    ++s_fresh_count;

    if (touches > 0) {
        // Touch point 0 at 0x8150
        Wire.beginTransmission(gt911_addr);
        Wire.write(0x81);
        Wire.write(0x50);
        Wire.endTransmission();
        Wire.requestFrom(gt911_addr, (uint8_t)4);
        if (Wire.available() >= 4) {
            touch_x = Wire.read() | (Wire.read() << 8);
            touch_y = Wire.read() | (Wire.read() << 8);
            touch_down = true;
        }
    } else {
        touch_down = false;         // fresh scan, zero points = a real lift
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
        data->state = LV_INDEV_STATE_REL;
        return;
    }
    data->point.x = touch_x;
    data->point.y = touch_y;
    data->state   = touch_down ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
}
