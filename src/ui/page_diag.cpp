#include "ui/pages.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "ui/view_expanded.h"
#include "model/FluidNCModel.h"
#include "GrblParserC.h"

struct diag_cmd_t { const char* label; const char* cmd; };

static const diag_cmd_t DIAG[6] = {
    { "SETTINGS", "$S" },
    { "CHANGED",  "$SC" },
    { "LIMITS",   "$Limits/Show" },
    { "CHANNELS", "$CI" },
    { "GPIO",     "$GD" },
    { "STATE",    "$T" },
};

static void diag_cb(lv_event_t* e) {
    const char* cmd = (const char*)lv_event_get_user_data(e);
    send_line(cmd);
    // The replies arrive as ordinary lines, so open the console to read them.
    expanded_open(EXPANDED_CONSOLE);
}

void page_diag_build(lv_obj_t* p) {
    static lv_coord_t c[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
    static lv_coord_t r[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
    lv_obj_set_grid_dsc_array(p, c, r);
    lv_obj_set_layout(p, LV_LAYOUT_GRID);
    lv_obj_set_style_pad_column(p, U, 0);
    lv_obj_set_style_pad_row(p, U, 0);

    for (int i = 0; i < 6; i++) {
        lv_obj_t* b = w_button(p, DIAG[i].label, DIAG[i].cmd, C_SURFACE, C_TEXT,
                               diag_cb, (void*)DIAG[i].cmd);
        lv_obj_set_grid_cell(b, LV_GRID_ALIGN_STRETCH, i % 3, 1, LV_GRID_ALIGN_STRETCH, i / 3, 1);
    }
}
