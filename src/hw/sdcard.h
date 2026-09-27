#pragma once
#include <stdint.h>
#include <stddef.h>

// Panel-local microSD on HSPI: CS=10, MOSI=11, CLK=12, MISO=13.
// Holds only /wifi.txt for the network firmware update (fw_update.cpp). G-code
// is never read from here: jobs run from the controller's own SD card.
bool   sd_begin();
bool   sd_present();
bool   sd_open(const char* path);
size_t sd_read(uint8_t* dst, size_t max);    // 0 = EOF
void   sd_close();
