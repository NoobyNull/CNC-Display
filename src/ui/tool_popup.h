#pragma once

// Opens while a job is paused for a tool change and says which tool. Called
// from shell_tick().
void tool_popup_tick();
