#pragma once
#include <lvgl.h>

// Pages build once into a 688x240 container and are shown/hidden thereafter.
// Rebuilding an LVGL tree on every nav tap is allocation churn for no gain.
typedef void (*page_build_fn)(lv_obj_t* parent);

enum page_id_t {
    PAGE_JOG = 0, PAGE_HOME, PAGE_PROBE, PAGE_OFFSETS,
    PAGE_RUN, PAGE_MACRO, PAGE_CAL, PAGE_DIAG, PAGE_SETUP,
    PAGE_COUNT
};

void page_jog_build(lv_obj_t* p);
void page_home_build(lv_obj_t* p);
void page_probe_build(lv_obj_t* p);
void page_offsets_build(lv_obj_t* p);
void page_run_build(lv_obj_t* p);
void page_macro_build(lv_obj_t* p);
void page_cal_build(lv_obj_t* p);
void page_diag_build(lv_obj_t* p);
void page_setup_build(lv_obj_t* p);

// Called on the shared 20 Hz tick while the page is visible.
void page_jog_tick();
void page_offsets_tick();
void page_run_tick();
void page_setup_tick();
void page_diag_tick();
void page_cal_tick();
void page_macro_tick();

// Diag runs $Limits/Show only while it is on screen; the shell calls this on
// navigating away so the controller stops monitoring.
void page_diag_hidden();
// Consumes the monitor's stream (existence preamble + 500ms samples).
// Returns true when the line belonged to the monitor and not the console.
bool page_diag_limits_feed(const char* line);
