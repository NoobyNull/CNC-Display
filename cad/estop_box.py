# E-stop + power box: operator pod for the desk edge, controls only.
#
# The wiring plan (one master switch powers the display first, then a relay
# closes the machine's contactor) keeps the relay and contactor elsewhere —
# this box carries a 22mm mushroom E-stop on the face and the PRONIC bracket
# paddle toggle (DPST 15A/125V) mounted in the FLOOR, paddle hanging down
# through a slot, guarded by a hinged flip safety cap. It hangs UNDER the
# desk edge; the desk's apron rail hides the face's blank top band
# (RAIL_DROP).
#
# TOGGLE, floor-mount (v2 after the slide-in design failed on the bench —
# the paddle's root shield is ~2mm wider than any slot it must pass, so the
# switch can never thread through from behind): the switch simply SITS on
# the floor, placed through the open back. Tabs flat on the floor's inside,
# paddle down through the slot (the shield stays inside, wider than the
# slot, resting above it), two M3/M3.5 machine screws up from below through
# the Ø3.82 tab holes, nuts dropped on from the open back. Nothing to
# thread, twist, or slide.
#
# SAFETY CAP: a printed tray hinged on two lugs behind the paddle slot,
# swinging down/back on M3 pin screws. Closed it shrouds the paddle from
# stray knees; a nub on its front lip clicks over a mating nub on the floor
# (tune CATCH_ENGAGE on the printed part if the click is too stiff/loose).
#
# Print orientation: box FACE DOWN as before (holes land on the bed; the
# hinge lugs and catch nub point along the bed plane). Cap prints as an
# open tray, opening up. No supports on either part.
#
# Run from the FreeCAD Python console:  exec(open("cad/estop_box.py").read())

import FreeCAD as App
import Part
import Mesh
import os

# ---- panel hardware ----
ESTOP_HOLE_D = 22.3    # standard 22mm pushbutton cutout, +0.3 print slop
ESTOP_HEAD_D = 40.0    # mushroom head, sets that zone's clearance
ESTOP_BODY_L = 45.0    # behind-panel depth incl. contact block

# Pronic paddle toggle, measured (2026-08-23), letters from the 3-view:
TOG_H = 41.3           # tab hole c-c (listing, 1-5/8"); D=0: pivot at centre
TOG_C = 16.75          # paddle width
TOG_E = 25.0           # pivot to paddle tip
TOG_J = 10.37          # tab plane to plate face
TOG_K = 38.1           # tab plane to body rear — standing height on the floor
TOG_B = 27.75          # pivot plate width — sets the toggle zone width
TOG_G = 48.3           # overall tab-tip span
TOG_SCREW_D = 4.0      # floor clearance holes; M3/M3.5 screws + nuts

# ---- box ----
WALL_T   = 3.0
FACE_T   = 3.0
BASE_T   = 3.0
MARGIN   = 5.0
WIRE_ROOM = 15.0
RAIL_DROP = 38.0       # desk apron rail depth; set to the real apron

# Floor slot: ends between the screws, 2mm web to each hole — the paddle's
# sweep at the floor plane cannot need more (pivot sits ~J above it).
SLOT_W     = TOG_C + 3.0
SLOT_REACH = TOG_H / 2 - TOG_SCREW_D / 2 - 2.0
TOG_PIV_Y  = 35.0      # pivot's y (depth) position on the floor

# ---- back panel ----
# Separately printed plug for the back opening: the body fits snugly INSIDE
# the box; a small lip runs the full OUTER circumference and seats on the
# box's rear edges, so the panel cannot be pushed inward — it stiffens the
# open back and closes it against dust. Four screws through the walls bite
# its edges. Wires leave through the bottom slot.
BACK_T   = 6.0
BACK_CLR = 0.2         # per side; snug plug fit — the accuracy knob
BACK_LIP = 3.0         # proud rim on the outer perimeter
BACK_SCREW_Z = (25.0, 65.0)
BACK_PILOT_D = 2.9     # in the panel edges; walls get 3.4 clearance
WIRE_W, WIRE_H = 20.0, 12.0

