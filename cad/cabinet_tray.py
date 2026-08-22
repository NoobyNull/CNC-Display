# Open-frame chassis for the Corgi (FluidNC) + Airedale, cabinet-mounted.
#
# v2, after the walled tray failed review: walls blocked screwdriver access to
# every terminal row and left RJ12 plugs no insertion or bend room. This box
# lives INSIDE a cabinet — the enclosure problem is chips and dust from above,
# not the sides. So: a base plate with standoffs, six posts, and a vented roof
# lid. Every edge is open; every connector faces free air.
#
# Layout (interior coords, mm) — v4, after v3 buried the Corgi's USB-C (it is
# on the Corgi's WEST short edge, which v3 faced into the bay):
#   west   x 5..130    Corgi: USB-C edge open west, terminal rows open
#                      north/south, RJ12/SD edge facing the gap
#   gap    x 130..150  20mm: Corgi plug insertion + the Airedale's terminal
#                      row, serviced from the open top
#   east   x 150..210  Airedale portrait, RJ12 jacks facing OPEN EAST
# The 8" interconnect leaves the Corgi's east jack, dips around the
# Airedale's south side in open air, and rises to its east-facing jacks —
# the route Matthew sketched.
#
# Measured, not assumed:
#   Corgi:    holes at (3.3,3.25)/(121.9,81.75) from its corner (corgi_1p1.stl)
#   Airedale: 59.98 x 85.97, holes 28 x 76 pattern (Airedale+V1.1.stl),
#             jacks overhang one long edge ~2.2mm
#
# Run from the FreeCAD Python console:  exec(open("cad/cabinet_tray.py").read())

import FreeCAD as App
import Part
import Mesh
import os

# ---- boards (measured) ----
CORGI_W, CORGI_H = 124.97, 84.97
CORGI_HOLES = [(3.3, 3.25), (121.9, 3.25), (3.3, 81.75), (121.9, 81.75)]
AIRE_LONG, AIRE_SHORT = 85.97, 59.98
AIRE_DL, AIRE_DS = 76.0, 28.0            # hole pattern along the long/short axes

# ---- chassis ----
MARGIN   = 5.0
RING     = 10.0                           # base ring outside the interior; posts + mounting live here
BASE_T   = 4.0
POST_W   = 10.0
POST_H   = 28.0                           # above base top; tallest part is ~24
POST_PILOT = 2.7                          # M3 thread-forms for the lid screws
STAND_H  = 4.0
STAND_D  = 7.0
PILOT_D  = 2.7
MOUNT_HOLE = 4.5
LID_T    = 3.0
LID_SCREW_D = 3.4

GAP = 20.0                                                 # plug room + terminal lane
INT_W = MARGIN + CORGI_W + GAP + AIRE_SHORT + MARGIN       # 214.95
INT_H = MARGIN + AIRE_LONG + MARGIN                        # 95.97
CORGI_X0 = MARGIN

doc = App.newDocument("cabinet_tray")


def box(w, h, t, x, y, z):
    return Part.makeBox(w, h, t, App.Vector(x, y, z))


# Strain-relief fillets: every vertical edge at the plan-view corners gets a
# radius. Sharp printed corners are stress risers and the first place a
# dropped or pried part cracks; the corner posts share these edges, so the
# post-to-plate corner is rounded its full height in one go.
CORNER_R = 5.0

def _is_vert_line(e):
    if len(e.Vertexes) < 2 or e.Length < 1.0:
        return False
    v0, v1 = e.Vertexes[0].Point, e.Vertexes[-1].Point
    return abs(v0.x - v1.x) < 1e-6 and abs(v0.y - v1.y) < 1e-6


def fillet_corners(shape, r=CORNER_R):
    bb = shape.BoundBox
    eps = 0.5
    edges = []
    for e in shape.Edges:
        if not _is_vert_line(e):
            continue
        v0 = e.Vertexes[0].Point
        at_x = abs(v0.x - bb.XMin) < eps or abs(v0.x - bb.XMax) < eps
        at_y = abs(v0.y - bb.YMin) < eps or abs(v0.y - bb.YMax) < eps
        if at_x and at_y:
            edges.append(e)
    return shape.makeFillet(r, edges) if edges else shape


def _in_post(p, posts, slack):
    for (px, py) in posts:
        if px - slack <= p.x <= px + POST_W + slack and py - slack <= p.y <= py + POST_W + slack:
            return True
    return False


def fillet_post_verticals(shape, posts, r=3.0):
    # every remaining sharp vertical post edge; the plan-corner ones are
    # already round and their tangent seams must not be re-filleted
    bb = shape.BoundBox
    edges = []
    for e in shape.Edges:
        if not _is_vert_line(e) or e.Length < 15.0:
            continue
        p = e.Vertexes[0].Point
        near_corner = any(abs(p.x - cx) < CORNER_R + 0.6 and abs(p.y - cy) < CORNER_R + 0.6
                          for cx in (bb.XMin, bb.XMax) for cy in (bb.YMin, bb.YMax))
        if near_corner:
            continue
        if _in_post(p, posts, 0.1):
            edges.append(e)
    if not edges:
        return shape
    for rr in (r, 2.0, 1.5):              # OCC gets pickier next to the base torus
        try:
            return shape.makeFillet(rr, edges)
        except Exception:
            continue
    return shape


