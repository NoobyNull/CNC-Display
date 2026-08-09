#include "model/file_upload.h"
#include "hw/sdcard.h"
#include "transport/xmodem_tx.h"
#include "transport/transport.h"
#include "model/FluidNCModel.h"
#include "model/console_log.h"
#include <Arduino.h>
#include <stdio.h>

static bool     s_busy = false;
static bool     s_ok   = false;
static uint32_t s_total = 0;
static uint32_t s_start_ms = 0;
static char     s_status[64] = "idle";
static char     s_remote[128];

static void tx(uint8_t c) { fnc_putchar(c); }
static size_t reader(uint8_t* dst, size_t max) { return sd_read(dst, max); }

bool upload_busy() { return s_busy; }
bool upload_ok()   { return s_ok; }
const char* upload_status() { return s_status; }

int upload_percent() {
    if (!s_total) return 0;
    uint32_t sent = (uint32_t)xmodem_tx_bytes();
    return sent >= s_total ? 100 : (int)((sent * 100) / s_total);
}

bool upload_start(const char* local_path, const char* remote_path) {
    if (s_busy) return false;
    if (!sd_present()) { snprintf(s_status, sizeof(s_status), "no SD card"); return false; }

    // $XR is gated on allowConfigStates — Idle or Alarm only.
    if (!(state == Idle || state == Alarm)) {
        snprintf(s_status, sizeof(s_status), "machine must be idle");
        clog_add("[MSG:Upload needs Idle or Alarm]");
        return false;
    }
    if (!sd_open(local_path)) {
        snprintf(s_status, sizeof(s_status), "cannot open %s", local_path);
        return false;
    }

    s_total = sd_size();
    snprintf(s_remote, sizeof(s_remote), "%s", remote_path);
    xmodem_tx_begin(tx, reader);
    send_linef("$XR=%s", s_remote);      // FluidNC opens the file and waits
    s_busy = true;
    s_ok   = false;
    s_start_ms = millis();
    snprintf(s_status, sizeof(s_status), "uploading %u bytes", (unsigned)s_total);
    return true;
}

void upload_poll() {
    if (!s_busy) return;

    xmodem_tx_tick(millis());
    if (!xmodem_tx_done()) return;

    s_busy = false;
    s_ok   = xmodem_tx_ok();
    sd_close();

    uint32_t ms = millis() - s_start_ms;
    snprintf(s_status, sizeof(s_status), "%s %u bytes in %lus",
             s_ok ? "sent" : "FAILED", (unsigned)xmodem_tx_bytes(),
             (unsigned long)(ms / 1000));
    char b[96];
    snprintf(b, sizeof(b), "[MSG:Upload %s -> %s]", s_ok ? "ok" : "failed", s_remote);
    clog_add(b);
}
