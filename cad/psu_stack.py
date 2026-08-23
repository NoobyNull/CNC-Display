# Stacking mounts for two BOSYTRO ES-12V480W supplies (Mean Well LRS-class
# clones, 215 x 115 x 50): FOUR identical L-posts, one per side screw
# column. Each post's vertical leg carries two M3 clearance holes — lower
# unit and upper unit — holding the pair with an air gap so the lower fan
# exhausts into open space; the foot screws to the bench and a gusset each
# side braces it. Place each post wherever the PSU's screw columns actually
# are: the part never needs to know the hole positions along the case, only
# their HEIGHT above the case bottom (SIDE_HOLE_Z — measure it; if the two
# columns sit at different heights, this becomes two variants).
#
# Printing (Bambu P1S): leg face on the bed, foot and gussets rising, 45°
# everywhere it matters — no supports. Print FOUR. PSU screws M3 x 10
# button head (6 through the leg, ~4 into the case, clone-safe).
#
# Driver access is PROVEN: probe cylinders along every screw axis must
# clear the solid or the build fails loudly.
#
# Run from the FreeCAD Python console:  exec(open("cad/psu_stack.py").read())

import FreeCAD as App
import Part
import Mesh
import os

# ---- PSU ----
PSU_H = 50.0
SIDE_HOLE_Z = 12.5     # ponytail: PLACEHOLDER — measure the side hole
                       # height above the case bottom and replace.
SCREW_D = 3.4          # M3 clearance

# ---- post ----
POST_W  = 26.0
LEG_T   = 6.0
GAP     = 25.0         # air between the units
TOP_MARGIN = 10.0

# ---- foot ----
FOOT_W      = 20.0     # reach outward from the leg face
FOOT_T      = 5.0
FOOT_HOLE_D = 4.5      # #8 / M4 screw into the bench
GUSSET_LEG  = 12.0
GUSSET_T    = 5.0

POST_H = PSU_H + GAP + SIDE_HOLE_Z + TOP_MARGIN
HOLE_Z = (SIDE_HOLE_Z, PSU_H + GAP + SIDE_HOLE_Z)

doc = App.newDocument("psu_stack")

post = Part.makeBox(POST_W, LEG_T, POST_H)

# Foot with a centred bench hole; gussets at the outer edges so the driver
# path down the middle stays open.
post = post.fuse(Part.makeBox(POST_W, FOOT_W, FOOT_T, App.Vector(0, LEG_T, 0)))
post = post.cut(Part.makeCylinder(FOOT_HOLE_D / 2, FOOT_T + 2,
                                  App.Vector(POST_W / 2, LEG_T + FOOT_W / 2, -1)))
for gx in (0.0, POST_W - GUSSET_T):
    tri = Part.makePolygon([
        App.Vector(gx, LEG_T, FOOT_T),
        App.Vector(gx, LEG_T + GUSSET_LEG, FOOT_T),
        App.Vector(gx, LEG_T, FOOT_T + GUSSET_LEG),
        App.Vector(gx, LEG_T, FOOT_T)])
    post = post.fuse(Part.Face(tri).extrude(App.Vector(GUSSET_T, 0, 0)))

# PSU screw holes, centred in the leg.
for hz in HOLE_Z:
    post = post.cut(Part.makeCylinder(SCREW_D / 2, LEG_T + 2,
                                      App.Vector(POST_W / 2, -1, hz),
                                      App.Vector(0, 1, 0)))

# Rounded top corners.
oc = [e for e in post.Edges
      if e.BoundBox.XLength < 0.01 and e.BoundBox.ZLength < 0.01
      and e.BoundBox.ZMin > POST_H - 0.05]
try:
    post = post.makeFillet(4.0, oc)
except Exception as ex:
    print("psu_stack: corner fillet skipped (%s)" % ex)
post = post.removeSplitter()

# PROVE the driver paths.
probe = Part.makeCylinder(5.5, 40.0,
                          App.Vector(POST_W / 2, LEG_T + FOOT_W / 2, FOOT_T + 0.1))
v = post.common(probe).Volume
assert v < 0.001, "foot screw obstructed (%.1fmm3)" % v
for hz in HOLE_Z:
    probe = Part.makeCylinder(4.5, 40.0,
                              App.Vector(POST_W / 2, LEG_T + 0.1, hz),
                              App.Vector(0, 1, 0))
    v = post.common(probe).Volume
    assert v < 0.001, "PSU screw at z=%.1f obstructed (%.1fmm3)" % (hz, v)

obj = doc.addObject("Part::Feature", "psu_post")
obj.Shape = post
doc.recompute()

here = os.path.dirname(os.path.abspath(__file__))
obj.Shape.exportStep(os.path.join(here, "psu_post.step"))
Mesh.export([obj], os.path.join(here, "psu_post.stl"))
print("psu_post: %.0f x %.0f x %.0f — print FOUR" % (POST_W, LEG_T, POST_H))
