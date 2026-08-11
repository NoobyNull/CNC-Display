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

---

# Table mount — `arm_mount.py`

Ball-and-socket arm, VESA 75 at the panel end, clamps a table edge without
drilling. Four printed parts, one printed twice.

| Part | Qty | Notes |
|---|---|---|
| `BasePlate` | 1 | 110 × 80 × 10 with a Ø20 ball on a 25 mm neck |
| `ClampBar` | 1 | goes under the table top, two captive M8 nuts |
| `ArmHalf` | **2** | identical; together they make a double socket arm |
| `VesaPlate` | 1 | VESA 75, counterbored for M4 caps, Ø20 ball |

Hardware: 2 × M8 (table thickness + 35 mm) + 2 M8 nuts · 4 × M5×30 + 4 M5 nyloc
· 4 × M4×12 into the back cover.

## Range

**±40° at each ball**, measured by sweeping the neck against the assembled
socket in 5° steps until it interferes — not calculated and hoped for. Plus
unlimited roll about each ball axis, so the panel swings freely around the base
and can be rolled to portrait.

The socket mouth is 19.80 mm across against a 20.00 mm ball, so the ball is
captive before the bolts are even touched.

## Why the angles are what they are

A printed sphere on a vertical neck has a horrible underside, so the neck blends
into the ball on a 45° cone — the steepest overhang anywhere on these parts, and
it needs no support. That cone consumes the ball surface nearest the neck, which
is why the socket mouth is opened to 75°. Usable tilt is the difference.

## Clamp

The bolts drop **past** the table edge, not through the table: the plate lands
72 mm of bearing on the top, the bar reaches 38 mm back underneath, and
tightening pinches the edge between them. The bar's nuts are recessed into its
underside so its top face stays flat against the table.

## Print

- `ArmHalf` — **socket face up**. The cavities then open upward and print as
  bowls with no overhang at all.
- `BasePlate`, `VesaPlate` — plate down, ball up. No supports.
- `ClampBar` — either way.

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
- **The neck is the weak point of the arm**, and it prints across the layers,
  which is the direction printed plastic is worst in. It is Ø11 for that reason.
  Panel plus cover is ~450 g on a ~150 mm lever, about 5 MPa of bending in the
  neck — fine in PETG, fine in PLA, and the first thing to reprint in something
  tougher if it ever lets go.
- **Nothing has been printed or test-fitted.** Everything above is measured off
  the model.