def fillet_post_bases(shape, posts, z, r=2.5):
    # concave roundover where each post meets the plate — the printed gusset.
    # MUST run on clean geometry BEFORE the vertical fillets: afterwards the
    # junction loop is a mixed bag of lines and fillet seams and OCC refuses it.
    # Edges flush with the plate's outer boundary are skipped — there is no
    # material outside them to blend into and they poison the whole operation.
    bb = shape.BoundBox
    eps = 0.05
    edges = []
    for e in shape.Edges:
        vs = e.Vertexes
        if not vs or not all(abs(v.Point.z - z) < 0.01 for v in vs):
            continue
        p = vs[0].Point
        if not _in_post(p, posts, 0.6):
            continue
        mid = e.valueAt(e.FirstParameter + (e.LastParameter - e.FirstParameter) / 2)
        on_boundary = (abs(mid.x - bb.XMin) < eps or abs(mid.x - bb.XMax) < eps or
                       abs(mid.y - bb.YMin) < eps or abs(mid.y - bb.YMax) < eps)
        if on_boundary:
            continue
        edges.append(e)
    if not edges:
        return shape
    for rr in (r, 2.0, 1.5, 1.0):
        try:
            return shape.makeFillet(rr, edges)
        except Exception:
            continue
    return shape


# ---- base plate: interior + ring all around ----
base = box(INT_W + 2 * RING, INT_H + 2 * RING, BASE_T, -RING, -RING, 0)

# cabinet mounting holes, mid-edge on the left/right ring
for hx in (-RING / 2, INT_W + RING / 2):
    for hy in (INT_H * 0.25, INT_H * 0.75):
        base = base.cut(Part.makeCylinder(MOUNT_HOLE / 2, BASE_T, App.Vector(hx, hy, 0)))

# ---- posts: four corners + two mid-edge, on the ring ----
# corners, plus mid-span posts on the north/south edges at the bay line so the
# 217mm lid does not sag; they flank the bay opening rather than covering it
POSTS = [(-RING, -RING), (INT_W, -RING), (-RING, INT_H), (INT_W, INT_H),
         (MARGIN + CORGI_W + GAP / 2 - POST_W / 2, -RING),
         (MARGIN + CORGI_W + GAP / 2 - POST_W / 2, INT_H)]
for px, py in POSTS:
    base = base.fuse(box(POST_W, POST_W, BASE_T + POST_H, px, py, 0))
    base = base.cut(Part.makeCylinder(POST_PILOT / 2, 10,
                                      App.Vector(px + POST_W / 2, py + POST_W / 2, BASE_T + POST_H - 10)))

# ---- standoffs ----
def standoff(shape, x, y):
    shape = shape.fuse(Part.makeCylinder(STAND_D / 2, STAND_H, App.Vector(x, y, BASE_T)))
    return shape.cut(Part.makeCylinder(PILOT_D / 2, STAND_H + 6, App.Vector(x, y, BASE_T)))

for hx, hy in CORGI_HOLES:               # Corgi east, corner at (CORGI_X0, 5)
    base = standoff(base, CORGI_X0 + hx, MARGIN + hy)

AIRE_CX = MARGIN + CORGI_W + GAP + AIRE_SHORT / 2.0   # Airedale east, portrait
AIRE_CY = INT_H / 2.0
for sx in (-1, 1):
    for sy in (-1, 1):
        base = standoff(base, AIRE_CX + sx * AIRE_DS / 2, AIRE_CY + sy * AIRE_DL / 2)

base = base.removeSplitter()
base = fillet_post_bases(base, POSTS, BASE_T)   # first, on clean junction loops
base = fillet_corners(base)
base = fillet_post_verticals(base, POSTS)
obj = doc.addObject("Part::Feature", "cabinet_tray")
obj.Shape = base

# ---- lid: vented roof screwed to the posts ----
top_z = BASE_T + POST_H
lid = box(INT_W + 2 * RING, INT_H + 2 * RING, LID_T, -RING, -RING, top_z)
for px, py in POSTS:
    lid = lid.cut(Part.makeCylinder(LID_SCREW_D / 2, LID_T,
                                    App.Vector(px + POST_W / 2, py + POST_W / 2, top_z)))
vx = 12.0
while vx < INT_W - 16:
    lid = lid.cut(box(4, INT_H - 24, LID_T, vx, 12, top_z))
    vx += 12
lid = lid.removeSplitter()
lid = fillet_corners(lid)
lobj = doc.addObject("Part::Feature", "cabinet_lid")
lobj.Shape = lid

doc.recompute()
here = os.path.dirname(os.path.abspath(__file__))
obj.Shape.exportStep(os.path.join(here, "cabinet_tray.step"))
lobj.Shape.exportStep(os.path.join(here, "cabinet_lid.step"))
Mesh.export([obj], os.path.join(here, "cabinet_tray.stl"))
Mesh.export([lobj], os.path.join(here, "cabinet_lid.stl"))
print("open-frame chassis exported")
