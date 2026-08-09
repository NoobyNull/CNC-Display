#include "ui/pages.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "model/FluidNCModel.h"
#include "GrblParserC.h"

// FluidNC macros are named in the controller's config; $RM=<name> runs one.
// Six fixed slots for now — reading the real names off the controller is listed
// as deferred work in the plan.
static const char* MACRO_NAME[6] = { "macro0", "macro1", "macro2", "macro3", "macro4", "macro5" };

static void macro_cb(lv_event_t* e) {
    const char* name = (const char*)lv_event_get_user_data(e);
    send_linef("$RM=%s", name);
}

void page_macro_build(lv_obj_t* p) {
    static lv_coord_t c[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
    static lv_coord_t r[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
    lv_obj_set_grid_dsc_array(p, c, r);
    lv_obj_set_layout(p, LV_LAYOUT_GRID);
    lv_obj_set_style_pad_column(p, U, 0);
    lv_obj_set_style_pad_row(p, U, 0);

    for (int i = 0; i < 6; i++) {
        lv_obj_t* b = w_button(p, MACRO_NAME[i], "$RM", C_SURFACE, C_TEXT,
                               macro_cb, (void*)MACRO_NAME[i]);
        lv_obj_set_grid_cell(b, LV_GRID_ALIGN_STRETCH, i % 3, 1, LV_GRID_ALIGN_STRETCH, i / 3, 1);
    }
}
