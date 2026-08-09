# CNC Console — CrowPanel Advance 7.0" pendant for FluidNC

**Date:** 2026-08-08
**Status:** Design approved, ready for planning
**Layout study:** https://claude.ai/code/artifact/29f1b135-2fd7-40dc-b26b-bc20692e0f22

---

## 1. Goal

Build a full CNC control console on an Elecrow CrowPanel Advance 7.0" (800×480, ESP32-S3),
talking to FluidNC over a wired UART at 1 Mbaud.

Reuse FluidDial's communications and status layer. Discard its UI entirely — it is built for a
240×240 dial with a rotary encoder and none of its layout logic survives contact with an
800×480 capacitive touch panel.

The console replaces the pendant as the primary interface. It is not a companion display.

### Scope

Eight destinations: Jog, Home, Probe, Offsets, Run, Macro, Diag, Setup. Plus an expanded
full-screen view with two faces — toolpath and live console.

---

## 2. Verified hardware facts

All of the following were read off the running board or its source, not assumed.

### Board

| Property | Value | Source |
|---|---|---|
| MCU | ESP32-S3 | boot ROM banner |
| PSRAM | 8,388,608 bytes (8,384,788 free at boot) | firmware boot log |
| Internal heap | 332,920 bytes (294,304 free) | firmware boot log |
| Panel | 800×480 RGB565 parallel | `display_hw.cpp` |
| Touch | GT911 @ I2C `0x5D` | boot log, `touch.cpp` |
| IO expander | PCA9557 @ I2C `0x18` | boot log |
| Flash config | 4 MB, custom partition table | `platformio.ini` |
| App partition | 0x300000 (3,072 KB) | `partitions_4MB_app.csv` |
| Current firmware | 1,648,832 bytes — **52.4% used, 1,462 KB free** | `.pio/build/display/firmware.bin` |

Flash headroom is not a constraint for this project.

### GPIO map

Every pin consumer, established from the existing `Elcrow-Display-hotkeys` project:

| Consumer | Pins |
|---|---|
| RGB panel data d0–d15 | 15, 7, 6, 5, 4, 9, 46, 3, 8, 16, 1, 14, 21, 47, 48, 45 |
| RGB sync (DE/VSYNC/HSYNC/PCLK) | 41, 40, 39, 0 |
| Backlight (PWM) | 2 |
| Power rail (drive LOW) / PCF8575 INT | 38 |
| I2C — GT911, PCA9557, PCF8575 | 19 (SDA), 20 (SCL) |
| SD card (HSPI) | 10 (CS), 11 (MOSI), 12 (CLK), 13 (MISO) |
| UART0 console (CH340) | 43 (TX), 44 (RX) |
| Octal PSRAM + QIO flash | 26–37 |

**Free: GPIO 17, 18, and 42.**

GPIO 19/20 are the ESP32-S3's native USB D−/D+ pins, used here as I2C. Native USB is therefore
unavailable; the CH340 is the only serial path.

### Panel initialisation

`Elcrow-Display-hotkeys/display/display_hw.cpp` is a complete, working bring-up. Copy it
verbatim. It contains:

- LovyanGFX `Bus_RGB` + `Panel_RGB` config with all 16 data pins and sync timings
  (`freq_write` 12 MHz, hsync 40/48/40, vsync 1/31/13, `pclk_active_neg = 1`)
- `Light_PWM` backlight on GPIO2
- GPIO38 driven LOW for the power rail
- PCA9557 reset sequence: IO0 = GT911 reset (low 20 ms, high 100 ms), IO1 = GT911 INT as input
- LVGL double draw buffers, 800×40 each, allocated with `ps_malloc`

---

## 3. Known defect in the source project

`Elcrow-Display-hotkeys/display/uart_input.cpp:26`:

```c
Serial1.begin(115200, SERIAL_8N1, 44, 43);   // RX=44, TX=43
```

