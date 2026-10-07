#pragma once

// The model writes globals then raises a flag. The UI polls the flags on an
// lv_timer. No observer registry: this UI is a fixed screen set, not a plugin
// host, and FluidDial's current_scene->onX() calls become these.

void model_flag_dro();
void model_flag_state();
void model_flag_limits();
void model_flag_error(const char* msg);
void model_flag_message(const char* cmd, const char* args);

// Sticky: true once the controller has sent a single position report, i.e.
// once n_axes and the limit/probe picture describe a real machine rather than
// the compiled defaults. The boot gate waits on this.
bool        model_dro_ever();

bool        model_take_dro();
bool        model_take_state();
bool        model_take_limits();
const char* model_take_error();   // nullptr if none pending
bool        model_take_update_request();   // [MSG:PANEL:UPDATE] arrived

// The pendant passthrough board as FluidNC named it at boot ("Airedale v1.1"),
// or nullptr if no banner has been seen. Latched, because the console ring
// drops the banner long before anyone opens the HW page.
const char* model_expander_board();

// GrblParserC hands unrecognised lines to handle_other(); this fans them out to
// the offset table, the file list and the console.
extern "C" void handle_other_line(char* line);
