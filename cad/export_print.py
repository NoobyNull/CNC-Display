"""
Export every printable part, already sitting in its print orientation.

Run inside FreeCAD:  exec(open("cad/export_print.py").read())

Writes cad/print/*.stl. Drop them straight onto the Bambu plate -- each one is
already the right way up and resting on Z=0, so no rotating in the slicer.
Filenames carry the quantity.

Meshed at 0.04mm linear / 0.2 deg angular deviation. That matters here: the ball
and its socket are a running fit with 0.25mm of clearance, and a coarse
tessellation would eat most of it.
"""

import FreeCAD as App
import Mesh
import MeshPart
import os

HERE = os.path.dirname(os.path.abspath(__file__)) if "__file__" in dir() else "cad"
OUT = os.path.join(HERE, "print")
if not os.path.isdir(OUT):
    os.mkdir(OUT)

for src in ("back_cover.py", "arm_mount.py"):
    p = os.path.join(HERE, src)
    exec(compile(open(p).read(), src, "exec"), {"__file__": p})

g = {"__file__": os.path.join(HERE, "arm_mount.py")}
exec(compile(open(g["__file__"]).read(), "arm_mount.py", "exec"), g)

LINEAR, ANGULAR = 0.04, 0.2


def oriented(doc, name, rot=None, spin=0.0):
    """Fetch a part and turn it into its printing pose (not yet seated)."""
    s = App.getDocument(doc).getObject(name).Shape.copy()
    s.Placement = App.Placement()
    if rot is not None:
        s.rotate(App.Vector(0, 0, 0), App.Vector(*rot), spin)
    return s


def seat(shape):
    """Mesh, then drop it onto Z=0 and centre it in XY.

    Seating is done on the MESH, never on the shape. Shape.BoundBox is the loose
    NURBS-pole box on anything curved -- on the arm half it reads ~9mm below the
    real underside, which is exactly how far off the bed the part ends up if you
    trust it.
    """
    m = MeshPart.meshFromShape(Shape=shape, LinearDeflection=LINEAR,
                               AngularDeflection=ANGULAR, Relative=False)
    bb = m.BoundBox
    m.translate(-(bb.XMin + bb.XMax) / 2.0, -(bb.YMin + bb.YMax) / 2.0, -bb.ZMin)
    return m


# Rear face down: every wall is then vertical and the columns and bosses stand
# up off the floor, so there is nothing to support.
cover = oriented("WS7B_back_cover", "BackCover", (1, 0, 0), 90)

# Socket face UP. The spherical cavities then open upward and print as bowls
# with no overhang. The cable tray ends up against the bed, where its half-round
# section closes over itself as an arch -- steep only at the very apex.
arm = oriented("WS7B_arm_mount", "ArmHalf", (1, 0, 0), 180)

# Both already model plate-down, ball-up.
base = oriented("WS7B_arm_mount", "BasePlate")
vesa = oriented("WS7B_arm_mount", "VesaPlate")

PARTS = [("back_cover_x1", cover), ("arm_half_x2", arm),
         ("base_plate_x1", base), ("vesa_plate_x1", vesa)]

doc = App.newDocument("WS7B_print")
report = []
for fname, shape in PARTS:
    m = seat(shape)
    o = doc.addObject("Mesh::Feature", fname)
    o.Mesh = m
    Mesh.export([o], os.path.join(OUT, fname + ".stl"))
    bb = m.BoundBox
    assert abs(bb.ZMin) < 1e-6, "%s is not sitting on the bed" % fname
    report.append("%-16s %6.1f x %6.1f x %6.1f mm  Zmin=%.3f  %6d facets  solid=%s" % (
        fname, bb.XLength, bb.YLength, bb.ZLength, bb.ZMin,
        m.CountFacets, m.isSolid()))

# One 3MF holding the lot, if this build can write them -- it keeps the parts
# together and carries units, which STL does not.
try:
    Mesh.export(doc.Objects, os.path.join(OUT, "cnc_console_all.3mf"))
    report.append("3mf: cnc_console_all.3mf")
except Exception as e:
    report.append("3mf export unavailable (%s) -- STLs only" % e)

App.closeDocument(doc.Name)
print("\n".join(report))