GPIO 43/44 are the ESP32-S3's UART0 console pins. The button-controller link is claiming the
pins the CH340 uses. Observed consequence: `esptool` reports *"Download mode successfully
detected, but getting no sync reply: The serial TX path seems to be down"* — host→device bytes
never reach the ROM loader.

Flashing still works by holding BOOT while tapping RESET. This project does not inherit the
defect (it uses UART2 on 17/18), but the hotkey project should be fixed separately.

---

## 4. Architecture

### 4.1 Project structure

New repository at `/home/matthew/Projects/CNC-Disply`. Single PlatformIO environment.

```
src/
  main.cpp              setup(): panel + LVGL + UART. loop(): drain, lv_timer_handler()
  hw/
    display_hw.cpp/.h   copied verbatim from Elcrow-Display-hotkeys
    touch.cpp/.h        copied verbatim
    lv_conf.h           copied
  transport/
    uart.cpp            fnc_putchar / fnc_getchar on UART2 @ 1 Mbaud
    xmodem_rx.cpp       XModem-1K receiver for $XS bulk file pull
  model/
    FluidNCModel.cpp/.h ported from FluidDial
    FileParser.cpp/.h   ported from FluidDial
    offsets.cpp/.h      new — parses $# output
    gcode_path.cpp/.h   new — G-code → point buffer (host-testable)
  ui/
    theme.h             base unit, touch minimums, colour tokens
    shell.c             persistent grid: rail / DRO / content / safety
    page_jog.c  page_home.c  page_probe.c  page_offsets.c
    page_run.c  page_macro.c page_diag.c   page_setup.c
    view_expanded.c     full-screen toolpath ⇄ console
lib/
  PCA9557/              copied from Elcrow-Display-hotkeys
test/
  test_gcode_path/      host-native unit test for the path parser
```

**Why a new repo rather than a fourth env inside `Elcrow-Display-hotkeys`.** That repo already
uses the multi-firmware `build_src_filter` pattern, so adding an env would avoid copying ~400
lines of bring-up. Rejected: the bring-up is finished, stable code that will not drift, so
duplicating it costs close to nothing, whereas permanently welding a CNC pendant into a
hotkey-deck repo costs attention every time either one is touched.

### 4.2 Dependencies

```ini
lib_deps =
    lovyan03/LovyanGFX@^1.1.8
    https://github.com/lvgl/lvgl.git#v8.3.11
    https://github.com/MitchBradley/json-streaming-parser#charp-1.0.2
    https://github.com/MitchBradley/GrblParser#9108f54a3e30526b66194898676af34e96a87da9
```

The GrblParser and json-streaming-parser commits are pinned to exactly what FluidDial uses.

**LVGL stays at 8.3.11.** The copied bring-up is LVGL 8 API (`lv_disp_drv_t`,
`lv_disp_draw_buf_init`, `lv_disp_drv_register`). Upgrading to LVGL 9 means rewriting the one
piece of code we least want to touch, for no benefit this project needs.

### 4.3 Layering

```
GrblParserC (lib)  ──calls──>  show_dro/show_state/show_overrides/... (FluidNCModel)
                                        │ writes globals, sets dirty flag
                                        ▼
                               lv_timer @ 20 Hz reads globals, repaints
                                        ▲
UART2 @ 1 Mbaud  ──fnc_getchar──────────┘
```

FluidDial's model couples to its UI in exactly eight places, all of the form
`current_scene->onDROChange()`. Each is replaced with a dirty flag.

```c
// model — the only change to FluidDial's show_dro()
extern "C" void show_dro(const pos_t* axes, const pos_t* wco,
                         bool isMpos, bool* limits, size_t n_axis) {
    /* ... existing FluidDial body populates myAxes[] ... */
    dro_dirty = true;               // was: current_scene->onDROChange()
}

// ui
static void dro_timer_cb(lv_timer_t* t) {
    if (!dro_dirty) return;
    dro_dirty = false;
    for (int a = 0; a < n_axes; a++) {
        lv_label_set_text(dro_work[a], fmt_pos(myAxes[a] - wco[a]));
        lv_label_set_text(dro_mach[a], fmt_pos(myAxes[a]));
    }
    toolpath_set_marker(myAxes[0], myAxes[1]);
}
lv_timer_create(dro_timer_cb, 50, NULL);
```

