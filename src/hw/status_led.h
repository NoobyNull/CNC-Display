#pragma once

// Eight WS2812 pixels driven as one Datron-style status bar. Data on
// STATUS_LED_PIN, default IO4 on the wireless-module socket. IO4-6 reach that
// socket only with the function DIP on "WM"; on "MIC & TF" they go to the SD
// slot instead (sdcard.cpp releases them when no card mounts). 15/16 on the
// socket are the touch I2C bus and 19/20 the S3's native USB. Driven by RMT, so no interrupt
// masking and no quarrel with the RGB panel's DMA. Pixels want 5V; one pixel
// at 60mA full white is 480mA for the bar, so power it from the panel's 5V
// supply, not a USB-fed rail.
void status_led_begin();
void status_led_tick();   // call from loop(); writes only when the colour changes
int  status_led_pin();
