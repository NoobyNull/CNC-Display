#include "model/homed.h"

static bool     s_homed = false;
static uint32_t s_epoch = 0;

bool     homed()       { return s_homed; }
uint32_t homed_epoch() { return s_epoch; }

void homed_forget() {
    s_epoch++;
    s_homed = false;
}

void homed_note_state(bool was_homing, bool now_alarm, bool now_disconnected) {
    // A link that drops may be a controller power cycle; nothing says otherwise.
    if (now_disconnected) { homed_forget(); return; }
    if (was_homing && !now_alarm) s_homed = true;
}

void homed_note_alarm(int code) {
    switch (code) {
        case 1:                        // hard limit
        case 3:                        // reset while moving
        case 6: case 7: case 8: case 9:// homing failed
        case 13:                       // hard stop
        case 14:                       // unhomed
        case 18:                       // probe hard limit
            homed_forget();
            break;
        default:                       // soft limit, probe miss etc. keep position
            break;
    }
}
