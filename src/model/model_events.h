#pragma once

// The model writes globals then raises a flag. The UI polls the flags on an
// lv_timer. No observer registry: this UI is a fixed screen set, not a plugin
// host, and FluidDial's current_scene->onX() calls become these.

void model_flag_dro();
void model_flag_state();
void model_flag_limits();
void model_flag_error(const char* msg);
void model_flag_message(const char* cmd, const char* args);

bool        model_take_dro();
bool        model_take_state();
bool        model_take_limits();
const char* model_take_error();   // nullptr if none pending

// GrblParserC hands unrecognised lines to handle_other(); this fans them out to
// the offset table, the file list and the console.
extern "C" void handle_other_line(char* line);
