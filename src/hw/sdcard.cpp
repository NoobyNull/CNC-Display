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
static sd_entry_t s_entries[SD_MAX_ENTRIES];
static size_t     s_count = 0;
static File       s_file;

bool sd_present() { return s_ok; }
size_t sd_count() { return s_count; }
const sd_entry_t* sd_at(size_t i) { return i < s_count ? &s_entries[i] : nullptr; }

bool sd_begin() {
    sd_spi.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    s_ok = SD.begin(SD_CS, sd_spi, SD_SPI_FREQ);
    Serial.printf("[sd] %s\n", s_ok ? "mounted"
                                    : "no card (or the function DIP is not on TF-CARD)");
    return s_ok;
}

bool sd_list(const char* path) {
    s_count = 0;
    if (!s_ok) return false;
    File dir = SD.open(path && *path ? path : "/");
    if (!dir || !dir.isDirectory()) return false;

    for (File f = dir.openNextFile(); f && s_count < SD_MAX_ENTRIES; f = dir.openNextFile()) {
        const char* n = f.name();
        const char* slash = strrchr(n, '/');
        if (slash) n = slash + 1;
        if (n[0] == '.') { f.close(); continue; }     // skip dotfiles and ._ resource forks

        sd_entry_t& e = s_entries[s_count];
        strncpy(e.name, n, SD_NAME_MAX - 1);
        e.name[SD_NAME_MAX - 1] = 0;
        e.is_dir = f.isDirectory();
        e.size   = e.is_dir ? 0 : (uint32_t)f.size();
        s_count++;
        f.close();
    }
    dir.close();
    return true;
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

uint32_t sd_size() { return s_file ? (uint32_t)s_file.size() : 0; }

void sd_close() {
    if (s_file) s_file.close();
}
