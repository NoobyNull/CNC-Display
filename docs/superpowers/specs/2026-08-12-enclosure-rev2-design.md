# Enclosure rev 2 — full back, battery, hardwired link, restart button

**Date:** 2026-08-12
**Status:** approved, not yet implemented
**Touches:** `cad/back_cover.py`, `cad/README.md`, and a small firmware module for the button

## Problem

Four changes to the rear enclosure, all landing in the same part:

1. Cover only the bracket footprint today; enclose the whole screen back instead.
2. Nowhere to put a battery for ride-through when the cabinet loses power.
3. The machine link is a loose loom; it should be a hardwired, protected cable.
4. No reachable restart — the RESET switch is 3.4mm down a slot.

## Decisions

Recorded with the rejected options so they are not silently revisited.

| Decision | Chosen | Why |
|---|---|---|
| Battery role | UPS / ride-through | Panel, DRO and link survive a power blip or the e-stop cutting the cabinet |
| Charger + boost | **None needed** | The board already has them — Waveshare item 13 is a 3.7V lithium PH2.0 header, items 19/20/21 are DONE / CHARGE / POWER indicators. An earlier version of this design mounted a TP4056 and a boost converter; both were wrong |
| Depth for the cell | Local bump-out, left strip | Outside the VESA 100×100 so the arm still bolts flat, and the furthest point in the box from the ESP32 and backlight supply |
| Cell access | Sealed in, opens with the cover | One part, no seam, stiffer. Accepted trade: swapping the cell means taking the panel off the arm, because the M3 cover screws sit under the VESA plate |
| Cable entry | Hardwired onto the adapter's terminal block | The panel is arm-mounted with the loom captive in the Ø12 tray under two bridges, so detachability buys almost nothing, while a connector is one more thing to vibrate loose. The adapter is kept anyway — its TVS diodes are the part earning their place near a VFD, not its socket |
| Strain relief | Two-screw printed saddle | A knot grips only the jacket and lets conductors creep, putting the strain on solder joints |
| Button target | Panel reboot (`ESP.restart()`) | Assumed, not confirmed — see Open questions |
| Button wiring | Sensor header (3V3 / GND / GPIO) | A free GPIO with power and ground beside it. Plugs in, no soldering, no argument over the pin budget |

### Non-goals

- No exposed RJ12 socket. The jack is not populated or brought to a wall.
- No separate battery door.
- No `$Bye` controller restart on this button. SETUP already has one on screen.

## Geometry

### The glass outline, derived not measured

`back_cover.py` never records the glass origin, but it does say the VESA pattern
is centred on the glass, and gives `VESA_CX = 0.0`, `VESA_CZ = 1.02`. With the
glass at 192.96 × 110.76 that fixes it:

```
glass X : 0.00 ± 96.48   ->  -96.48 .. +96.48
glass Z : 1.02 ± 55.38   ->  -54.36 .. +56.40
```

The overhang is asymmetric — 15.43mm on the left, 12.13mm on the right —
because the bracket spans -81.05..84.35, whose centre is +1.65, not 0.

The new outline is the glass **inset 0.3mm** on every side, so print tolerance
cannot leave a lip standing proud of the glass to catch on.

### Port scallops — required, not cosmetic

Moving the walls out to the glass moves them away from the ports behind them:

| Opening | Now | Naive full-back | With scallop |
|---|---|---|---|
| USB-C, left wall | 23mm inboard | ~38mm | ~23mm |
| microSD, top wall | 22mm down | ~27mm | ~22mm |
| BOOT/RESET/UART, bottom | — | +3.4mm | unchanged |

38mm of tunnel defeats a normal USB-C overmould, and one of those ports is the
flashing port, which the script is explicit must never be sealed in. So at each
of the three openings the wall is locally stepped back to roughly its present
position. They are vertical faces in the print orientation, so they cost nothing
in supports.

### Battery bay

- Left strip, X from the inner left wall to **-53**, clear of the VESA 100×100.
- Bump-out projects rearward from `CAVITY_BACK_Y`; ~20mm proud, giving ~51mm
  total from the glass in that corner only.
- Cell vertical along Z: 69mm (protected 18650) inside a 110mm-tall cover.
- Cradle bored Ø19.0 for an 18.6mm cell.
- Two wires to the existing battery header, which sits at the PCB's top-left —
  next to the bay, so the run is short.

### Cable entry and adapter tray

- Entry through the **rear wall**, alongside the existing 11 × 30 loom slot, so
  the cable still leaves pointing down at the arm and drops into its tray.
