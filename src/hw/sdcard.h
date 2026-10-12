#pragma once
#include <stdint.h>
#include <stddef.h>

// Panel-local microSD on HSPI: CS=10, MOSI=11, CLK=12, MISO=13.
// Holds /wifi.txt and the staged download (/fw) for the network firmware
// update (fw_update.cpp), and the IMU logs. G-code
// is never read from here: jobs run from the controller's own SD card.
bool   sd_begin();
bool   sd_present();
bool   sd_open(const char* path);
size_t sd_read(uint8_t* dst, size_t max);    // 0 = EOF
void   sd_close();

// Streaming WRITE, on its own File handle. Deliberately not the one above: the
// XModem transmitter reads a G-code file for as long as an upload lasts, and
// the IMU recorder writes for as long as a job lasts, so the two overlap. One
// shared handle would have whichever started second silently close the first.
bool   sd_log_open(const char* path);        // create, truncating any existing
size_t sd_log_write(const void* src, size_t n);
void   sd_log_flush();                       // commit to the card, keep it open
void   sd_log_close();
bool   sd_log_is_open();

bool sd_mkdir(const char* path);             // true if it exists afterwards
bool sd_exists(const char* path);
