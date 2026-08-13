#pragma once
#include <stdint.h>

// Four fingers, one in each corner, held. A deliberate escape hatch to the
// console log: the one time you want the log is when the UI is misbehaving, so
// the way in must not depend on hitting a particular button on a particular
// page.
//
// Four corners rather than a simpler gesture because nothing else does it by
// accident. A palm, a sleeve or a wipe-down puts fingers close together or in a
// line; it does not put exactly one contact in each of four opposite corners
// and hold them there.
//
// Free of LVGL and the GT911 so the geometry and the hold timing can be tested
// on the host, which is the only practical way to test a gesture that needs
// four hands' worth of contacts.

#define CG_MAX_POINTS 5    // GT911 reports up to five

// Hold time before it fires. Long enough that four fingers landing during a
// clean-down do not trigger it, short enough not to feel broken.
#define CG_HOLD_MS 2000

// Corner box, as a fraction of each screen dimension: 1/5 is 20%.
#define CG_CORNER_DIV 5

typedef struct {
    uint16_t x, y;
} cg_point_t;

typedef struct {
    uint16_t w, h;
    uint32_t since_ms;   // when all four corners were first held; 0 = not held
    bool     fired;      // latched until the fingers lift, so it fires once
} cg_state_t;

void cg_init(cg_state_t* g, uint16_t screen_w, uint16_t screen_h);
void cg_reset(cg_state_t* g);

// Feed every contact currently on the glass. Returns true EXACTLY ONCE per
// gesture, on the update where the hold completes; the fingers must lift and
// land again before it can fire a second time.
bool cg_update(cg_state_t* g, const cg_point_t* pts, uint8_t n, uint32_t now_ms);

// True when all four corner boxes are occupied right now. Exposed for tests
// and for anything that wants to show the gesture is being recognised.
bool cg_corners_held(const cg_state_t* g, const cg_point_t* pts, uint8_t n);
