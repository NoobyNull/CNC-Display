#pragma once

// With the rack, FluidNC forgets the tool on every boot and reset, and a wrong
// answer sends a cartridge to an occupied pocket. So when it does not know, this
// card asks: empty, custom, or T1..T5. A touch on the toolsetter can settle tool or no
// tool first. Called from shell_tick().
void spindle_card_tick();
