#pragma once

// The link to the controller: is it up, has it restarted, and what to ask it
// when it comes up. One place decides all three.
//
//   NO LINK  --first status report-->  priming  -->  ready
//   ready --4 s of silence--> NO LINK         (queue flushed, homing forgotten)
//   ready --sign-on banner--> priming         (controller restarted; homing
//                                              forgotten unless we sent RESET)

// Once per loop(): liveness pings, loss, restarts, priming.
void link_service();
