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
bosses stand up off the floor — **no supports**. The only overhang is the top of
the cable slot, which bridges fine.

## What is open, and why

- **Rear wall, one 11 × 30 slot** — power, UART and I2C leave through the back,
  pointing down at the arm, which is where the loom is going anyway. It sits in
  the corridor right of the VESA plate and between the two M3 columns, so
  nothing around it is thin. From there the loom drops into the arm's tray.
  (These used to be three holes in the *side* wall, which put the cables into
  free space beside the panel with nothing to follow — that is what snags.)
- **Left wall, slot** — both USB-C ports. One of them is the flashing port, so
  it is never allowed to be sealed in. A plug has to reach 23 mm inboard to find
  the socket, which is why the opening is sized for the overmould.
- **Top wall** — microSD. See the caveat below.
- **Bottom wall, slot** — BOOT, RESET and the UART-select switch.

---

# Table mount — `arm_mount.py`

Ball-and-socket arm, VESA 75 at the panel end, bolted flat to the bench. Three
printed parts, one printed twice.

| Part | Qty | Notes |
|---|---|---|
| `BasePlate` | 1 | 110 × 80 × 10 with a Ø20 ball on a 25 mm neck |
| `ArmHalf` | **2** | identical; together they make a double socket arm |
| `VesaPlate` | 1 | VESA 75, counterbored for M4 caps, Ø20 ball |

Hardware: 4 × bench screws · 4 × M5×30 + 4 M5 nyloc · 4 × M4×12 into the back
cover.

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

## Cable tray

A Ø12 channel runs the length of the arm's outer face, with **two moulded
bridges** spanning it — thread the loom under them and it is captive without a
zip tie, and there is nothing sticking out to catch.

It sits on the arm's neutral axis for the bending that matters. The panel hangs
in the plane containing the arm and gravity, so the arm bends about the bolt
axis and the stress varies across the width, not down the middle where the
channel is. The tray stops at ±42 mm, well short of the sockets at ±55.

Both halves get a tray, so the assembled arm has one on each side — use
whichever faces away from the work.

**Not done: routing through the balls.** It is possible — a Ø6 bore costs the
neck only ~8% of its bending strength, because a tube is efficient — but a
PH2.0 plug is 8 × 4.5 mm and will not pass it. You would be threading bare wire
and crimping after assembly, and every adjustment would work the cable inside
the joint. Say the word if you want it anyway.

## Fixing to the bench

Four holes on an 84 × 56 mm rectangle, Ø7 through with a Ø14 × 4 mm flat
counterbore, so a 1/4in lag or M6 head sinks flush on a washer. `SCREW_CLR` and
`SCREW_HEAD_D` size them for anything else; for flat-head screws swap the
counterbore for a 90° countersink.

The load is small. The panel hangs 167 mm forward of the base, which is 0.74 N·m
of tipping about the front screws; with the rows 56 mm apart that is 13 N shared
between the rear pair — under 7 N of pull-out each. Any screw that bites will
hold it.

## Print

- `ArmHalf` — **socket face up**. The cavities then open upward and print as
  bowls with no overhang at all.
- `BasePlate`, `VesaPlate` — plate down, ball up. No supports.

---

# Print files — `export_print.py`

```python
exec(open("cad/export_print.py").read())
```

Writes `cad/print/` — four STLs plus `cnc_console_all.3mf` holding the lot.
Every part comes out **already in its print orientation and seated on Z=0**, so
they drop onto the Bambu plate with no rotating.

| File | Qty | Footprint | Height |
|---|---|---|---|
| `back_cover_x1.stl` | 1 | 165.4 × 102.2 | 23.3 |
| `arm_half_x2.stl` | **2** | 122.7 × 44.0 | 13.5 |
| `base_plate_x1.stl` | 1 | 110.0 × 80.0 | 45.0 |
| `vesa_plate_x1.stl` | 1 | 100.0 × 100.0 | 40.0 |

All four verified solid, manifold and free of self-intersections after meshing.

