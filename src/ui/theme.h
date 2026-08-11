#pragma once
#include <lvgl.h>
#include "hw/display_hw.h"

// Two constants are hand-set because physics sets them: the grid quantum, and
// the size of a fingertip. Everything else derives from them or uses
// LV_GRID_FR(), so changing U reflows the whole UI.
static const lv_coord_t U          = 8;        // grid quantum
static const lv_coord_t TOUCH_HOLD = U * 10;   // 80px: keys you hold contact on
static const lv_coord_t TOUCH_TAP  = U * 7;    // 56px floor: discrete taps

// The panel is the one input the layout cannot derive: it comes from the driver.
static const lv_coord_t SCREEN_W      = SCREEN_WIDTH;
static const lv_coord_t SCREEN_H      = SCREEN_HEIGHT;
static const lv_coord_t RAIL_W        = U * 12;   // 96
static const lv_coord_t RAIL_SCROLL_H = U * 5;    // 40
static const lv_coord_t DRO_H         = U * 18;   // 144
static const lv_coord_t SAFETY_H      = U * 10;   // 80
static const lv_coord_t CONTENT_W     = SCREEN_W - RAIL_W;              // 928
static const lv_coord_t CONTENT_H     = SCREEN_H - DRO_H - SAFETY_H;    // 376

// The panel is deliberately single-theme: an emissive display in a workshop is
// dark because glare and grime make it so, not because a viewer chose it.
#define C_BG      lv_color_hex(0x0D1114)
#define C_SURFACE lv_color_hex(0x161C20)
#define C_RAISE   lv_color_hex(0x1F272C)
#define C_LINE    lv_color_hex(0x2B353B)
#define C_TEXT    lv_color_hex(0xE8EFF2)
#define C_DIM     lv_color_hex(0x7E8F99)
#define C_ACCENT  lv_color_hex(0x3FC1CC)

// Semantic state colour, kept separate from the accent.
#define C_OK      lv_color_hex(0x46C25A)
#define C_RUN     lv_color_hex(0x4C93F5)
#define C_WARN    lv_color_hex(0xE0A22C)
#define C_CRIT    lv_color_hex(0xF4574C)
#define C_SEL_BG  lv_color_hex(0x12262A)
