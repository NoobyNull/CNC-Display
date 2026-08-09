#pragma once
#include <stdint.h>
#include <stddef.h>

// Copy a file from the panel's SD card to the controller's SD card using
// "$XR=<dest>", then it can be run with "$SD/Run=<dest>" like any other file.
//
// This beats streaming G-code line by line: once the file is on the controller
// it runs from the controller's own storage, so the pendant can be unplugged
// mid-job without stopping the cut.
bool        upload_start(const char* local_path, const char* remote_path);
void        upload_poll();
bool        upload_busy();
bool        upload_ok();
int         upload_percent();
const char* upload_status();
