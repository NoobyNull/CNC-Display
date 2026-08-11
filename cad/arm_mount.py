"""
Adjustable table mount for the CNC console: ball-and-socket arm, VESA 75 head.

Run inside FreeCAD:  exec(open("cad/arm_mount.py").read())

Three printed parts, one of them twice:

    base_plate   screws flat to the bench, carries a ball
    arm_half     x2, identical -- two halves of a double socket arm
    vesa_plate   VESA 75 onto the panel's back cover, carries a ball

Hardware: 4x bench screws (1/4in lag or M6, heads sink flush),
          4x M5x30 + 4 M5 nyloc, 4x M4x12 into the back cover.

Why every angle here is what it is
----------------------------------
A sphere printed with its neck vertical has a nasty underside, so the neck
blends into the ball with a 45 deg cone -- the steepest overhang the part has
anywhere, and it needs no support. That cone eats the 45 deg of ball surface
nearest the neck, which is why the socket's opening cone is 75 deg: the joint's
usable tilt is (socket cone - print cone), so 75 - 45 = +/-30 deg.

Opening the socket to 75 deg leaves an aperture of 2*R*sin(75) = 1.93*R, still
narrower than the ball, so the ball cannot pull out through it even before the
bolts are touched.
"""

import FreeCAD as App
import Part
import math
import os

# --------------------------------------------------------------- ball & socket
BALL_R      = 10.0
NECK_R      = 5.5          # 11mm neck: the bending stress lives here, and it
                           # prints across the layers, which is the weak way
SOCKET_CLR  = 0.25
SOCKET_R    = BALL_R + SOCKET_CLR
PRINT_CONE  = 45.0         # deg from the ball axis -- also the max overhang
SOCKET_CONE = 75.0         # deg half-angle of the socket mouth
PRELOAD     = 1.0          # each cavity stops this far short of a hemisphere,
                           # so the halves start 2mm apart and can be squeezed

# ------------------------------------------------------------------------- arm
ARM_SPAN    = 110.0        # ball centre to ball centre
ARM_HALF_W  = 22.0
ARM_HALF_T  = 13.5
ARM_END_X   = 64.0
M5_CLR      = 5.4
M5_HEX_AF   = 8.4          # M5 nut across flats, +0.4 print clearance
M5_HEX_D    = 4.5
BOLT_Y      = 16.0

# Cable tray down the outer face. It stops well short of the socket cavities,
# and it sits on the arm's neutral axis for the bending that actually matters
# (the panel hangs in the plane containing the arm and gravity, which bends the
# arm about the bolt axis -- stress varies across the width, not the middle),
# so it costs almost nothing structurally.
#
# Two bridges are left spanning it so the loom is captive without a zip tie:
# thread it under them and it cannot flick out and catch on anything.
TRAY_R      = 6.0
TRAY_X      = 42.0
BRIDGE_X    = 25.0
BRIDGE_W    = 6.0
BRIDGE_T    = 2.0

# ------------------------------------------------------------------- table end
BASE_L, BASE_W, BASE_T = 110.0, 80.0, 10.0
BASE_BALL_H = 25.0         # neck length above the plate

# Four screws straight down into the bench. Sized for a 1/4in lag or an M6 --
# change SCREW_CLR/SCREW_HEAD_D for anything else. The counterbore is flat
# bottomed, so a hex head on a washer or a socket cap both bear properly; for
# flat-head screws cut a 90 deg countersink here instead.
SCREW_X, SCREW_Y = 42.0, 28.0
SCREW_CLR    = 7.0
SCREW_HEAD_D = 14.0
SCREW_HEAD_H = 4.0

# -------------------------------------------------------------------- vesa end
VESA_PITCH  = 75.0
VESA_PLATE  = 100.0
VESA_T      = 8.0
M4_CLR      = 4.5
M4_CBORE_D  = 7.6
M4_CBORE_H  = 4.0
VESA_BALL_H = 22.0


def hex_prism(af, height, pnt, direction=App.Vector(0, 0, 1)):
    """Hexagonal pocket, `af` across flats."""
    r = af / math.sqrt(3.0)                      # across corners / 2
    pts = [App.Vector(r * math.cos(math.radians(60 * i)),
                      r * math.sin(math.radians(60 * i)), 0) for i in range(6)]
    pts.append(pts[0])
    face = Part.Face(Part.Wire(Part.makePolygon(pts)))
    solid = face.extrude(App.Vector(0, 0, height))
    if direction.z < 0:
        solid.rotate(App.Vector(0, 0, 0), App.Vector(1, 0, 0), 180)
    solid.translate(pnt)
    return solid


def ball_on_stem(z_root, z_centre):
    """Neck + 45 deg blend + ball, axis along +Z, ball centred at z_centre."""
    rt = BALL_R / math.sqrt(2.0)                 # 45 deg tangency on the sphere
    z_tan = z_centre - rt
    z_neck_top = z_tan - (rt - NECK_R)           # cone rises 1:1, so dz = dr

    neck = Part.makeCylinder(NECK_R, z_neck_top - z_root,
                             App.Vector(0, 0, z_root))
    blend = Part.makeCone(NECK_R, rt, z_tan - z_neck_top,
                          App.Vector(0, 0, z_neck_top))
    ball = Part.makeSphere(BALL_R, App.Vector(0, 0, z_centre))
    return neck.fuse(blend).fuse(ball)