# ---- safety cap ----
CAP_WALL   = 2.5
CAP_CLEAR_D = 17.0     # interior depth below the floor; paddle pokes ~11
CAP_IN_W   = SLOT_W + 12.0
LUG_T      = 6.0       # each hinge lug's width (x)
LUG_DROP   = 11.0      # how far a lug hangs below the floor
HINGE_D    = 3.2       # M3 pin screws
CATCH_ENGAGE = 1.0     # nub interference; the tuning knob for the click

# Everything below derives.
ESTOP_CZ = BASE_T + MARGIN + ESTOP_HEAD_D / 2
CONTROL_TOP = ESTOP_CZ + ESTOP_HEAD_D / 2 + MARGIN
BOX_H = CONTROL_TOP + RAIL_DROP
TOG_ZONE = TOG_B + 2 * MARGIN
BOX_W = 2 * WALL_T + ESTOP_HEAD_D + 3 * MARGIN + TOG_ZONE
BOX_D = FACE_T + max(ESTOP_BODY_L, TOG_G + 8.0) + WIRE_ROOM

ESTOP_CX = WALL_T + MARGIN + ESTOP_HEAD_D / 2
TOG_CX   = BOX_W - WALL_T - MARGIN - TOG_ZONE / 2

slot_y0 = TOG_PIV_Y - SLOT_REACH
slot_y1 = TOG_PIV_Y + SLOT_REACH

doc = App.newDocument("estop_box")

solids = []
holes = []

# Face; y=0 is the operator side.
solids.append(Part.makeBox(BOX_W, FACE_T, BOX_H, App.Vector(0, 0, 0)))

# Floor and the two side walls; back and top stay open.
solids.append(Part.makeBox(BOX_W, BOX_D, BASE_T, App.Vector(0, 0, 0)))
for x0 in (0.0, BOX_W - WALL_T):
    solids.append(Part.makeBox(WALL_T, BOX_D, BOX_H, App.Vector(x0, 0, 0)))

# 45° rib along the face-to-floor joint.
rib = Part.makePolygon([
    App.Vector(WALL_T, FACE_T, BASE_T),
    App.Vector(WALL_T, FACE_T + 5.0, BASE_T),
    App.Vector(WALL_T, FACE_T, BASE_T + 5.0),
    App.Vector(WALL_T, FACE_T, BASE_T)])
solids.append(Part.Face(rib).extrude(App.Vector(BOX_W - 2 * WALL_T, 0, 0)))

# E-stop: 22mm hole in the face, bottom quarter. Its clamp ring holds it.
holes.append(Part.makeCylinder(ESTOP_HOLE_D / 2, FACE_T + 1,
                               App.Vector(ESTOP_CX, -1, ESTOP_CZ),
                               App.Vector(0, 1, 0)))

# Paddle slot in the floor, corners rounded on the cutter.
slot = Part.makeBox(SLOT_W, 2 * SLOT_REACH, BASE_T + 2,
                    App.Vector(TOG_CX - SLOT_W / 2, slot_y0, -1))
sc = [e for e in slot.Edges
      if e.BoundBox.XLength < 0.01 and e.BoundBox.YLength < 0.01]
holes.append(slot.makeFillet(2.5, sc))

# Toggle screw holes through the floor at the tab spacing.
for dy in (-TOG_H / 2, TOG_H / 2):
    holes.append(Part.makeCylinder(TOG_SCREW_D / 2, BASE_T + 2,
                                   App.Vector(TOG_CX, TOG_PIV_Y + dy, -1)))