Meshed at 0.04 mm linear / 0.2° angular. That tolerance is not cosmetic: the
ball and socket are a running fit on 0.25 mm of clearance, and a coarse
tessellation would eat most of it.

**Seating is done on the mesh, never on the shape.** `Shape.BoundBox` is the
loose NURBS-pole box on anything curved — on the arm half it reads ~9 mm below
the real underside, and trusting it floats the part that far off the bed.

## Suggested settings

- **No supports on anything.** Every part is oriented so it does not need them.
- PETG for `ArmHalf`, `BasePlate` and `VesaPlate` — they carry the load, and the
  neck prints across the layers. PLA is fine for the cover.
- 4 walls / 30% infill on the load path; 3 walls / 15% is plenty for the cover.
- The arm half is the one to print first. It carries every assumption that
  cannot be checked without hardware: the 0.25 mm ball clearance, the 2 mm
  squeeze gap, and whether the bowl prints clean.

---

# Assembly — `assembly.py`

Poses the whole stack — panel, back cover, VESA plate, arm, base and a slab of
bench — so the geometry can be checked before anything is printed.

```python
exec(open("cad/assembly.py").read())
```

Everything is posed in **panel coordinates**, the frame the vendor STEP already
uses, because the panel is the one thing already correctly placed. The table
comes out tilted in that frame; that is expected, and world "up" is
`TABLE_NORMAL` in the script. Set `PANEL_STEP` to the vendor model to see the
board inside; without it you get the printed parts alone.

The pose is not decorative. Both balls are set to 30°, inside the measured ±40°,
and the sitting angle falls out of that rather than being chosen:

| | |
|---|---|
| Screen angle | 30° above horizontal |
| Panel height | 100–200 mm above the table top |
| Reach | 167 mm forward of the base plate's front edge |
| Interference | zero, among all printed parts, the panel and the table |

Change `JOINT_DEG` to see other poses. It is the angle at *each* ball, so the
screen ends up at twice it above horizontal — 40° gives a near-vertical screen,
20° gives a drafting-board angle.

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
- **The panel hangs 167 mm forward of the base**, so the fixing carries a
  tipping moment rather than just weight. It is small (see above), but it grows
  with reach — do not stretch the arm without redoing that sum.
- **Nothing has been printed or test-fitted.** Everything above is measured off
  the model.

---

# Lean-to cabinet — `lean_to.py` (CrowPanel Advance 7.0)

A fixed wedge bolted to the table top, with the face leaning back 30°. The
panel's stock front bezel frame **comes off** and is not used. The front wall is
a plain 5.58 mm plate, the LCD module's thickness, with a window for the LCD.
The PCB bears on the back of the plate and the LCD fills the window flush with
the face. Nothing else on the PCB's front side reaches the plate; this was
checked against the vendor STEP.

```python
exec(open("cad/lean_to.py").read())
```

It writes `lean_to.step` (model pose) and `print/lean_to_*.stl` (print pose).
Drop `ref/crowpanel_advance7.stp` (Elecrow's STEP, gitignored, 40 MB) in place
and the panel shows up as a mockup and is included in the clash check.

| Part | Qty | Print |
|---|---|---|
| `lean_to_shell_x1` | 1 | **face down**, 205 × 132 × 96, no supports |
| `lean_to_cover_x1` | 1 | flat, back face down (hinge tabs and hairpin up) |
| `lean_to_lens_x1_clear` | 1 | **transparent PLA**, flange down |

Hardware: 4 × 1/4-20 bolts + hex nuts (table). Nothing else: the panel and
the back flap both snap in.

- **Panel.** Remove the four stock corner screws and lift off the front bezel
  frame (discard it). Drop the LCD into the window with the PCB against the back
  of the plate, then press the middle frame and acrylic back on behind them. The
  front wall carries **four snap pins** at the panel's corner holes. They are
  Ø3.0 with a split Ø3.8 barb, printed with the shell, and their tips spring
  open 0.1 mm behind the acrylic. **Drill the corner holes in the middle frame
  and acrylic back out to 1/8"** (they are 2.7 mm stock; the PCB is already
  3.2 mm). To release, squeeze the split tips with pliers. Print the shell in
  PETG so the tines flex instead of snapping. `SNAP_GAP` takes up stack
  tolerance if the barbs don't catch or the panel rattles.