def socket_cavity(cx, sign):
    """Half a ball socket at x=cx, its mouth opening toward sign*X.

    The sphere sits PRELOAD below the mating plane (z=0) so the cavity is
    short of a hemisphere; two halves 2*PRELOAD apart then make a true sphere,
    and the bolts close that gap onto the ball.
    """
    sphere = Part.makeSphere(SOCKET_R, App.Vector(cx, 0, -PRELOAD))
    reach = 60.0
    mouth = Part.makeCone(0.0, reach * math.tan(math.radians(SOCKET_CONE)),
                          reach, App.Vector(cx, 0, -PRELOAD),
                          App.Vector(sign, 0, 0))
    return sphere.fuse(mouth)


def make_arm_half():
    body = Part.makeBox(2 * ARM_END_X, 2 * ARM_HALF_W, ARM_HALF_T,
                        App.Vector(-ARM_END_X, -ARM_HALF_W, 0))
    body = body.makeFillet(10.0, [e for e in body.Edges
                                  if abs(e.tangentAt(e.FirstParameter).z) > 0.99])
    for cx, sign in ((-ARM_SPAN / 2.0, -1), (ARM_SPAN / 2.0, 1)):
        body = body.cut(socket_cavity(cx, sign))
        for by in (-BOLT_Y, BOLT_Y):
            body = body.cut(Part.makeCylinder(M5_CLR / 2.0, ARM_HALF_T + 2,
                                              App.Vector(cx, by, -1)))
            # Nut or bolt head sinks into the outer face -- the part is
            # symmetric, so the same pocket serves either end of the screw.
            body = body.cut(hex_prism(M5_HEX_AF, M5_HEX_D + 1,
                                      App.Vector(cx, by, ARM_HALF_T - M5_HEX_D)))

    body = body.cut(Part.makeCylinder(TRAY_R, 2 * TRAY_X,
                                      App.Vector(-TRAY_X, 0, ARM_HALF_T),
                                      App.Vector(1, 0, 0)))
    for sx in (-1, 1):
        body = body.fuse(Part.makeBox(
            BRIDGE_W, 2 * TRAY_R, BRIDGE_T,
            App.Vector(sx * BRIDGE_X - BRIDGE_W / 2.0, -TRAY_R,
                       ARM_HALF_T - BRIDGE_T)))
    return body.removeSplitter()


def make_base_plate():
    body = Part.makeBox(BASE_L, BASE_W, BASE_T,
                        App.Vector(-BASE_L / 2.0, -BASE_W / 2.0, 0))
    body = body.makeFillet(8.0, [e for e in body.Edges
                                 if abs(e.tangentAt(e.FirstParameter).z) > 0.99])
    body = body.fuse(ball_on_stem(0.0, BASE_T + BASE_BALL_H))
    # Skirt at the neck root: the whole panel's moment lands on this joint.
    body = body.fuse(Part.makeCone(NECK_R + 7.0, NECK_R, 7.0,
                                   App.Vector(0, 0, BASE_T - 0.01)))
    for sx in (-1, 1):
        for sy in (-1, 1):
            p = App.Vector(sx * SCREW_X, sy * SCREW_Y, -1)
            body = body.cut(Part.makeCylinder(SCREW_CLR / 2.0, BASE_T + 2, p))
            body = body.cut(Part.makeCylinder(
                SCREW_HEAD_D / 2.0, SCREW_HEAD_H + 1,
                App.Vector(p.x, p.y, BASE_T - SCREW_HEAD_H)))
    return body.removeSplitter()


def make_vesa_plate():
    body = Part.makeBox(VESA_PLATE, VESA_PLATE, VESA_T,
                        App.Vector(-VESA_PLATE / 2.0, -VESA_PLATE / 2.0, 0))
    body = body.makeFillet(8.0, [e for e in body.Edges
                                 if abs(e.tangentAt(e.FirstParameter).z) > 0.99])
    body = body.fuse(ball_on_stem(0.0, VESA_T + VESA_BALL_H))
    body = body.fuse(Part.makeCone(NECK_R + 7.0, NECK_R, 7.0,
                                   App.Vector(0, 0, VESA_T - 0.01)))
    for sx in (-1, 1):
        for sy in (-1, 1):
            p = App.Vector(sx * VESA_PITCH / 2.0, sy * VESA_PITCH / 2.0, -1)
            body = body.cut(Part.makeCylinder(M4_CLR / 2.0, VESA_T + 2, p))
            body = body.cut(Part.makeCylinder(
                M4_CBORE_D / 2.0, M4_CBORE_H + 1,
                App.Vector(p.x, p.y, VESA_T - M4_CBORE_H)))
    return body.removeSplitter()


PARTS = [
    ("BasePlate",  make_base_plate,  App.Vector(0, 0, 0)),
    ("ArmHalf",    make_arm_half,    App.Vector(0, 130, 0)),
    ("VesaPlate",  make_vesa_plate,  App.Vector(190, 0, 0)),
]

doc_name = "WS7B_arm_mount"
if doc_name in App.listDocuments():
    App.closeDocument(doc_name)
doc = App.newDocument(doc_name)

here = os.path.dirname(os.path.abspath(__file__)) if "__file__" in dir() else "cad"
import Mesh
objs = []
for name, fn, offset in PARTS:
    shape = fn()
    o = doc.addObject("Part::Feature", name)
    o.Shape = shape
    o.Placement.Base = offset
    objs.append(o)
doc.recompute()

doc.saveAs(os.path.join(here, "arm_mount.FCStd"))
for o in objs:
    o.Shape.exportStep(os.path.join(here, "arm_%s.step" % o.Name.lower()))
    Mesh.export([o], os.path.join(here, "arm_%s.stl" % o.Name.lower()))
