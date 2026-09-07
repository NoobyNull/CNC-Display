# Switch-panel vanity plate, 110mm tall, three columns: E-stop | H100 VFD
# remote display pod with the 1/4in power toggle under it | mains rocker.
# Every column's contents centre on the plate height, and the lip over
# the wood opening is symmetric all round. Labels under the switches.
#
# The plate is the panel: each component's own hardware clamps it (toggle
# nut, E-stop clamp ring, mains module's snap tabs / bezel). It sits in a
# CNC-cut wood opening, registered by an inset lip — a rim on the back,
# LIP_W in from the edge, that drops into the cutout. Two ways to cut the
# wood, same part:
#   proud:  one through-opening matching the rim outline (+~0.5mm slop);
#           the face overlaps the wood by the LIP_* amounts, proud by
#           PLATE_T. Compression tabs on the rim take up the slop.
#   flush:  same opening, plus a PLATE_T-deep rabbet matching the plate
#           outline (+ slop) — the face lands level with the wood.
# Corners everywhere are R4, which clears a 1/4in bit (r3.18).
#
# Layout is derived, not drawn: each control gets a zone as wide as the larger
# of its hardware clearance and its label, zones are packed left to right, and
# the plate wraps the result in an even margin. Change a label or a diameter
# and everything reflows.
#
# Print: FACE DOWN (flip in the slicer), rim pointing up — no supports.
# The engraving lands on the bed; elephant-foot relief is chamfered on the
# face's outer edge only, so the letters stay crisp.
#
# Run from the FreeCAD Python console:
#   exec(open("cad/switch_plate.py").read())

import FreeCAD as App
import Part
import Mesh
import math
import os

# ---- panel hardware (all measured by Matthew, 2026-09-05) ----
TOG_HOLE_D = 6.0 + 0.3  # measured 6mm bushing + print slop
TOG_CLEAR = 16.0       # keyway washer / hex nut Ø~14 + room to spin it
TOG_NUT_R = 6.5        # rear nut, Ø~13 across corners — must clear the
                       # wood opening's edge behind the plate
TOG_WASH_R = 6.5       # front washer/nut radius on the face
ESTOP_HOLE_D = 22.3    # measured 22mm, the standard pushbutton cutout; +slop
ESTOP_HEAD_D = 40.0    # mushroom head — sets that zone's clearance
MAINS_W = 22.0 + 0.3   # measured 46 x 22 rocker cutout + slop, mounted tall
MAINS_H = 46.0 + 0.3
MAINS_BEZEL = 3.0      # snap-in flange overhang past the cutout, per side
MAINS_R = 2.0          # cutout corner radius (cutter-style, like estop_box)
# The rocker's retainers want 52mm overall; per Matthew, keep the hole
# proper and give the retainers a narrower notch, opening total 51mm —
# split equally, 2.5mm past EACH end, everything centered on the row.
MAINS_NOTCH_W = 18.0 + 0.3
MAINS_NOTCH_L = 51.0 + 0.3
WOOD_T = 12.44         # measured surface thickness — RIM_D=12 stops 0.4
                       # shy of the back; tab bumps land mid-thickness
# H100 VFD's removable display pod (remote-mounted on its ribbon cable),
# measured: 52.5 wide x 67 tall. Plain rectangular cutout — bench-verified
# the pod seats without retainer notches (2026-09-06, first print).
DISP_W = 52.5 + 0.3
DISP_H = 67.0 + 0.3
DISP_BEZEL = 3.0       # pod frame overhang past the cutout, assumed like
                       # the rocker's — shrink if the frame is slimmer

# ---- plate ----
PLATE_T = 2.4          # snap-in mains modules grip 1-3mm panels; E-stop
                       # clamp rings take up to ~6 — thin end wins
MARGIN = 8.0           # plate edge to any zone
GAP = 5.0              # between zones
CORNER_R = 4.0         # house corner radius (back_cover is R4)
CHAMFER_BED = 0.8      # elephant-foot relief on the bed-contact edges

