#include "model/corner_gesture.h"

void cg_init(cg_state_t* g, uint16_t screen_w, uint16_t screen_h) {
    g->w = screen_w;
    g->h = screen_h;
    cg_reset(g);
}

void cg_reset(cg_state_t* g) {
    g->since_ms = 0;
    g->fired    = false;
}

bool cg_corners_held(const cg_state_t* g, const cg_point_t* pts, uint8_t n) {
    if (!pts || n < 4) return false;
    if (n > CG_MAX_POINTS) n = CG_MAX_POINTS;

    const uint16_t bw = g->w / CG_CORNER_DIV;
    const uint16_t bh = g->h / CG_CORNER_DIV;

    // One flag per corner: top-left, top-right, bottom-left, bottom-right.
    bool seen[4] = { false, false, false, false };

    for (uint8_t i = 0; i < n; i++) {
        const bool left = pts[i].x < bw;
        const bool right = pts[i].x >= (uint16_t)(g->w - bw);
        const bool top = pts[i].y < bh;
        const bool bottom = pts[i].y >= (uint16_t)(g->h - bh);

        if (top && left)          seen[0] = true;
        else if (top && right)    seen[1] = true;
        else if (bottom && left)  seen[2] = true;
        else if (bottom && right) seen[3] = true;
    }
    return seen[0] && seen[1] && seen[2] && seen[3];
}

bool cg_update(cg_state_t* g, const cg_point_t* pts, uint8_t n, uint32_t now_ms) {
    if (!cg_corners_held(g, pts, n)) {
        // Any finger leaving its corner both cancels an in-progress hold and
        // re-arms the latch, so the next gesture has to be deliberate.
        cg_reset(g);
        return false;
    }
    if (g->fired) return false;

    if (g->since_ms == 0) {
        // now_ms == 0 would be indistinguishable from "not held", and millis()
        // really is 0 for the first millisecond after boot.
        g->since_ms = now_ms ? now_ms : 1;
        return false;
    }
    if ((uint32_t)(now_ms - g->since_ms) < CG_HOLD_MS) return false;

    g->fired = true;
    return true;
}
