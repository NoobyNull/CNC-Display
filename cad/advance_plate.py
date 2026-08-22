# Adapter plate: Elecrow CrowPanel Advance 7.0 (V1.4) rear mount -> VESA 75.
#
# The Advance is a cased display, so the 7B's full rear enclosure collapses to
# this one part. Display side: the shell's own centred 4 x M3 pattern,
# 160.0 x 74.0 mm, measured from Elecrow's official STEP
# (00-7_0-view-v2_12_10.stp), not from the wiki. Arm side: the same VESA 75
# boss interface back_cover.py had, so arm_vesaplate and the whole ball-joint
# arm bolt on unchanged.
#
# The plate does NOT sit flat on the shell: the wireless-module bay and the
# function-select DIP recess live under it, and an installed module would
# stand proud of the case. Four pads at the screws hold the body PAD_H off
# the shell instead.
# ponytail: 3mm pad standoff clears an EMPTY module bay; a fitted wireless
# module may be taller — measure and raise PAD_H if one is ever installed.
#
# Run from the FreeCAD Python console:  exec(open("cad/advance_plate.py").read())

import FreeCAD as App
import Part
import Mesh
import os

# ---- display side: the shell's mounting pattern (measured, do not "fix") ----
MOUNT_DX = 160.0          # hole-to-hole, X
MOUNT_DY = 74.0           # hole-to-hole, Y
M3_BORE_D  = 3.4
M3_CBORE_D = 6.2          # socket/button head sits in this, arm side
M3_CBORE_H = 3.0
PAD_D  = 10.0             # contact pads around the display screws
PAD_H  = 3.0

# ---- arm side: identical to back_cover.py so arm_vesaplate fits ----
VESA_PITCH  = 75.0
VESA_BOSS_D = 11.0
VESA_BOSS_H = 9.0
VESA_HOLE_D = 3.3         # M4 thread-forms into the plastic; 5.6 for heat-sets
VESA_HOLE_DEPTH = 11.0

# ---- the plate itself ----
PLATE_W = 172.0           # covers the mount pattern with ~6mm around each hole
PLATE_H = 86.0            # top edge stops short of the shell's connector strip
PLATE_T = 5.0             # counterbore leaves a 2mm web under the head
CORNER_R = 8.0

doc = App.newDocument("advance_plate")


def rounded_plate(w, h, t, r):
    box = Part.makeBox(w, h, t, App.Vector(-w / 2, -h / 2, 0))
    for sx in (-1, 1):
        for sy in (-1, 1):
            corner = Part.makeBox(r, r, t, App.Vector(sx * w / 2 - (r if sx > 0 else 0),
                                                      sy * h / 2 - (r if sy > 0 else 0), 0))
            keep = Part.makeCylinder(r, t, App.Vector(sx * (w / 2 - r), sy * (h / 2 - r), 0))
            box = box.cut(corner.cut(keep))
    return box


# Z=0 is the display-side face of the pads; the plate body floats PAD_H above.
plate = rounded_plate(PLATE_W, PLATE_H, PLATE_T, CORNER_R)
plate.translate(App.Vector(0, 0, PAD_H))

solids = [plate]
holes = []

# Display screws: pads down to the shell, counterbored bores all the way up.
for sx in (-1, 1):
    for sy in (-1, 1):
        x, y = sx * MOUNT_DX / 2, sy * MOUNT_DY / 2
        solids.append(Part.makeCylinder(PAD_D / 2, PAD_H, App.Vector(x, y, 0)))
        holes.append(Part.makeCylinder(M3_BORE_D / 2, PAD_H + PLATE_T, App.Vector(x, y, 0)))
        holes.append(Part.makeCylinder(M3_CBORE_D / 2, M3_CBORE_H,
                                       App.Vector(x, y, PAD_H + PLATE_T - M3_CBORE_H)))

# VESA bosses on the arm side, blind holes for the vesaplate's M4s.
for sx in (-1, 1):
    for sy in (-1, 1):
        x, y = sx * VESA_PITCH / 2, sy * VESA_PITCH / 2
        solids.append(Part.makeCylinder(VESA_BOSS_D / 2, VESA_BOSS_H,
                                        App.Vector(x, y, PAD_H + PLATE_T)))
        holes.append(Part.makeCylinder(VESA_HOLE_D / 2, VESA_HOLE_DEPTH,
                                       App.Vector(x, y, PAD_H + PLATE_T + VESA_BOSS_H - VESA_HOLE_DEPTH)))

shape = solids[0]
for s in solids[1:]:
    shape = shape.fuse(s)
for h in holes:
    shape = shape.cut(h)
shape = shape.removeSplitter()

obj = doc.addObject("Part::Feature", "advance_plate")
obj.Shape = shape
doc.recompute()

here = os.path.dirname(os.path.abspath(__file__))
obj.Shape.exportStep(os.path.join(here, "advance_plate.step"))
Mesh.export([obj], os.path.join(here, "advance_plate.stl"))
print("advance_plate: exported")