# ---- rear registration lip ----
LIP_SIDE = 6.0         # face overlap onto the wood — symmetric all
LIP_TOP = 6.0          # round, per Matthew after the first print (the
LIP_BOT = 6.0          # earlier 1/2in bottom lip is reverted)
RIM_W = 2.4            # rim wall — one 0.4-nozzle perimeter stack
RIM_D = 12.0           # rim depth = tab flex length; a shorter rim makes
                       # the tabs too stiff (strain ~ 1/length^2)

# ---- compression tabs ----
# Cantilever fingers cut into the rim wall: two slots free a TAB_W tongue
# anchored at the face, with a ramped bump proud of the rim. Push the plate
# in and they take up the cutout slop. Bump peak sits 6.5-8mm behind the
# face; wood thinner than ~8mm lets the bumps clear the back edge and
# snap-lock instead — either way it grips.
TAB_W = 10.0
TAB_SLOT = 2.0
TAB_PROUD = 0.8        # interference; ~2% root strain at full squeeze —
                       # fine in PETG, drop to 0.6 for brittle PLA
TAB_RAMP_IN = 4.0      # tip-side lead-in ramp (insertion)
TAB_FLAT = 1.5         # full-proud dwell
TAB_RAMP_OUT = 1.5     # face-side ramp (printable, and removable)

# ---- labels ----
FONT = "/usr/share/fonts/liberation/LiberationSans-Bold.ttf"
TEXT_BOLD = 0.2        # stroke fattening radius: each label is fused
                       # with 8 copies shifted this far around a circle,
                       # thickening the font's ~0.7mm thin bars to ~1.1 —
                       # so even after the inserts lose INSERT_CLR per
                       # side they keep Matthew's 0.8 two-line minimum.
                       # (makeOffset2D slivers or nulls on these glyphs;
                       # translated-copy dilation never does.)
TEXT_SIZE = 7.0 - 2 * TEXT_BOLD  # trimmed so bolding lands back on the
                       # same ~6.5mm caps the layout math expects
TEXT_DEPTH = 1.2       # pocket depth for the colour letter inserts —
                       # leaves a 1.2 web in the 2.4 face, and gives the
                       # separate letters 6 layers of body at 0.2
LABEL_GAP = 3.0        # below the deepest hardware, above the text
LABEL_PAD = 2.0        # zone must exceed its label by this, per side
LABELS = ("E-STOP", "POWER", "MAINS")


def text_shape(txt, bold, depth):
    """Extruded text solid at the origin (baseline y=0, z=0..depth) and
    its width, stroke-fattened by dilation radius `bold`."""
    chars = Part.makeWireString(txt, FONT, TEXT_SIZE, 0.0)
    faces = []
    for ch in chars:
        if not ch:
            continue
        ws = sorted(ch, key=lambda w: Part.Face(w).Area, reverse=True)
        try:
            f = Part.Face(ws)          # one face, counters as holes
        except Exception:
            f = Part.Face(ws[0])
            for w in ws[1:]:
                f = f.cut(Part.Face(w))
        faces.append(f)
    # Fuse, don't compound: bolded neighbours can touch, and a cutter
    # that self-intersects wrecks the boolean.
    solid = None
    for f in faces:
        s = f.extrude(App.Vector(0, 0, depth))
        solid = s if solid is None else solid.fuse(s)
    base = solid.copy()
    for i in range(8):     # dilate: union of shifted selves = bold
        a = i * math.pi / 4
        c = base.copy()
        c.translate(App.Vector(bold * math.cos(a),
                               bold * math.sin(a), 0))
        solid = solid.fuse(c)
    return solid, solid.BoundBox.XLength


labels = [text_shape(t, TEXT_BOLD, TEXT_DEPTH + 1) for t in LABELS]
text_h = max(s.BoundBox.YLength for s, _ in labels)  # actual cap height

# Zones (columns), left to right: E-stop | display with the 1/4in toggle
# under it | mains. Width is hardware clearance vs label, whichever wins.
disp_w_ext = DISP_W + 2 * DISP_BEZEL
mains_w_ext = MAINS_W + 2 * MAINS_BEZEL
zones = [max(ESTOP_HEAD_D + 2.0, labels[0][1] + 2 * LABEL_PAD),
         max(disp_w_ext + 2.0, TOG_CLEAR, labels[1][1] + 2 * LABEL_PAD),
         max(mains_w_ext + 2.0, labels[2][1] + 2 * LABEL_PAD)]

