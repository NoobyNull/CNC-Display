# Jog go-to: tap a coordinate, type a target

**Date:** 2026-08-12
**Status:** approved, not yet implemented

## Problem

The jog page can only move the machine by relative nudges — a tap is one step,
a hold is a continuous jog. There is no way to say "put X at 125.4". Reaching a
known coordinate today means counting steps or bumping and reading the DRO.

## What it does

On the JOG page, tapping an axis cell in the DRO opens a modal number pad. Type
a target, press GO, and the axis moves there in the active work coordinate
system.

## Decisions

Each of these was considered and settled during design; the rejected options are
recorded so they are not silently revisited.

| Decision | Chosen | Why |
|---|---|---|
| Motion command | `$J=G90F<feed><axis><target>` | Cancellable, and `$J` does not change modal state — a `G90 G0` go-to would leave G90/G0 set for whatever runs next. Also reuses the existing `send_jog_cancel()` path |
| Coordinate system | Work, active WCS | It is the big number on the DRO and the one you position by. `G53` machine go-tos are rarer and riskier |
| Where the tap works | `PAGE_JOG` only | The DRO is shared by every page (`shell.cpp:238`). The jog page is the motion page; elsewhere the DRO stays a read-only readout so a stray tap during a job does nothing |
| Number pad | Dedicated modal, not `w_keyboard()` | `w_keyboard()` is a singleton and two pages attaching their own done handler fight — `page_cal.cpp:12` avoided it for exactly this reason. A dedicated pad keeps the DIAG settings editor out of the blast radius |
| Typo / travel guard | **None** | The panel is a controller, not a safety device. Catching an out-of-range target is FluidNC's job, via `soft_limits` once homing is trusted. A delta readout and a `max_travel_mm` check were both considered and rejected — building a safety layer into the panel invites relying on it |
| Gating | Disconnected only | Reuses the jog-key guard, for its real reason: a send with no controller freezes the UI for up to 2s. **Not** gated on `Idle` — FluidNC answers `error:8` and that is correct |

### Non-goals

- No machine-coordinate (`G53`) go-to.
- No multi-axis go-to. One axis per entry.
- No feed-rate setting in SETUP. The feed is a constant; making it a setting is
  a later decision if it turns out to matter.

## Architecture

Three units, following the existing `calibration.cpp` / `page_cal.cpp` split:
pure logic in `model/`, LVGL view on top, host test on the logic.

### 1. `src/model/numentry.{h,cpp}` — pure entry buffer

No LVGL, no hardware, so the keystroke logic and the parse can be tested on the
host. Added to the `native` env's `build_src_filter`.

```c
#define NUMENTRY_MAX 16

typedef struct {
    char buf[NUMENTRY_MAX + 1];
    bool negative;
} numentry_t;

void        numentry_clear(numentry_t* e);
void        numentry_digit(numentry_t* e, char c);  // '0'-'9'; ignores overflow
void        numentry_dot(numentry_t* e);            // ignored if one is present
void        numentry_sign(numentry_t* e);           // toggles
void        numentry_back(numentry_t* e);           // one character
const char* numentry_text(const numentry_t* e);     // for display; "" when empty
bool        numentry_value(const numentry_t* e, float* out);  // false if unparseable
```

`numentry_value()` returning false is what makes GO a no-op — an entry of `""`,
`"-"` or `"."` is not a number.

### 2. `src/ui/widgets.cpp` — `w_numpad`

A lazily-created modal overlay on `lv_scr_act()`, hidden when idle, holding an
`lv_btnmatrix`:

```
7  8  9  <-
4  5  6  +/-
1  2  3  CLR
0  .  GO CANCEL
```

```c
typedef void (*w_numpad_cb_t)(bool ok, float value, void* user);

// title   - e.g. "GO TO X"
// current - ALREADY FORMATTED text, shown as context for what is being
//           edited. A string, not a number: positions are pos_t (e4_t fixed
//           point under -DE4_POS_T), and the caller formats them with the
//           model's own e4_to_cstr() rather than leaking that type into the
//           view. Entry starts EMPTY regardless -- prefilling invites
//           appending digits to a number you meant to replace.
void w_numpad_open(const char* title, const char* current,
                   w_numpad_cb_t cb, void* user);
```

The callback hands back a plain `float`, which the caller formats into G-code
with `%.4f` exactly as `jog_send()` already does.

The callback is a **parameter of open()**, rebound on every call and cleared on
close. That is what structurally avoids the `w_keyboard()` bug: there is no
handler for independent pages to attach and fight over.

Sizing comes off the existing `U` / `SCREEN_W` / `SCREEN_H` scale and the theme
colours, not literals — the project rule is no hardcoded values in visual code.

### 3. Wiring

**`src/ui/shell.cpp`** — axis cells get `LV_OBJ_FLAG_CLICKABLE` and a handler
carrying the axis index. It early-returns unless `shell_current_page() ==
PAGE_JOG`, then calls `page_jog_goto_open(axis)`. The shell formats no G-code.

**`src/ui/page_jog.cpp`** — owns both halves of the interaction:

```c
void page_jog_goto_open(int axis);              // opens the pad for this axis
static void goto_done(bool ok, float v, void*); // sends on confirm
```

All `$J` formatting stays in the one file that already owns it, so there is no
second copy of the jog wire format to keep in step with `jog_send()`.

```c
// $J needs an F. continuous_feed() is step-scaled and wrong here -- at the
// 0.01mm step it is 3 mm/min. Ask for more than any axis can do and let
// FluidNC clamp to each axis's max_rate_mm_per_min, so the machine config
// stays the single source of truth for speed.
static const float GOTO_FEED_MM_MIN = 10000.0f;
```

## Data flow

```
tap DRO axis cell
  -> shell.cpp: page == PAGE_JOG ?  no -> ignore
  -> page_jog_goto_open(axis)
       formats myAxes[axis] (work position, wco already subtracted)
       with e4_to_cstr(.., 3), the same way the DRO does
  -> w_numpad_open("GO TO X", "12.500", goto_done, axis)
  -> user types, presses GO
  -> goto_done(true, 125.4, axis)
  -> send_jog_line("$J=G90F10000X125.400")
  -> FluidNC moves, DRO tracks it
```

## Error handling

| Case | Behaviour |
|---|---|
| Link down | Pad opens; GO greyed with `LV_STATE_DISABLED`, same idiom as the jog keys. Prevents the 2s UI freeze, not the move |
| Not `Idle` | Sends anyway; FluidNC answers `error:8`, surfaced in the console log like any other error |
| Empty / malformed entry | `numentry_value()` returns false, GO does nothing |
| Target out of range | **Sent as typed.** FluidNC's business — see Decisions |
| Cancel, or tap outside | Pad closes, nothing sent |

## Testing

`test/test_numentry/main.cpp`, in the existing Unity suite, over the pure
module: digits accumulate, one dot only, sign toggles, backspace at empty is a
no-op, overflow is ignored, and `numentry_value()` rejects `""`, `"-"` and `"."`
while accepting `"125.4"` and `"-3"`.

The LVGL view and the `$J` string are verified on hardware against the
simulator, which now answers jogs (`$J=G90F1000X10` -> `ok`, MPos advances).

## Known interaction

GO rides `send_jog_line()`, which is ack-gated: one line means one 2s worst-case
UI block if the controller does not answer. That is the open bug recorded in
`docs/HANDOFF.md`, inherited rather than introduced — a single line cannot start
the self-sustaining loop the held keys could.
