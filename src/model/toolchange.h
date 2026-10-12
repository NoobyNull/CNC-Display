#pragma once

// The job asked for a tool. The post writes "(MSG, Change to T2: <name>)" and
// FluidNC's atc_manual writes "(MSG: Install tool #2 ...)" before its M0; both
// reach the panel as [MSG:INFO: MSG, ...]. Remembered until the pause ends so
// the UI can say WHICH tool instead of just HOLD.

void        tc_note_message(const char* args);   // [MSG:...] arguments
int         tc_tool();       // 0 = no request pending
const char* tc_name();       // tool name from the post's message, "" if none
bool        tc_measured();   // atc_manual asked: resume probes it and sets TLO
void        tc_clear();