# Vertical: 110mm per Matthew — the minimum that stacks display over
# toggle. E-stop and mains center on the plate; the display rides as high
# as its notch allows (0.5 to the rim wall); the toggle drops in beneath.
PLATE_H = 110.0
PLATE_W = 2 * MARGIN + sum(zones) + 2 * GAP
ROW_CY = PLATE_H / 2
LABEL_Y = MARGIN                          # common baseline, bottom band
# Centre column, derived bottom-up: toggle just above its label, display
# just above the toggle — the whole stack (label, toggle, display) then
# centres on the plate height like its neighbours.
TOG_CY = LABEL_Y + text_h + 0.5 + TOG_WASH_R
DISP_CY = TOG_CY + TOG_WASH_R + 0.5 + DISP_BEZEL + DISP_H / 2
assert DISP_CY + DISP_H / 2 <= PLATE_H - LIP_TOP - RIM_W - 0.5, \
    "display cutout runs into the top rim wall"
assert TOG_CY - TOG_NUT_R >= LIP_BOT + 0.5, \
    "toggle nut lands on the wood edge"

cx = []                                   # zone centers, left to right
x = MARGIN
for z in zones:
    cx.append(x + z / 2)
    x += z + GAP

doc = App.newDocument("switch_plate")

plate = Part.makeBox(PLATE_W, PLATE_H, PLATE_T)
corners = [e for e in plate.Edges
           if e.BoundBox.XLength < 0.01 and e.BoundBox.YLength < 0.01]
plate = plate.makeFillet(CORNER_R, corners)
# Prints face-down, so the face plane is the bed plane.
plate = plate.makeChamfer(CHAMFER_BED,
                          [e for e in plate.Edges
                           if e.BoundBox.ZMin > PLATE_T - 0.05])

# Rim: a rectangular ring on the back, inset LIP_W from the plate edge.
def _rounded_box(w, h, d, r, org):
    b = Part.makeBox(w, h, d, org)
    vs = [e for e in b.Edges
          if e.BoundBox.XLength < 0.01 and e.BoundBox.YLength < 0.01]
    return b.makeFillet(r, vs)

rx0, rx1 = LIP_SIDE, PLATE_W - LIP_SIDE
ry0, ry1 = LIP_BOT, PLATE_H - LIP_TOP
rim = _rounded_box(rx1 - rx0, ry1 - ry0, RIM_D,
                   CORNER_R, App.Vector(rx0, ry0, -RIM_D))
rim = rim.cut(_rounded_box(rx1 - rx0 - 2 * RIM_W, ry1 - ry0 - 2 * RIM_W,
                           RIM_D + 2, CORNER_R - RIM_W / 2,
                           App.Vector(rx0 + RIM_W, ry0 + RIM_W,
                                      -RIM_D - 1)))
plate = plate.fuse(rim)

# Tabs: (wall axis, center along the wall, wall outer coord, press sign).
# Two per wall now that both spans are long.
qx = (rx1 - rx0) / 4
qy = (ry1 - ry0) / 4
tabs = [('x', rx0 + qx, ry0, -1), ('x', rx1 - qx, ry0, -1),
        ('x', rx0 + qx, ry1, +1), ('x', rx1 - qx, ry1, +1),
        ('y', ry0 + qy, rx0, -1), ('y', ry1 - qy, rx0, -1),
        ('y', ry0 + qy, rx1, +1), ('y', ry1 - qy, rx1, +1)]

