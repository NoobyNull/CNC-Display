"""
Whole-stack assembly: panel, back cover, VESA plate, arm, base, table.

Run inside FreeCAD:  exec(open("cad/assembly.py").read())

Everything is posed in PANEL coordinates -- the frame the vendor STEP already
uses (+X right, +Z up, +Y out of the screen) -- because the panel is the one
thing already correctly placed. The table ends up tilted in this frame; that is
expected, and `TABLE_NORMAL` below is world "up" expressed in panel coordinates.

The pose is not decorative. Both ball joints are set to 30 deg, inside the
+/-40 deg the sweep in arm_mount.py measured, and the resulting screen angle
falls out of that rather than being chosen: 30 deg at the base plus 30 deg at
the head puts the screen 30 deg above horizontal.

Set PANEL_STEP to the vendor model to see the board inside; without it you get
the printed parts alone.
"""

import FreeCAD as App
import Part
import math
import os

PANEL_STEP = None          # e.g. "/path/to/ESP32-S3-Touch-LCD-7B.stp"

JOINT_DEG   = 30.0         # both balls, well inside the measured +/-40
TABLE_THK   = 25.0

COVER_BACK_Y = -31.0       # outer face of the back cover, in panel coords
VESA_CZ      = 1.02        # the VESA pattern is centred on the glass, not the cover


def basis(xc, yc, zc):
    return App.Rotation(App.Matrix(xc.x, yc.x, zc.x, 0,
                                   xc.y, yc.y, zc.y, 0,
                                   xc.z, yc.z, zc.z, 0,
                                   0, 0, 0, 1))


def load(doc_name, obj_name):
    d = App.getDocument(doc_name)
    s = d.getObject(obj_name).Shape.copy()
    s.Placement = App.Placement()          # undo the parts-layout offset
    return s


mount = "WS7B_arm_mount"
for name, path in ((mount, "arm_mount.py"), ("WS7B_back_cover", "back_cover.py")):
    if name not in App.listDocuments():
        here = os.path.dirname(os.path.abspath(__file__)) if "__file__" in dir() else "cad"
        p = os.path.join(here, path)
        exec(compile(open(p).read(), path, "exec"), {"__file__": p})

g = {}
exec(compile(open(os.path.join(
    os.path.dirname(os.path.abspath(__file__)) if "__file__" in dir() else "cad",
    "arm_mount.py")).read(), "arm_mount.py", "exec"), g)
SPAN, PRE = g["ARM_SPAN"], g["PRELOAD"]
BALL_UP   = g["VESA_T"] + g["VESA_BALL_H"]      # plate face to ball centre
BASE_UP   = g["BASE_T"] + g["BASE_BALL_H"]

c, s = math.cos(math.radians(JOINT_DEG)), math.sin(math.radians(JOINT_DEG))

# --- head joint -------------------------------------------------------------
# The VESA plate lies on the cover's back face, ball centre BALL_UP behind it.
mount_pt = App.Vector(0, COVER_BACK_Y, VESA_CZ)
plate_z  = App.Vector(0, -1, 0)                          # plate grows rearward
rot_plate = basis(App.Vector(1, 0, 0), App.Vector(0, 0, 1), plate_z)
ball_head = mount_pt + plate_z * BALL_UP

# --- arm --------------------------------------------------------------------
# Its axis leaves the head ball JOINT_DEG off the neck, tipping downward.
u = App.Vector(0, c, s)
ball_base = ball_head - u * SPAN
rot_arm = basis(u, App.Vector(0, -s, c), App.Vector(1, 0, 0))

# --- base joint -------------------------------------------------------------
# Another JOINT_DEG, so the base neck stands JOINT_DEG off the arm.
neck_dir = App.Vector(0, -s, -c)                         # ball -> base plate
base_z = neck_dir * -1
# Turned 180 deg about its own axis so the clamped edge is the one you stand at
# and the table runs away behind the screen. The other way round works
# mechanically and puts the display facing across the bench, away from you.
rot_base = basis(App.Vector(-1, 0, 0), App.Vector(0, -c, s), base_z)

TABLE_NORMAL = base_z                                    # world up, in panel coords

doc_name = "WS7B_assembly"
if doc_name in App.listDocuments():
    App.closeDocument(doc_name)
doc = App.newDocument(doc_name)


def add(name, shape, placement, colour):
    o = doc.addObject("Part::Feature", name)
    o.Shape = shape
    o.Placement = placement
    o.ViewObject.ShapeColor = colour
    return o


GREY, STEEL, WOOD = (0.26, 0.28, 0.31), (0.55, 0.57, 0.60), (0.72, 0.60, 0.44)

add("BackCover", load("WS7B_back_cover", "BackCover"), App.Placement(), GREY)
add("VesaPlate", load(mount, "VesaPlate"),
    App.Placement(mount_pt, rot_plate), GREY)

half = load(mount, "ArmHalf")
arm_pl = App.Placement(ball_base - rot_arm.multVec(App.Vector(-SPAN / 2.0, 0, 0)), rot_arm)
# Compose, do not translate the shapes: Shape.translate() writes the shape's
# own Placement, which assigning o.Placement would then throw away -- both
# halves land in the same place and every socket ends up 1mm off its ball.
half_a = App.Placement(App.Vector(0, 0, PRE), App.Rotation())
half_b = App.Placement(App.Vector(0, 0, -PRE), App.Rotation(App.Vector(1, 0, 0), 180))
add("ArmHalfA", half, arm_pl.multiply(half_a), GREY)
add("ArmHalfB", half.copy(), arm_pl.multiply(half_b), GREY)

base_pl = App.Placement(ball_base - rot_base.multVec(App.Vector(0, 0, BASE_UP)), rot_base)
add("BasePlate", load(mount, "BasePlate"), base_pl, GREY)

bar_pl = base_pl.multiply(App.Placement(App.Vector(0, -19, -TABLE_THK - g["BAR_T"]),
                                        App.Rotation()))
add("ClampBar", load(mount, "ClampBar"), bar_pl, GREY)

# A slab of table, so the pose reads. Its top face is the base plate's underside.
table = Part.makeBox(600, 400, TABLE_THK, App.Vector(-300, -25, -TABLE_THK))
add("Table", table, base_pl, WOOD)

if PANEL_STEP and os.path.exists(PANEL_STEP):
    import Import
    Import.insert(PANEL_STEP, doc.Name)

doc.recompute()
print("assembled; table normal in panel coords = %s" % (TABLE_NORMAL,))
