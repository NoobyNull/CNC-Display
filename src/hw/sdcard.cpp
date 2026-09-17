#include "hw/sdcard.h"
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>

// The Advance 7.0's TF slot is SPI, on pins the function-select DIP switch
// shares with the speaker I2S and the wireless module SPI. THE DIP MUST BE ON
// "TF-CARD" (S1:S0 = 1:0) or these pins are not routed to the slot at all and
// the mount fails exactly like a missing card. The card's CS is strapped on
// the board, not wired to a GPIO — GPIO 0 is the dummy CS the SD library
// insists on, same as Elecrow's factory example.
#define SD_MISO 4
#define SD_SCK  5
#define SD_MOSI 6
#define SD_CS   0
// Factory value. These pins reach SPI through the GPIO matrix; if the card
// misbehaves, halving this is the first knob.
#define SD_SPI_FREQ 40000000

static SPIClass   sd_spi(FSPI);
static bool       s_ok = false;
static File       s_file;
static File       s_log;     // write handle; see the comment in sdcard.h

bool sd_present() { return s_ok; }

bool sd_begin() {
    sd_spi.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    s_ok = SD.begin(SD_CS, sd_spi, SD_SPI_FREQ);
    // No card usually means the DIP is on WM, routing these pins to the
    // wireless-module socket instead -- where the status bar's data line
    // lives on IO4. Let go of them so it can drive the pin.
    if (!s_ok) sd_spi.end();
    Serial.printf("[sd] %s\n", s_ok ? "mounted"
                                    : "no card (or the function DIP is not on TF-CARD)");
    return s_ok;
}

bool sd_open(const char* path) {
    if (!s_ok) return false;
    sd_close();
    s_file = SD.open(path, FILE_READ);
    return (bool)s_file;
}

size_t sd_read(uint8_t* dst, size_t max) {
    if (!s_file) return 0;
    int n = s_file.read(dst, max);
    return n > 0 ? (size_t)n : 0;
}

void sd_close() {
    if (s_file) s_file.close();
}

// ---- streaming write ---------------------------------------------------------

bool sd_mkdir(const char* path) {
    if (!s_ok) return false;
    if (SD.exists(path)) return true;
    return SD.mkdir(path);
}

bool sd_exists(const char* path) {
    return s_ok && SD.exists(path);
}

bool sd_log_open(const char* path) {
    if (!s_ok) return false;
    sd_log_close();
    s_log = SD.open(path, FILE_WRITE);
    return (bool)s_log;
}

size_t sd_log_write(const void* src, size_t n) {
    if (!s_log) return 0;
    return s_log.write((const uint8_t*)src, n);
}

// The FAT directory entry is only updated on flush or close, so a log that is
// never flushed reads as a zero-length file if the panel loses power mid-job —
// the data is on the card but nothing points at it. Cheap enough to do on a
// timer; expensive enough not to do per line.
void sd_log_flush() {
    if (s_log) s_log.flush();
}

void sd_log_close() {
    if (s_log) s_log.close();
}

bool sd_log_is_open() { return (bool)s_log; }