No observer registry, no callback vector. The UI is a fixed screen set, not a plugin system.

`FileParser.cpp` needs the same treatment: strip `#include "Scene.h"` and `#include
"HomingScene.h"`, replace its `current_scene->onFilesList()` / `onFileLines()` /`onError()`
calls with flags plus a small result buffer.

### 4.4 Concurrency

**Single task. No RTOS plumbing.**

```c
void loop() {
    for (int i = 0; i < 64; i++) {          // drain a chunk, not one byte
        fnc_poll();
        if (!fnc_rx_waiting()) break;
    }
    lv_timer_handler();
}
```

LVGL is not thread-safe; a second task means a mutex around every `lv_*` call. At 1 Mbaud with a
64-byte drain per tick there is no throughput reason to split them. The 64-byte drain is lifted
from FluidDial's `ardmain.cpp`, where it fixed real ring-buffer overflow.

---

## 5. Transport

UART2, TX = **GPIO 17**, RX = **GPIO 18**, 1,000,000 baud, 8N1.

Lift `init_fnc_uart()` from `FluidDial-main/src/SystemArduino.cpp:133` unchanged. It already
selects `UART_SCLK_XTAL` on ESP32-S3 (`SystemArduino.cpp:142`), which is what keeps 1 Mbaud
stable when the APB clock scales. `FNC_BAUD` already defaults to 1000000
(`SystemArduino.cpp:128`).

Software flow control (XON/XOFF, `0x11`/`0x13`) is part of FluidDial's model layer and carries
over unchanged.

**Only two GPIOs remained free and this consumes both.** GPIO 42 is the sole spare. Any future
hardware addition must go on I2C behind the existing expander, not on a new pin.

---

## 6. Command surface

`Protocol.cpp:292` calls `execute_line()` for every channel; `ProcessSettings.cpp:1112` routes
anything beginning `$` or `[` into the same registry that backs the WebUI. **There is no HTTP
gate.** The wired UART has the full command surface the browser has.

| Screen | Command | Purpose |
|---|---|---|
| Jog | `$J=` / `0x85` | Jog; cancel on release |
| Jog | `$MD` / `$ME` | Steppers off / on |
| Jog | `G10 L20 P0` | Zero an axis in the active system |
| Home | `$H`, `$HX`…`$HC` | Home all, or one of six axes |
| Probe | `G38.2`, `$X` | Probe, then clear the alarm lock |
| Offsets | `$#` | Read G54–G59, G28/G30, G92, TLO |
| Offsets | `G10 L20 Pn`, `G92.1` | Set / clear offsets |
| Run | `$Files/ListGCode` | Directory listing, G-code only |
| Run | `$XS=<path>` | XModem-1K file pull for the toolpath |
| Run | `$SD/Run=<path>` | Start the job |
| Run | `$C` | Check mode — dry run, no motion |
| Macro | `$RM=<name>` | Run a named macro |
| Diag | `$S`, `$SC`, `$Limits/Show`, `$CI`, `$GD`, `$T` | Settings, limits, channels, GPIO, state |
| Any | `$A`, `$E` | Alarm and error text, read from the firmware |
| Setup | `$Bye` | Restart the controller |
| Console | any line | MDI — sent unvalidated |

Sequences reused verbatim rather than re-derived:

- Probe: `FluidDial-main/src/ProbingScene.cpp:29` — `G38.2G91F%d%c%dP%s` (rate, axis char,
  travel, offset), plus the retract `$J=G91F1000%c%d` (`:51`) and the `$X` unlock (`:39`)
- Run file: `FluidDial-main/src/FilePreviewScene.cpp:78` — `$SD/Run=%s`
- Home axis: `FluidDial-main/src/HomingScene.cpp:91` — `$H%c`

### Alarm and error text

