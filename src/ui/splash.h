#pragma once

// Animated boot splash. splash_show() builds it (dark plate, gantry parked)
// so it can cover the rest of boot; splash_begin() starts the timeline and is
// called when boot is done — the anim clock is wall-time, so starting it
// before loop() pumps frames spends the whole sweep invisibly. Deletes itself
// when the animation ends (or on tap).
void splash_show();
void splash_begin();