for along, c, wall, sign in tabs:
    # Two slots free the tongue; they stop at the face, which is the hinge.
    lo = min(wall + sign * 1.0, wall - sign * (RIM_W + 1.0))
    for s in (-1, 1):
        off = c + s * (TAB_W / 2 + TAB_SLOT / 2) - TAB_SLOT / 2
        if along == 'x':
            plate = plate.cut(Part.makeBox(TAB_SLOT, RIM_W + 2, RIM_D,
                                           App.Vector(off, lo, -RIM_D)))
        else:
            plate = plate.cut(Part.makeBox(RIM_W + 2, TAB_SLOT, RIM_D,
                                           App.Vector(lo, off, -RIM_D)))
    # Bump: ramp in from the tip, dwell, ramp back toward the face.
    z = (-RIM_D, -RIM_D + TAB_RAMP_IN, -RIM_D + TAB_RAMP_IN + TAB_FLAT,
         -RIM_D + TAB_RAMP_IN + TAB_FLAT + TAB_RAMP_OUT)
    u = (wall, wall + sign * TAB_PROUD, wall + sign * TAB_PROUD, wall)
    if along == 'x':
        pts = [App.Vector(c - (TAB_W - 2) / 2, ui, zi) for ui, zi in zip(u, z)]
        sweep = App.Vector(TAB_W - 2, 0, 0)
    else:
        pts = [App.Vector(ui, c - (TAB_W - 2) / 2, zi) for ui, zi in zip(u, z)]
        sweep = App.Vector(0, TAB_W - 2, 0)
    poly = Part.makePolygon(pts + [pts[0]])
    plate = plate.fuse(Part.Face(poly).extrude(sweep))

# ---- face ribs ----
# Cross-bracing for the 2.4 face: a vertical rib in each GAP between
# columns (the only full-height corridors with nothing behind the face),
# and two horizontal ribs across the left region, above and below the
# E-stop/toggle row, clearing the E-stop's ~Ø32 clamp ring by 2mm. All
# ribs stop 2mm short of the rim walls so they can never re-bridge a
# compression-tab slot; the face itself carries them, and they fuse where
# they cross. The right column's back stays rib-free — the display and
# rocker retainers need it clear to latch.
RIB_T = 2.4
RIB_H = 6.0            # half the rim depth is plenty for a 2.4 face
iy0, iy1 = ry0 + RIM_W + 2.0, ry1 - RIM_W - 2.0
ix0 = rx0 + RIM_W + 2.0
rib_xs = [cx[i] + zones[i] / 2 + GAP / 2 for i in (0, 1)]
for rbx in rib_xs:
    plate = plate.fuse(Part.makeBox(RIB_T, iy1 - iy0, RIB_H,
                                    App.Vector(rbx - RIB_T / 2, iy0,
                                               -RIB_H)))
ring_r = 18.0          # E-stop clamp ring Ø~32 plus clearance
ix1 = rx1 - RIM_W - 2.0
rib_y_lo = (iy0 + ROW_CY - ring_r) / 2
rib_y_hi = (ROW_CY + ring_r + iy1) / 2
# Horizontal ribs brace the E-stop column above and below the row. The
# display and mains columns stay rib-free behind — their retainers latch
# at the notch ends, and the toggle's nut owns the band under the display.
hribs = ((ix0, rib_xs[0] + RIB_T / 2, rib_y_lo),
         (ix0, rib_xs[0] + RIB_T / 2, rib_y_hi))
for hx0, hx1, rby in hribs:
    plate = plate.fuse(Part.makeBox(hx1 - hx0, RIB_T, RIB_H,
                                    App.Vector(hx0, rby - RIB_T / 2,
                                               -RIB_H)))

hole_l = PLATE_T + RIM_D + 2
holes = []
holes.append(Part.makeCylinder(ESTOP_HOLE_D / 2, hole_l,
                               App.Vector(cx[0], ROW_CY, -RIM_D - 1)))
holes.append(Part.makeCylinder(TOG_HOLE_D / 2, hole_l,
                               App.Vector(cx[1], TOG_CY, -RIM_D - 1)))
# Rectangular cutouts; the mains gets its hole plus a narrower centered
# retainer notch poking equally past both ends.
def rect_cut(ccx, ccy, *rects):
    for bw, bh in rects:
        b = Part.makeBox(bw, bh, hole_l,
                         App.Vector(ccx - bw / 2, ccy - bh / 2,
                                    -RIM_D - 1))
        bc = [e for e in b.Edges
              if e.BoundBox.XLength < 0.01 and e.BoundBox.YLength < 0.01]
        holes.append(b.makeFillet(MAINS_R, bc))

rect_cut(cx[2], ROW_CY, (MAINS_W, MAINS_H), (MAINS_NOTCH_W, MAINS_NOTCH_L))
rect_cut(cx[1], DISP_CY, (DISP_W, DISP_H))