Read from `$A` and `$E` at startup and cache. Do not hardcode a table — a local copy drifts out
of sync with the controller's firmware and then lies to the operator.

---

## 7. UI

### 7.1 Tokens

Two constants are hand-set because physics sets them. Everything else derives or uses
`LV_GRID_FR()`.

```c
static const lv_coord_t U          = 8;       // grid quantum
static const lv_coord_t TOUCH_HOLD = U * 10;  // 80px — keys you hold contact on
static const lv_coord_t TOUCH_TAP  = U * 7;   // 56px floor — discrete taps
```

Colours (panel is deliberately single-theme — an emissive display in a workshop):

```
bg #0D1114   surface #161C20   raise #1F272C   line #2B353B
text #E8EFF2   dim #7E8F99   accent #3FC1CC
ok #46C25A   run #4C93F5   warn #E0A22C   critical #F4574C
```

Semantic state colour is separate from the accent. Device font is LVGL's bundled Montserrat.

### 7.2 Shell

```
┌──────┬────────────────────────────────────────────┐
│ ««   │  DRO BAR   704 × 144   state, WCS, X/Y/Z   │
│ RAIL ├────────────────────────────────────────────┤
│  96  │  CONTENT   704 × 256   ← only this swaps   │
│  ×   ├────────────────────────────────────────────┤
│ 480  │  SAFETY    704 × 80    HOLD RESUME RESET   │
│ »»   │                                            │
└──────┴────────────────────────────────────────────┘
```

```c
static lv_coord_t cols[] = { U*12, LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
static lv_coord_t rows[] = { U*18, LV_GRID_FR(1), U*10, LV_GRID_TEMPLATE_LAST };
lv_obj_set_grid_dsc_array(scr, cols, rows);
lv_obj_set_layout(scr, LV_LAYOUT_GRID);
lv_obj_set_grid_cell(rail,   LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 3);
lv_obj_set_grid_cell(dro,    LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
lv_obj_set_grid_cell(page,   LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
lv_obj_set_grid_cell(safety, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 2, 1);
```

The DRO is on top of every page. Hold/resume/reset are on the bottom of every page. Only the
middle 704×256 changes.

### 7.3 Nav rail

Eight destinations, five slots: `5U + 5×10U + 5U = 60U`, exactly the panel height, every target
at the full 10U.

```
 40  ««      greyed at top of list
 80  JOG
 80  HOME
 80  PROBE     ← default window: everything needed during a job
 80  OFFSET
 80  RUN
 40  »»      greyed at bottom
```

`««` / `»»` move the window one item and grey out at each end. Macro, Diag and Setup are
commissioning and configuration — one scroll away, never in the path of running a job.

### 7.4 Geometry

| Region | Size | Base units |
|---|---|---|
| Nav rail | 96 × 480 | 12U × 60U |
| Nav item | 96 × 80 | 12U × 10U |
| Nav scroll | 96 × 40 | 12U × 5U |
| DRO bar | 704 × 144 | 88U × 18U |
| Axis cell | 192 × 144 | 24U × 18U |
| Content | 704 × 256 | 88U × 32U |
| Safety bar | 704 × 80 | 88U × 10U |
| Expanded canvas | 800 × 400 | 100U × 50U |
| Jog key | 80 × 80 | 10U × 10U |

### 7.5 DRO bar

State chip (colour **and** word), active WCS chip, link indicator, then three axis cells:
work coordinate at 40 px, machine coordinate at 12 px beneath.

The WCS chip appears on every page so the operator cannot zero the wrong coordinate system by
forgetting which one is live.

**Four or more axes:** the DRO is a three-column grid; FluidNC reports up to six. At four, cells
narrow to 176 px and the 40 px numerals still fit. Past four, either the work coordinate drops a
size or A/B/C move to a second row and machine coordinates are dropped. Deferred until a fourth
axis exists.

### 7.6 Pages

**Jog.** 3×3 XY pad (80 px keys, centre shows step size), Z column (88 × 119 keys), step
selector 0.01/0.1/1/10 mm, and Zero X / Zero Y / Zero Z / `$MD` steppers-off.