# Hinge lugs for the safety cap, behind the slot, hanging below the floor —
# fully OUTSIDE the cap's width so the closed cap nests between them.
cap_ow = CAP_IN_W + 2 * CAP_WALL
cx0 = TOG_CX - cap_ow / 2
lug_y = slot_y1 + 8.0
lug_xs = (cx0 - LUG_T - 0.4, cx0 + cap_ow + 0.4)
pin_drop = LUG_DROP - 4.0          # pin axis depth below the floor
for lx in lug_xs:
    solids.append(Part.makeBox(LUG_T, 8.0, pin_drop,
                               App.Vector(lx, lug_y, -pin_drop)))
    # Rounded knuckle about the pin axis instead of a square block end.
    solids.append(Part.makeCylinder(4.5, LUG_T,
                                    App.Vector(lx, lug_y + 4.0, -pin_drop),
                                    App.Vector(1, 0, 0)))
for lx in lug_xs:
    holes.append(Part.makeCylinder(HINGE_D / 2, LUG_T + 2,
                                   App.Vector(lx - 1, lug_y + 4.0, -pin_drop),
                                   App.Vector(1, 0, 0)))

# Catch nub on the floor's underside at the cap's front lip line.
catch_y = slot_y0 - 12.0
solids.append(Part.makeBox(10.0, 2.0, 1.5,
                           App.Vector(TOG_CX - 5.0, catch_y, -1.5)))

# Wall clearance holes for the back-panel screws, at plug mid-thickness.
for x0 in (-1.0, BOX_W - WALL_T - 1.0):
    for hz in BACK_SCREW_Z:
        holes.append(Part.makeCylinder(3.4 / 2, WALL_T + 2,
                                       App.Vector(x0, BOX_D - 3.0, hz),
                                       App.Vector(1, 0, 0)))

shape = solids[0]
for s in solids[1:]:
    shape = shape.fuse(s)
for h in holes:
    shape = shape.cut(h)
shape = shape.removeSplitter()


def try_round(shp, edges, r, what):
    if not edges:
        return shp
    try:
        out = shp.makeFillet(r, edges)
        if out.isValid():
            return out
        print("estop_box: fillet on %s invalid, skipped" % what)
    except Exception as ex:
        print("estop_box: fillet on %s failed (%s), skipped" % (what, ex))
    return shp


def _vert(e):
    bb = e.BoundBox
    return bb.XLength < 0.01 and bb.YLength < 0.01 and bb.ZLength > 5


# Fillets are stress relief, not decoration: concave inside corners only.
int_vert = [e for e in shape.Edges if _vert(e)
            and (abs(e.BoundBox.XMin - WALL_T) < 0.05
                 or abs(e.BoundBox.XMax - (BOX_W - WALL_T)) < 0.05)
            and abs(e.BoundBox.YMin - FACE_T) < 0.15]
shape = try_round(shape, int_vert, 2.0, "face-wall inner corners")
int_base = [e for e in shape.Edges
            if e.BoundBox.XLength < 0.01 and e.BoundBox.ZLength < 0.01
            and e.BoundBox.YLength > 10
            and abs(e.BoundBox.ZMin - BASE_T) < 0.05
            and (abs(e.BoundBox.XMin - WALL_T) < 0.05
                 or abs(e.BoundBox.XMax - (BOX_W - WALL_T)) < 0.05)]
shape = try_round(shape, int_base, 2.0, "wall-floor inner corners")
shape = shape.removeSplitter()

obj = doc.addObject("Part::Feature", "estop_box")
obj.Shape = shape
doc.recompute()

# ---- safety cap: open tray; its side walls extend up into hinge ears -------
cap_front = catch_y - CAP_WALL
cap_len = (lug_y - cap_front) + 12.0
cap = Part.makeBox(cap_ow, cap_len, CAP_CLEAR_D, App.Vector(cx0, cap_front, 0))
cap = cap.cut(Part.makeBox(CAP_IN_W, cap_len - 2 * CAP_WALL,
                           CAP_CLEAR_D - CAP_WALL,
                           App.Vector(cx0 + CAP_WALL, cap_front + CAP_WALL,
                                      CAP_WALL)))
