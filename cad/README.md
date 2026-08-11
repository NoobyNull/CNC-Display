# Rear enclosure — Waveshare ESP32-S3-Touch-LCD-7B

`back_cover.py` builds the part. Run it from the FreeCAD Python console:

```python
exec(open("cad/back_cover.py").read())
```

It writes `back_cover.FCStd`, `.step` and `.stl` next to itself. Only the script
and the STEP are tracked; the other two are regenerated.

Every panel dimension in the script came from measuring Waveshare's own STEP
(`ESP32-S3-Touch-LCD-7B_Drawing.zip`), not from the datasheet drawing. The model
was checked against that STEP with a boolean interference test — zero clashes,
verified against a positive control that shifts the cover 4 mm forward and does
report clashes.

## Numbers

| | |
|---|---|
| Outline | 165.40 × 102.15 mm, corners R4 |
| Depth | 23.30 mm behind the bracket face; 31.80 mm from the front glass |
| Walls / rear | 2.5 mm / 3.0 mm |
| Volume | 81.8 cm³ (~65 g printed at 20% infill) |
| Mount to panel | 4 × M3×25 socket cap, heads sunk flush in the rear face |
| Arm mount | VESA 75 × 75, tapped M4, centred on the glass |

## Print

Rear face down on the bed. Every wall is then vertical and the columns and
bosses stand up off the floor — **no supports**. The only overhangs are the
tops of the three Ø8 cable holes, which bridge fine.

## What is open, and why

- **Right wall, 3 × Ø8** — power, UART and I2C, spread across the PH2.0 terminal
  bank so the cable run from any of the six connectors is short. Sized for an
  8 mm grommet if you want strain relief.
- **Left wall, slot** — both USB-C ports. One of them is the flashing port, so
  it is never allowed to be sealed in. A plug has to reach 23 mm inboard to find
  the socket, which is why the opening is sized for the overmould.
- **Top wall** — microSD. See the caveat below.
- **Bottom wall, slot** — BOOT, RESET and the UART-select switch.

## Caveats

- **The SD card sits 22 mm down its opening.** The socket is at the PCB's top
  edge and the cover's top wall is level with the bracket, so swapping a card
  means tweezers. If the panel SD is going to be the working G-code source, this
  wants a chute down to the socket, or a top wall that steps down to meet it.
- **M3 thread engagement in the bracket is shallow** — the vendor STEP shows
  only ~1.7 mm of thread. Do not torque these, and check that a 25 mm screw does
  not bottom out before the cover is clamped.
- **No ventilation.** A closed box around an ESP32-S3 driving a 1024×600 panel
  at a 30 MHz pixel clock. Nothing is known to overheat yet; if it does, slots
  in the bottom and top walls are the cheap fix.
- **The 9DOF sensor is assumed to be off-board**, on the machine, wired back
  through one of the cable holes. There is no pocket for it in here.