# Labels: centered under their switches — pockets in the face that take
# the separately printed colour inserts.
for (solid, w), c in zip(labels, cx):
    s = solid.copy()
    s.translate(App.Vector(c - w / 2 - s.BoundBox.XMin,
                           LABEL_Y - s.BoundBox.YMin,
                           PLATE_T - TEXT_DEPTH))
    holes.append(s)

for h in holes:
    plate = plate.cut(h)
plate = plate.removeSplitter()

obj = doc.addObject("Part::Feature", "switch_plate")
obj.Shape = plate
doc.recompute()

# ---- toggle retaining nut (the toggle ships without one) ----
# Measured thread length 8.3; the plate face eats PLATE_T of it, so the
# nut swallows the remaining 5.9 (+0.3 so it seats before bottoming).
# The lever exits through the middle, so this is a dress RING, not a cap:
# through-bore, undersized so the metal thread self-taps the plastic,
# domed donut top with a flared mouth for the bat's swing, fluted flanks
# for fingers. Print dome-up, no supports.
TOG_THREAD_L = 8.3
NUT_OD = 12.0
NUT_BORE_D = 5.4       # on the Ø6 thread: ~0.3/side bite — open it to
                       # 5.6 if threading on fights too hard
bore_depth = TOG_THREAD_L - PLATE_T + 0.3
dome_h = 3.0
dome_r = ((NUT_OD / 2) ** 2 + dome_h ** 2) / (2 * dome_h)  # cap geometry:
                       # sphere through the rim, tangent-continuous
nut = Part.makeCylinder(NUT_OD / 2, bore_depth)
nut = nut.fuse(Part.makeSphere(
    dome_r, App.Vector(0, 0, bore_depth + dome_h - dome_r)))
nut = nut.cut(Part.makeBox(4 * dome_r, 4 * dome_r, 4 * dome_r,
                           App.Vector(-2 * dome_r, -2 * dome_r,
                                      -4 * dome_r)))
for i in range(8):     # grip flutes, scalloped 1.2 deep into the flank
    a = i * math.pi / 4
    nut = nut.cut(Part.makeCylinder(
        1.2, bore_depth + dome_h + 2,
        App.Vector(NUT_OD / 2 * math.cos(a),
                   NUT_OD / 2 * math.sin(a), -1)))
# Through-bore for bushing and lever; the bushing tip lands 0.3 below the
# dome shoulder, and the bat is confined inside the bushing until there.
nut = nut.cut(Part.makeCylinder(NUT_BORE_D / 2, bore_depth + dome_h + 2,
                                App.Vector(0, 0, -1)))
# Thread lead-in at the base, flare at the mouth so the bat never rubs.
nut = nut.cut(Part.makeCone(3.4, NUT_BORE_D / 2, 1.2))
nut = nut.cut(Part.makeCone(NUT_BORE_D / 2, 5.0, 3.0,
                            App.Vector(0, 0, bore_depth + dome_h - 2.0)))
try:                   # elephant-foot relief; the scalloped rim can
    nut = nut.makeChamfer(0.5, [e for e in nut.Edges  # refuse — cosmetic
                                if e.BoundBox.ZMax < 0.05])
except Exception as ex:
    print("switch_plate: nut chamfer skipped (%s)" % ex)
nut.translate(App.Vector(PLATE_W + 20, 20, 0))
nobj = doc.addObject("Part::Feature", "toggle_nut")
nobj.Shape = nut.removeSplitter()
doc.recompute()

# ---- letter inserts: the pockets' positives, printed in a colour ----
# Same glyphs re-dilated with a SMALLER radius than the pockets — the
# difference is the fit clearance (~0.9 x INSERT_CLR per side), and it
# costs nothing extra, unlike a true erosion pass (which took minutes).
# Modeled sitting IN their pockets, flush with the face, so the print
# file carries them pre-aligned.
INSERT_CLR = 0.1       # nominal per-side clearance — tune on one letter
letters = None
for t, ((_, w), c) in zip(LABELS, zip(labels, cx)):
    sol, lw = text_shape(t, TEXT_BOLD - INSERT_CLR, TEXT_DEPTH)
    bb = sol.BoundBox
    sol.translate(App.Vector(c - lw / 2 - bb.XMin, LABEL_Y - bb.YMin,
                             PLATE_T - TEXT_DEPTH))
    letters = sol if letters is None else letters.fuse(sol)