# Ears: rear-corner extensions of the side walls with rounded knuckle tops.
# Pin sits pin_drop BELOW the cap's rim, so closed (rim against the floor)
# the ear holes land exactly on the lug holes.
pin_z = CAP_CLEAR_D - (LUG_DROP - 4.0)
for ex in (cx0, cx0 + cap_ow - LUG_T):
    cap = cap.fuse(Part.makeBox(LUG_T, 12.0, pin_z,
                                App.Vector(ex, lug_y - 2.0, 0)))
    cap = cap.fuse(Part.makeCylinder(4.5, LUG_T,
                                     App.Vector(ex, lug_y + 4.0, pin_z),
                                     App.Vector(1, 0, 0)))
cap = cap.cut(Part.makeCylinder(HINGE_D / 2, cap_ow + 2,
                                App.Vector(cx0 - 1, lug_y + 4.0, pin_z),
                                App.Vector(1, 0, 0)))
# Catch notch in the front lip: the floor nub clicks into it.
cap = cap.cut(Part.makeBox(10.0 + 0.6, CAP_WALL + 2, CATCH_ENGAGE + 2.0,
                           App.Vector(TOG_CX - 5.3, cap_front - 1,
                                      CAP_CLEAR_D - CATCH_ENGAGE - 0.5)))
cobj = doc.addObject("Part::Feature", "estop_cap")
cobj.Shape = cap.removeSplitter()
doc.recompute()

# ---- back panel (modeled in place; prints flat, lip on the bed) -----------
bp_x0, bp_x1 = WALL_T + BACK_CLR, BOX_W - WALL_T - BACK_CLR
bp_z0 = BASE_T + BACK_CLR
back = Part.makeBox(bp_x1 - bp_x0, BACK_T, BOX_H - 0.2 - bp_z0,
                    App.Vector(bp_x0, BOX_D - BACK_T, bp_z0))
back = back.fuse(Part.makeBox(BOX_W, BACK_LIP, BOX_H,
                              App.Vector(0, BOX_D, 0)))
# Wire slot, open at the bottom, corners rounded on the cutter.
ws = Part.makeBox(WIRE_W, BACK_T + BACK_LIP + 2, WIRE_H + 1,
                  App.Vector(BOX_W / 2 - WIRE_W / 2, BOX_D - BACK_T - 1, -1))
wse = [e for e in ws.Edges
       if e.BoundBox.XLength < 0.01 and e.BoundBox.ZLength < 0.01
       and e.BoundBox.ZMin > 0]
back = back.cut(ws.makeFillet(3.0, wse))
# Edge pilots matching the wall clearance holes.
for sx, x0 in ((1, bp_x0 - 1.0), (-1, bp_x1 + 1.0)):
    for hz in BACK_SCREW_Z:
        back = back.cut(Part.makeCylinder(BACK_PILOT_D / 2, 13.0,
                                          App.Vector(x0, BOX_D - 3.0, hz),
                                          App.Vector(sx, 0, 0)))
bobj = doc.addObject("Part::Feature", "estop_back")
bobj.Shape = back.removeSplitter()
doc.recompute()

here = os.path.dirname(os.path.abspath(__file__))
Mesh.export([bobj], os.path.join(here, "estop_back.stl"))
obj.Shape.exportStep(os.path.join(here, "estop_box.step"))
Mesh.export([obj], os.path.join(here, "estop_box.stl"))
Mesh.export([cobj], os.path.join(here, "estop_cap.stl"))
print("estop_box: %.1f x %.1f x %.1f; cap %.0f x %.0f x %.0f"
      % (BOX_W, BOX_D, BOX_H, cap_ow, cap_len, CAP_CLEAR_D + LUG_DROP))