`$MD` takes the fourth slot rather than Zero-All: pushing the gantry by hand is reached far more
often. Tinted amber — it drops holding torque, and an unbalanced Z head can fall.

**Home.** Home-all plus per-axis, with homed/unhomed state per axis.

**Probe.** Four parameter rows with ± steppers (tool offset, max travel, feed rate, retract),
live probe input state, and a start button that displays the G-code it will send.

**Offsets.** Table of G54–G59 / G28 / G92 / TLO × X/Y/Z from `$#`, active row highlighted.
Actions: Set from here (`G10 L20 Pn`), Go to origin, Clear G92 (`G92.1`), Activate.

Every action on this screen displays the G-code beneath its label. It writes persistent machine
state; if it does something unintended, the operator must be able to see what it did.

**Run.** File list (296) | toolpath preview (296) | actions (80). Actions are RUN, DRY (`$C`),
RELOAD. Once a job starts, the layout becomes toolpath (688×184) over a progress row (688×48)
with elapsed and estimated remaining.

Remaining time is derived from percent-complete against elapsed, which assumes a uniform feed
the file does not have. Display it with a tilde. Do not present a linear extrapolation as a
countdown.

**Macro / Diag / Setup.** Macro list via `$RM`. Diag surfaces `$S`, `$SC`, `$Limits/Show`,
`$CI`, `$GD`, `$T`. Setup covers connection, brightness, units, decimal places, `$Bye`.

### 7.7 Expanded view

Tapping the toolpath (⤢ affordance in its corner) hides the rail and DRO bar. Canvas becomes
800 × 400. `FLIP` and `CLOSE` occupy the safety bar's right-hand cells, so no chrome floats over
the canvas.

**The safety bar is never covered.** 90% of 480 px would be 432, leaving 48 px for
hold/resume/reset. Those keep their full 80 px; the canvas takes everything else. Actual
coverage is 83% of panel area.

The DRO moves rather than disappearing — X/Y/Z ride in a corner overlay driven by the same
`dro_timer_cb` tick.

**Face A — toolpath.** Cut path lit, remainder dimmed, marker at current position. Overlays:
filename and line counter (top left), X/Y/Z (top right), extents and active WCS (bottom right).

**Face B — console.** 28 px header, 13 log lines at 24 px, 48 px MDI row. Colour separates sent
lines, `ok`, `error:N`, `ALARM:`, `[MSG:]` and `[GC:]`.

Status frames (`<Run|MPos:…>`) are **filtered out by default**, with a toggle to show them.
FluidNC emits these several times a second; unfiltered they turn a 13-line window into a blur
that scrolls real errors off the top within seconds. The toggle exists because when debugging
the link itself they are the only thing worth seeing.

The MDI row sends typed input to the controller unvalidated — that is its purpose. Entry via
LVGL's on-screen keyboard, with a numeric keypad button for coordinates.

---

## 8. Toolpath pipeline

1. File selected → `$XS=<path>` → XModem-1K receive into PSRAM.
   `xmodem.cpp:91` defines `TRANSMIT_XMODEM_1K`, so blocks are 1024 bytes with CRC16, not 128.
   Estimated 10–15 s for 1 MB at 1 Mbaud. **Unmeasured — see risks.**
2. Parse G0/G1/G2/G3 with modal state (G90/G91, G20/G21, plane select) into a point buffer.
   ~40k moves × 12 bytes ≈ 500 KB, comfortable in 8 MB PSRAM.
3. Compute extents, fit transform to canvas.
4. Render to LVGL canvas. Marker position from `myAxes[]` on the existing DRO tick.

**Both file-read commands require Idle or Alarm.** `$XS` is registered with the `notIdleOrAlarm`
predicate (`FileCommands.cpp:676`) and `$File/SendJSON` checks it inline
(`FileCommands.cpp:195`). A file cannot be pulled mid-job.

