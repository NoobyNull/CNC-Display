#pragma once
#include <stdint.h>
#include "model/FluidNCModel.h"

// Datron-style machine status colour: one colour for the whole light bar.
// Pure so the host test can pin the mapping; the pixels live in hw/status_led.
// Packed 0xRRGGBB.
static inline uint32_t status_color(state_t s) {
    switch (s) {
        case Idle:                                       return 0x0000FF;  // blue: ready
        case Cycle: case Jog: case Homing:               return 0x00FF00;  // green: moving
        case Hold: case DoorOpen: case DoorClosed:
        case CheckMode: case GrblSleep:                  return 0xFFB000;  // amber: paused / waiting
        case Alarm: case Critical: case ConfigAlarm:     return 0xFF0000;  // red: fault
        case Disconnected:
        default:                                         return 0x202020;  // dim white: no controller
    }
}