- Two-screw printed saddle clamping the jacket, immediately inside the entry.
- Adapter board mounted on two printed bosses, on the rear wall inboard of the
  entry.

Measured board:

```
RJ_BOARD_X    = 30.0    # along the edge the holes reference
RJ_BOARD_Z    = 28.0
RJ_HOLE_D     = 3.25    # M3 clearance
RJ_HOLE_PITCH = 21.0    # centred on the 30mm span -> 4.5mm from each end
RJ_HOLE_EDGE  = 4.25    # offset from the 30mm edge
```

Bosses are Ø7 with a 2.5mm pilot, so an M3 self-taps straight into the plastic;
drill to 4.2 for a heat-set insert instead if the board will come out often. Two
screws only, so the board is located by the pair plus a lip against one edge to
stop it pivoting.

### Restart button

Momentary switch between the sensor header's GPIO and its GND, `INPUT_PULLUP`.
Mounted through the **bottom wall**, recessed below the surrounding face so a
knock cannot press it.

## Firmware

`src/model/holdbutton.{h,cpp}` — pure, no Arduino, host-tested like
`calibration.cpp` and `sendq.cpp`:

```c
typedef struct { bool was_down; uint32_t since_ms; bool fired; } holdbtn_t;

void holdbtn_reset(holdbtn_t* b);
// Feed the debounced level each poll. True EXACTLY ONCE, when the hold
// completes; the button must be released before it can fire again.
bool holdbtn_update(holdbtn_t* b, bool down, uint32_t now_ms);
```

A **1000ms hold**, not a tap. This reboots the panel: mid-job that costs the
on-screen HOLD and RESET for the ~3s of boot, so it must take intent. The caller
in `main.cpp` reads the pin, feeds it, and calls `ESP.restart()` on true.

Tests: fires only after the full hold, exactly once while held, release re-arms,
a bounce shorter than the hold does not fire, and a hold starting at
`millis() == 0`.

## Unverified constants

Both are parameters, not blockers. Each gets a `# UNVERIFIED` comment so it is
obvious what has and has not been measured — this file's own rule is that
nothing is a guessed number.

| Constant | Needed for | How to get it |
|---|---|---|
| `RJ_STACK_H` — tallest thing on the adapter, measured from the board face | Bump-out/cavity clearance over the board | Calipers over the fitted RJ12 jack. **This is the one that can still bite:** a 6P6C jack stands ~13mm, which with a 1.6mm board is ~15mm of the 20.3mm cavity. It fits only where the panel PCB behind it is shallow, so the board's position depends on this number. If the jack is left unpopulated the problem disappears |
| `RESTART_GPIO` | Firmware only | Flash a probe setting 6, 15 and 16 to `INPUT_PULLUP` and print them; short the header's GPIO to GND and read which drops. By elimination it is one of those three — CAN is muxed onto the USB pins per `EXIO_CAN_SEL`, so RS485 takes two and the sensor header the last. GPIO6 is the likely one |

## Verification

`cad/README.md` records that the current cover was checked against Waveshare's
STEP with a boolean interference test, against a positive control. **That STEP
is not in the repo** (`assembly.py` has `PANEL_STEP = None`, and it is nowhere
on this machine), so re-running that check after these changes means fetching
`ESP32-S3-Touch-LCD-7B_Drawing.zip` again. The changes here move walls outward
and add a bump-out behind existing free space, so a clash is unlikely — but
"unlikely" is not what the current part was held to, and it should not be the
standard for the revision.

Also to re-check after the change:

- Bump-out clears the VESA plate's 100 × 100 footprint.
- Rear face still prints flat with no supports; the bump-out becomes a raised
  island, walls vertical.
- Volume and mass — the README quotes 81.8 cm³ / ~65g, and the arm's stress sum
  assumes ~450g for panel plus cover.

## Known risks

- **Thermal.** The README already flags no ventilation around an ESP32-S3
  driving a 1024×600 panel at a 30MHz pixel clock. Adding a sealed li-ion cell
  makes that a safety question rather than a comfort one. Vents go in this
  revision: inlets low, outlets high, on the walls rather than the top face so
  chips do not drop in. The cell sits at the far end of the box from the ESP32
  and the backlight supply.
- **Onboard charger capacity.** The panel draws 500–800mA. Whether the board's
  charger and its battery path can actually run it, and for how long, needs the
  schematic. If it cannot, the UPS premise fails and the bay becomes a place to
  keep a cell rather than a working ride-through.
- **Nothing here has been printed or test-fitted**, which was already true of
  the existing parts.