- **Table.** 4 × 1/4-20 from underneath into nuts that slide sideways into the
  two floor rails. Bolt centres are 181.26 mm apart across and 49.14 mm apart
  front to back. The front pair is 10.6 mm behind the foot's front edge.
- **Cables.** The bottom is open, so the wires leave under the lean-to. The
  back cover is plain.
- **Status bar.** The 8-px SK6812 stick sits under a slot in the roof, centred
  across the width. Only the lit strip shows: 50.0 × 5.35 mm, which is
  49.4 × 4.75 plus 0.3 per side. A clear PLA lens fills the slot flush with the
  roof. Its flange sits in a pocket on the inside, clamped between the roof and
  the LEDs. The board (51 × 10.2) goes in behind the lens, LEDs first, and is
  held with glue or tape. `SLOT_AT` slides it along the roof (0.35: it has to stay forward of the
  flap latch's catch ledge).
- **Inside corners.** Every concave edge has an R1.5 fillet (`R_FILLET`),
  applied by an edge finder rather than picked by hand. Corners where a square
  part has to seat stay sharp: the LCD window, the snap-pin roots, the nut
  pockets and slots, the insert holes, the LED window and pocket, and the
  rail/lug ends that the cover stops against. An audit pass confirms that no
  other concave edge is left sharp. The cover's top corners are cut at 45° to
  clear the wall/roof fillets.
- **Outside edges.** Every external edge of the shell (on the outer skin) has a
  1 mm chamfer (`CHAMFER`), with the same fit features left sharp. The flap is
  not chamfered. Every fillet and chamfer batch must still mesh watertight or
  it is refused. A fillet OCC refuses at R1.5 is retried at R1.0, then R0.5; the
  build report lists which.
- **Back flap.** No screws. It hinges at the bottom on two Ø3 pins, on tabs
  inside the rails, that spring into Ø3.4 bores in the rails' inner faces. Bow
  the flap slightly to snap them in. It is held shut by a **hairpin spring**
  at the top centre. The lower leg comes off the flap's inner face and runs
  forward, U-turns at a rounded nose in front of a catch ledge under the roof,
  and the upper leg runs back under the ledge to a thumb tab that fills a notch
  in the flap's top edge, flush with the back face. Closing, the ledge rides the
  nose's 45° lead-in and deflects the hairpin, and the nose's barb snaps up in
  front of the 75° catch face (0.9 mm engagement). **To open, press the tab
  in** (about 2 mm of the 4 mm before it meets the ledge, 1.7% strain): that
  bends both legs and drops the nose 0.6 mm past the barb. It needs the margin
  because the nose sits forward of the hinge axis and rises about 0.7 mm as the
  flap opens. The leg gap runs out through the back face, so the upper leg and
  tab are free and the lower leg is the only root. The bottom corners are notched over the rails, and the bottom edge
  sits 4.3 mm off the table, so the flap clears the table and the rail ends all
  the way to 90° open, where it lies on the table. Checked: shut, in 5–90°
  steps, and a 0.6 mm pull (the barb must hit the ledge). The hairpin is also
  swept through the swing: at rest it binds on the ledge for the first 5° (the
  latch) and is clear from there; with the tab pressed it clears the whole way.
- **USB-C** (flashing) is reached through a slot in the side wall.
  **BOOT/RST** are reached from the back with the cover off.

Checked: zero interference with the vendor STEP. Elecrow's model places the
LCD 0.2 mm into the PCB, so in the mockup the LCD shows 0.2 mm below the face.
On the real part it is flush if the LCD is 5.58 mm, as measured. Positive controls, the stack pushed forward 1 mm and the STEP
pushed forward 0.5 mm, both register a clash. Both STLs mesh closed and
manifold.

Not printed or test-fitted yet.
