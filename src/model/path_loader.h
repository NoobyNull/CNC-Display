#pragma once
#include <stddef.h>

// Pull <path> from the controller with $XS and parse it into the point buffer.
// Only legal when the machine is Idle or Alarm: FluidNC gates $XS on
// notIdleOrAlarm (FileCommands.cpp:676), so a file cannot be pulled mid-job.
// That restriction and the right design agree — the toolpath loads at selection
// time, then the marker rides the DRO stream during the cut.
bool   pathload_start(const char* path);
void   pathload_poll();
bool   pathload_busy();
bool   pathload_ok();
size_t pathload_bytes();
