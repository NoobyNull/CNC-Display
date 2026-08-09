#pragma once
#include <stdint.h>
#include <stddef.h>

// Panel-local microSD on HSPI: CS=10, MOSI=11, CLK=12, MISO=13.
// This is the G-code source when the controller is out of reach.
#define SD_MAX_ENTRIES 64
#define SD_NAME_MAX    64

struct sd_entry_t {
    char     name[SD_NAME_MAX];
    uint32_t size;
    bool     is_dir;
};

bool sd_begin();
bool sd_present();
bool sd_list(const char* path);              // fills the entry table
size_t            sd_count();
const sd_entry_t* sd_at(size_t i);

// Streaming read for the XModem transmitter.
bool   sd_open(const char* path);
size_t sd_read(uint8_t* dst, size_t max);    // 0 = EOF
void   sd_close();
uint32_t sd_size();
