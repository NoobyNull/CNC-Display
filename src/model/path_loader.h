#pragma once
#include <stddef.h>

// Pull <path> from the controller with $XS and scan it into gcode_stats.
// Only legal when the machine is Idle or Alarm: FluidNC gates $XS on
// notIdleOrAlarm (FileCommands.cpp:676), so a file cannot be pulled mid-job.
// That matches the design: the file is scanned when it is selected, before RUN.
bool   pathload_start(const char* path);
void   pathload_poll();
bool   pathload_busy();
bool   pathload_ok();
size_t pathload_bytes();
const char* pathload_path();   // the file last scanned