lobj = doc.addObject("Part::Feature", "letters")
lobj.Shape = letters.removeSplitter()  # merge the fuse debris — without
                                       # this the mesh self-intersects
doc.recompute()

# ---- wood cutout template ----
# 1:1 tracing jig: the outer edge is the plate's footprint (for placing
# it on the panel), the inner edge is the through-opening with the
# 0.5/side slop already included — trace the inside, cut to the line.
# V-notches at every outer-edge midpoint mark the centrelines.
TPL_T = 1.2
tpl = _rounded_box(PLATE_W, PLATE_H, TPL_T, CORNER_R,
                   App.Vector(0, 0, 0))
tpl = tpl.cut(_rounded_box(rx1 - rx0 + 1.0, ry1 - ry0 + 1.0, TPL_T + 2,
                           CORNER_R, App.Vector(rx0 - 0.5, ry0 - 0.5,
                                                -1)))
for px, py, dx, dy in ((PLATE_W / 2, 0, 0, 1), (PLATE_W / 2, PLATE_H, 0, -1),
                       (0, PLATE_H / 2, 1, 0), (PLATE_W, PLATE_H / 2, -1, 0)):
    tri = Part.makePolygon([
        App.Vector(px - 3 * dy, py - 3 * dx, -1),
        App.Vector(px + 3 * dy, py + 3 * dx, -1),
        App.Vector(px + 3 * dx, py + 3 * dy, -1),
        App.Vector(px - 3 * dy, py - 3 * dx, -1)])
    tpl = tpl.cut(Part.Face(tri).extrude(App.Vector(0, 0, TPL_T + 2)))
tobj = doc.addObject("Part::Feature", "cutout_template")
tobj.Shape = tpl.removeSplitter()
doc.recompute()

here = os.path.dirname(os.path.abspath(__file__))
obj.Shape.exportStep(os.path.join(here, "switch_plate.step"))
Mesh.export([obj], os.path.join(here, "switch_plate.stl"))
Mesh.export([nobj], os.path.join(here, "switch_plate_nut.stl"))
Mesh.export([tobj], os.path.join(here, "switch_plate_template.stl"))
print("letters: %.1f tall, %.2f/side under the pockets" % (TEXT_DEPTH,
                                                           INSERT_CLR))
print("template: %.1f x %.1f frame, window %.1f x %.1f (slop included)"
      % (PLATE_W, PLATE_H, rx1 - rx0 + 1.0, ry1 - ry0 + 1.0))

# Combined print file: plate flipped face-down (rim and ribs up, pockets
# on the bed) with the letters flipped IDENTICALLY so they stay aligned
# in their pockets; nut dome-up where it already sits. All on Z=0 — drop
# on the plate, assign the letters their colour, slice.
pobj = doc.addObject("Part::Feature", "plate_print")
lpobj = doc.addObject("Part::Feature", "letters_print")
for o, src in ((pobj, plate), (lpobj, lobj.Shape)):
    s = src.copy()
    s.rotate(App.Vector(0, 0, 0), App.Vector(1, 0, 0), 180)
    s.translate(App.Vector(0, PLATE_H, PLATE_T))
    o.Shape = s
doc.recompute()
Mesh.export([pobj, nobj, lpobj],
            os.path.join(here, "switch_plate_print.3mf"))
Mesh.export([lpobj], os.path.join(here, "switch_plate_letters.stl"))
print("toggle nut: Ø%.0f x %.1f + %.0f dome, bore Ø%.1f x %.1f"
      % (NUT_OD, bore_depth, dome_h, NUT_BORE_D, bore_depth))
print("switch_plate: %.1f x %.1f x %.1f, centers %s, text %.1fmm caps"
      % (PLATE_W, PLATE_H, PLATE_T,
         ", ".join("%.1f" % c for c in cx), text_h))
print("wood: through-opening %.1f x %.1f (+0.5/side, corners R%.0f, tabs"
      " take up the slop); flush rabbet %.1f x %.1f x %.1f deep"
      % (rx1 - rx0, ry1 - ry0, CORNER_R,
         PLATE_W, PLATE_H, PLATE_T))
