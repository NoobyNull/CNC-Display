#pragma once

// Walk-through that opens by itself whenever machine position cannot be
// trusted (fresh link, controller restart, position-losing alarm): check the
// table, HOME ALL, then an optional TLO run. Called from shell_tick().
void home_wizard_tick();