This forces the correct design anyway: load the toolpath at selection time, then drive the
marker from the DRO stream already arriving. No bulk transfer competes with jog and status
traffic during a cut.

---

## 9. Safety requirements

These are not subject to simplification.

1. **Hold / Resume / Reset are visible on every screen**, including the expanded view, at a
   constant position and a minimum of 80 px.
2. **Jog cancels on release and on press-lost.**

   ```c
   lv_obj_add_event_cb(key, jog_start, LV_EVENT_PRESSED,    (void*)dir);
   lv_obj_add_event_cb(key, jog_stop,  LV_EVENT_RELEASED,   (void*)dir);
   lv_obj_add_event_cb(key, jog_stop,  LV_EVENT_PRESS_LOST, (void*)dir);
   ```

   `LV_EVENT_PRESS_LOST` is the finger sliding off the key. Without it the axis keeps moving
   after the thumb wanders onto the bezel.
3. **State is encoded in colour and in text**, never colour alone.
4. **RESET is not an E-stop.** It sends a soft reset over the wire and depends on the link being
   alive and the controller responding. The UI must not imply otherwise. A hardware E-stop that
   cuts power to spindle and drives remains mandatory.
5. **Link loss is shown, not hidden.** `fnc_is_connected()` going false drives a `NO LINK` state
   chip and disables motion controls.
6. **`$MD` is visually distinguished** as a state-changing action, not a neutral one.

---

## 10. Testing

`gcode_path.cpp` — the G-code → point buffer parser — is pure logic with no hardware
dependency. It gets a host-native PlatformIO test (`pio test -e native`) covering: G90 vs G91,
G20 vs G21, arc interpolation, modal carry-over between lines, and malformed input. This is the
one piece complex enough to fail silently and cheap enough to test properly.

Everything else is verified on hardware, stage by stage, against the exit criteria below.

---

## 11. Staging

| Stage | Deliverable | Exit criterion |
|---|---|---|
| 0 | Project skeleton, copied bring-up, blank LVGL screen | Touch-responsive screen at stable frame rate |
| 1 | UART2 + GrblParserC + model + shell + DRO | Jog from another sender; numbers track |
| 2 | Jog, Home, Probe, Offsets | Machine controllable from the panel alone |
| 3 | Run — file list, start, progress | A job runs to completion, watched from the panel |
| 4 | XModem, path parser, toolpath, console, expanded view | Toolpath of a real file with live marker |
| 5 | Macro, Diag, Setup | All eight destinations live |

---

## 12. Risks

| Risk | Detail | Mitigation |
|---|---|---|
| Full-canvas redraw cost | LVGL flush is 800×40 double-buffered via `lcd.writePixels`. An 800×400 toolpath redraw is 10 flush cycles. Frame rate unmeasured. | Measure in Stage 4. Redraw path only on marker crossing a threshold, not every DRO tick. |
| XModem throughput | 1 KB blocks with per-block ACK; turnaround latency unmeasured at 1 Mbaud. | Measure early in Stage 4 with a real file. Fall back to `$File/ShowSome` line ranges if unusable. |
| Path parse time | Unmeasured on 18k-line files. | Host test gives a baseline; measure on device. Downsample or background-chunk if needed. |
| GPIO exhaustion | 17 and 18 are the last free pins and this consumes both. GPIO 42 is the only spare. | Any future peripheral goes on I2C behind the existing expander. |
| Flashing | `esptool` TX path fails while the hotkey firmware runs (see §3). | Hold BOOT, tap RESET. Not inherited by this project. |
| 4+ axis DRO | Layout is a three-column grid. | Deferred until a fourth axis exists (§7.5). |

---

## 13. Open questions

1. Nav rail ordering — currently Jog, Home, Probe, Offset, Run, Macro, Diag, Setup. Cheap to
   change now, annoying later.
2. MDI input row — added under "maximum usability". Confirm it stays.
3. Whether the pendant continues to run. One FluidNC UART, one device: if the pendant is
   currently on that port, the console replaces it. Running both means moving the pendant to
   WiFi or ESP-NOW.
