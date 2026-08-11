#include "hw/sdcard.h"
#include "hw/io_expander.h"
#include <Arduino.h>
#include <SD_MMC.h>

// The 7B wires the TF slot to the SDMMC peripheral, not to a general SPI bus,
// and there is no CS line on a GPIO at all — DAT3 hangs off the io-extender.
// 1-bit mode is what Waveshare ships and it is all three data lines the
// connector carries. Held high by ioexp_init(); asserting it would drop the
// card into SPI mode, which these pins cannot drive.
#define SD_CMD  11
#define SD_CLK  12
#define SD_D0   13

static bool       s_ok = false;
static sd_entry_t s_entries[SD_MAX_ENTRIES];
static size_t     s_count = 0;
static File       s_file;

bool sd_present() { return s_ok; }
size_t sd_count() { return s_count; }
const sd_entry_t* sd_at(size_t i) { return i < s_count ? &s_entries[i] : nullptr; }

bool sd_begin() {
    ioexp_write(EXIO_SD_CS, true);          // DAT3 high = stay in SD mode
    SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0);
    s_ok = SD_MMC.begin("/sdcard", /*mode1bit=*/true);
    Serial.printf("[sd] %s\n", s_ok ? "mounted" : "no card");
    return s_ok;
}

bool sd_list(const char* path) {
    s_count = 0;
    if (!s_ok) return false;
    File dir = SD_MMC.open(path && *path ? path : "/");
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
    s_file = SD_MMC.open(path, FILE_READ);
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
