#pragma once
#include <lvgl.h>
#include "ui/pages.h"

void      shell_build();
void      shell_show_page(page_id_t id);
page_id_t shell_current_page();

// Full-screen expanded view: hides the rail and DRO, keeps the safety bar.
void shell_set_expanded(bool on);
bool shell_expanded();

// Called at 20 Hz from the LVGL timer.
void shell_tick();
