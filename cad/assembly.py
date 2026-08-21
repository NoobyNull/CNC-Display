"""
Whole-stack assembly: panel, back cover, VESA plate, arm, base, table --
and every piece of hardware, really modelled: cover screws, VESA screws,
arm bolts and nylocs, bench screws on washers, the restart switch, a 20mm
fan seated in its pocket, and the link cable woven through the strain
relief with its PH2.0 plug.

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

HERE = os.path.dirname(os.path.abspath(__file__)) if "__file__" in dir() else "cad"

# Vendor model, gitignored (25MB, freely downloadable -- see cad/.gitignore).
# Loaded when present; without it you get the printed parts and hardware alone.
PANEL_STEP = os.path.join(HERE, "ESP32-S3-Touch-LCD-7B.stp")

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
# Turned 180 deg about its own axis so the bench runs away behind the screen
# rather than out in front of it. The plate itself is symmetric, so this only
# decides which way the bench slab is drawn.
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

# A slab of bench, so the pose reads. Its top face is the base plate's underside,
# and it runs back under the whole plate -- the four screws go straight down
# into it.
table = Part.makeBox(600, 500, TABLE_THK, App.Vector(-300, -70, -TABLE_THK))
add("Table", table, base_pl, WOOD)

# ---------------------------------------------------------------- hardware
# Positions come from back_cover.py's own constants (exec of everything above
# its `def box(`), so a moved hole moves its screw with it. Nothing below is a
# second copy of a number that already exists.
bc = {}
exec(compile(open(os.path.join(HERE, "back_cover.py")).read().split("\ndef box(")[0],
             "back_cover-constants", "exec"), bc)

hex_prism = g["hex_prism"]
STEEL_BRIGHT = (0.75, 0.76, 0.78)      # fasteners, distinct from printed grey
BLACK = (0.12, 0.12, 0.13)
ROT_Y = App.Rotation(App.Vector(1, 0, 0), 90)      # shank built down -Z -> +Y


def socket_cap(d, l, head_d, head_h, socket_af):
    """Socket head cap screw: under-head plane at the origin, shank down -Z."""
    s = Part.makeCylinder(d / 2.0, l, App.Vector(0, 0, -l))
    head = Part.makeCylinder(head_d / 2.0, head_h)
    depth = 0.6 * head_h
    head = head.cut(hex_prism(socket_af, depth + 0.5,
                              App.Vector(0, 0, head_h - depth)))
    return s.fuse(head)


def hex_bolt(d, l, af, head_h):
    s = Part.makeCylinder(d / 2.0, l, App.Vector(0, 0, -l))
    return s.fuse(hex_prism(af, head_h, App.Vector(0, 0, 0)))


def nyloc(af, hex_h, collar_d, collar_h):
    """M nyloc, top face at the origin, growing down -Z: hex body then collar."""
    n = hex_prism(af, hex_h, App.Vector(0, 0, 0), App.Vector(0, 0, -1))
    return n.fuse(Part.makeCylinder(collar_d / 2.0, collar_h,
                                    App.Vector(0, 0, -hex_h - collar_h)))


def washer(od, bore, t):
    return Part.makeCylinder(od / 2.0, t).cut(
        Part.makeCylinder(bore / 2.0, t + 2, App.Vector(0, 0, -1)))


# 4x M3x25 socket cap: heads flush in the rear-face counterbores, threads
# reaching the bracket's M3 holes. (True 25mm length -- the README's caveat
# about shallow bracket engagement is a real-world check, not hidden here.)
n = 0
for x in bc["M3_X"]:
    for z in bc["M3_Z"]:
        n += 1
        add("M3x25_%d" % n, socket_cap(3.0, 25.0, 5.5, 3.0, 2.5),
            App.Placement(App.Vector(x, bc["CAVITY_BACK_Y"], z), ROT_Y),
            STEEL_BRIGHT)

# 4x M4x12 socket cap: through the VESA plate's counterbores into the cover's
# tapped holes -- placed in the plate's own frame so they follow its pose.
pl_plate = App.Placement(mount_pt, rot_plate)
n = 0
for sx in (-1, 1):
    for sy in (-1, 1):
        n += 1
        local = App.Placement(App.Vector(sx * g["VESA_PITCH"] / 2.0,
                                         sy * g["VESA_PITCH"] / 2.0,
                                         g["VESA_T"] - g["M4_CBORE_H"]),
                              App.Rotation())
        add("M4x12_%d" % n, socket_cap(4.0, 12.0, 7.0, 4.0, 3.0),
            pl_plate.multiply(local), STEEL_BRIGHT)

# 4x M5x30 hex + nyloc clamping the arm halves onto the balls, seated in the
# hex pockets arm_mount.py already cuts into the outer faces.
pocket_floor = PRE + g["ARM_HALF_T"] - g["M5_HEX_D"]
n = 0
for cx in (-SPAN / 2.0, SPAN / 2.0):
    for by in (-g["BOLT_Y"], g["BOLT_Y"]):
        n += 1
        add("M5x30_%d" % n, hex_bolt(5.0, 30.0, 8.0, 3.5),
            arm_pl.multiply(App.Placement(App.Vector(cx, by, pocket_floor),
                                          App.Rotation())), STEEL_BRIGHT)
        add("M5nyloc_%d" % n, nyloc(8.0, 3.7, 6.8, 1.3),
            arm_pl.multiply(App.Placement(App.Vector(cx, by, -pocket_floor),
                                          App.Rotation())), STEEL_BRIGHT)

# 4x M6x30 hex on a washer, straight down through the base plate into the
# bench. 30mm keeps the tip inside the 25mm slab.
n = 0
for sx in (-1, 1):
    for sy in (-1, 1):
        n += 1
        lx, ly = sx * g["SCREW_X"], sy * g["SCREW_Y"]
        seat = g["BASE_T"] - g["SCREW_HEAD_H"]
        add("BenchWasher_%d" % n, washer(13.0, 6.4, 1.2),
            base_pl.multiply(App.Placement(App.Vector(lx, ly, seat),
                                           App.Rotation())), STEEL_BRIGHT)
        add("M6x30_%d" % n, hex_bolt(6.0, 30.0, 10.0, 4.0),
            base_pl.multiply(App.Placement(App.Vector(lx, ly, seat + 1.2),
                                           App.Rotation())), STEEL_BRIGHT)

# Restart switch: flush-face momentary in its rear-face recess, cap standing
# 0.5 below the outer surface, retaining nut inside, body reaching forward.
swx, swz = bc["SW_CX"], bc["SW_CZ"]
face_y = bc["BACK_Y"] + bc["SW_CBORE_T"]           # recess floor
sw = Part.makeCylinder(6.4, 1.0, App.Vector(swx, face_y - 1.0, swz),
                       App.Vector(0, 1, 0))         # flush cap in the recess
sw = sw.fuse(Part.makeCylinder(3.3, 4.0, App.Vector(swx, face_y, swz),
                               App.Vector(0, 1, 0)))            # threaded barrel
sw = sw.fuse(Part.makeCylinder(4.75, 7.5, App.Vector(swx, face_y + 4.0, swz),
                               App.Vector(0, 1, 0)))            # body
for dz in (-2.0, 2.0):                               # solder prongs, tips at 14mm
    sw = sw.fuse(Part.makeBox(0.8, 2.5, 2.0,
                              App.Vector(swx - 0.4, face_y + 11.5, swz + dz - 1.0)))
add("RestartSwitch", sw, App.Placement(), BLACK)
add("SwitchNut", hex_prism(10.0, 2.0, App.Vector(0, 0, 0)),
    App.Placement(App.Vector(swx, bc["CAVITY_BACK_Y"] + 2.0, swz), ROT_Y),
    STEEL_BRIGHT)

# 20mm fan seated in its pocket: frame against the grill floor, hub and five
# pitched blades. Held by the seat walls, exactly as the seat was designed.
fx, fz = bc["FAN_CX"], bc["FAN_CZ"]
fy0 = bc["BACK_Y"] + bc["FAN_FLOOR_T"]              # seat floor
frame = Part.makeBox(20.0, 10.0, 20.0, App.Vector(fx - 10.0, fy0, fz - 10.0))
frame = frame.cut(Part.makeCylinder(9.0, 12.0, App.Vector(fx, fy0 - 1.0, fz),
                                    App.Vector(0, 1, 0)))
add("FanFrame", frame, App.Placement(), BLACK)
rotor = Part.makeCylinder(4.5, 6.0, App.Vector(fx, fy0 + 2.0, fz),
                          App.Vector(0, 1, 0))
blade0 = Part.makeBox(4.6, 1.0, 5.5, App.Vector(4.2, -0.5, -2.75))
for k in range(5):
    b = blade0.copy()
    b.rotate(App.Vector(6.5, 0, 0), App.Vector(1, 0, 0), 35)    # pitch
    b.rotate(App.Vector(0, 0, 0), App.Vector(0, 1, 0), 72 * k)
    b.translate(App.Vector(fx, fy0 + 5.0, fz))
    rotor = rotor.fuse(b)
add("FanRotor", rotor, App.Placement(), (0.35, 0.35, 0.37))

# The hardwired link cable: tail dropping toward the arm, in through its
# entry, woven through the three strain-relief posts, off to the UART header
# on a PH2.0 plug. The weave wiggle is the jacket deforming into the pinch.
ex, ez = bc["CABLE_ENTRY_X"], bc["CABLE_ENTRY_Z"]
rx, rz = bc["RELIEF_X"], bc["RELIEF_Z"]
ymid = bc["CAVITY_BACK_Y"] + bc["RELIEF_H"] / 2.0
pts = [App.Vector(50.0, -46.0, -30.0),
       App.Vector(48.5, -36.0, -14.0),
       App.Vector(ex, -29.5, ez),
       App.Vector(ex - 5.0, ymid, ez),
       App.Vector(rx[0], ymid, ez + 1.2),
       App.Vector(rx[1], ymid, ez - 1.2),
       App.Vector(rx[2], ymid, ez + 1.2),
       App.Vector(10.0, -22.0, 2.0),
       App.Vector(45.0, -20.0, 4.5),
       App.Vector(72.0, -19.6, 4.5)]
sp = Part.BSplineCurve()
sp.interpolate(pts)
tangent = sp.tangent(sp.FirstParameter)[0]
prof = Part.Wire([Part.makeCircle(bc["CABLE_OD_MAX"] / 2.0, pts[0], tangent)])
add("LinkCable", Part.Wire([sp.toShape()]).makePipeShell([prof], True, False),
    App.Placement(), BLACK)
add("PH2Plug", Part.makeBox(8.0, 4.2, 4.5, App.Vector(70.0, -21.8, 2.25)),
    App.Placement(), (0.9, 0.9, 0.88))

if PANEL_STEP and os.path.exists(PANEL_STEP):
    # ImportGui, not Import: the headless importer silently drops the STEP's
    # colors and the whole panel comes in grey. Fall back when run headless.
    try:
        import ImportGui as _imp
    except ImportError:
        import Import as _imp
    _imp.insert(PANEL_STEP, doc.Name)

doc.recompute()
doc.saveAs(os.path.join(HERE, "assembly.FCStd"))
print("assembled; table normal in panel coords = %s" % (TABLE_NORMAL,))
