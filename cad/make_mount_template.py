#!/usr/bin/env python3
# 1:1 paper drill template for the lean-to's table bolts and cable hole.
# Raw PDF, no dependencies. The numbers are not copied: lean_to.py's
# parameter and derived blocks are executed here (no FreeCAD needed), so the
# template follows the model.
# PRINT AT 100% SCALE (Letter, landscape) — the 100mm bar is the check.
import math
import os

HERE = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(HERE, "lean_to.py")).read()
exec(src[src.index("# ---- panel, measured"):src.index("def roof_inner_z")])

# Same expressions as lean_to.py's rail block.
nut_r = NUT_AF / math.sqrt(3)
rail_y1 = Y_BACK - COVER_T
hinge_y = rail_y1 - HINGE_FWD
bolt_y = (Y_FOOT + WALL + nut_r + 1, hinge_y - HINGE_BORE_D / 2 - 2 - nut_r - 1)
rail_xc = W / 2 - WALL - RAIL_W / 2
rail_in = W / 2 - WALL - RAIL_W
# Open bottom, table level: between the rails, from the foot wall's inside
# (it leans at LEAN_DEG off the table, so WALL thick is WALL/s across) to the
# door. Cable hole goes in here, MARGIN clear of the walls.
MARGIN = 5.0
open_y0, open_y1 = Y_FOOT + WALL / s, rail_y1

PAGE_W, PAGE_H = 279.4, 215.9                 # Letter landscape, mm
OX = (PAGE_W - W) / 2                         # page position of world X = -W/2
OY = 70.0                                     # page position of world Y = Y_FOOT
PT = 72 / 25.4


def mm(v):
    return "%.3f" % (v * PT)


def px(x):                                    # world -> page; viewed from above,
    return OX + x + W / 2                     # operator at the bottom of the page


def py(y):
    return OY + y - Y_FOOT


s_ = []
s_.append("0.4 w")
# Calibration bar: exactly 100mm, ticks every 10.
by = 25.0
s_.append("%s %s m %s %s l S" % (mm(OX), mm(by), mm(OX + 100), mm(by)))
for i in range(11):
    s_.append("%s %s m %s %s l S" % (mm(OX + 10 * i), mm(by), mm(OX + 10 * i), mm(by + 3)))
# Footprint, dashed.
s_.append("[%s %s] 0 d" % (mm(2), mm(2)))
s_.append("%s %s %s %s re S" % (mm(px(-W / 2)), mm(py(Y_FOOT)), mm(W), mm(Y_BACK - Y_FOOT)))
# Rails, dotted.
s_.append("[%s %s] 0 d" % (mm(0.5), mm(1.5)))
for sx in (-1, 1):
    x0 = min(sx * rail_in, sx * (W / 2 - WALL))
    s_.append("%s %s %s %s re S" % (mm(px(x0)), mm(py(Y_FOOT)), mm(RAIL_W), mm(rail_y1 - Y_FOOT)))
# Cable zone, solid.
s_.append("[] 0 d")
s_.append("%s %s %s %s re S" % (mm(px(-rail_in + MARGIN)), mm(py(open_y0 + MARGIN)),
                                mm(2 * (rail_in - MARGIN)), mm(open_y1 - open_y0 - 2 * MARGIN)))
# Bolt holes: clearance circle, crosshair, punch dot.
k = 0.5523
for sx in (-1, 1):
    for y in bolt_y:
        cx, cy, r = px(sx * rail_xc), py(y), BOLT_CLR / 2
        s_.append("0.6 w")
        s_.append("%s %s m" % (mm(cx + r), mm(cy)))
        # Four Bezier quarters, counter-clockwise; controls along the tangents.
        pts = [(cx + r, cy), (cx, cy + r), (cx - r, cy), (cx, cy - r), (cx + r, cy)]
        for i in range(4):
            (x0, y0), (x1, y1) = pts[i], pts[i + 1]
            c1 = (x0 - k * (y0 - cy), y0 + k * (x0 - cx))
            c2 = (x1 + k * (y1 - cy), y1 - k * (x1 - cx))
            s_.append("%s %s %s %s %s %s c" % (mm(c1[0]), mm(c1[1]), mm(c2[0]), mm(c2[1]), mm(x1), mm(y1)))
        s_.append("S 0.3 w")
        s_.append("%s %s m %s %s l S" % (mm(cx - 8), mm(cy), mm(cx + 8), mm(cy)))
        s_.append("%s %s m %s %s l S" % (mm(cx), mm(cy - 8), mm(cx), mm(cy + 8)))
        s_.append("%s %s %s %s re f" % (mm(cx - 0.4), mm(cy - 0.4), mm(0.8), mm(0.8)))


def text(x, y, size, t):
    s_.append("BT /F1 %g Tf %s %s Td (%s) Tj ET" % (size, mm(x), mm(y), t))


top = py(Y_BACK)
text(OX, top + 30, 11, "Lean-to - table drill template  (PRINT AT 100% SCALE, Letter landscape)")
text(OX, top + 23, 8, "Seen from above. Bottom edge of the dashed outline = FRONT (operator side).")
text(OX, top + 17, 8, "4 x 1/4-20, drill at the crosshairs. Circles = the part's %.1f mm holes." % BOLT_CLR)
text(OX, top + 11, 8, "Centres %.2f mm across, %.2f mm front to back; front pair %.1f mm behind the front edge."
     % (2 * rail_xc, bolt_y[1] - bolt_y[0], bolt_y[0] - Y_FOOT))
text(px(-rail_in + MARGIN) + 3, py(open_y0 + MARGIN) + 3, 8,
     "Cable hole: anywhere inside this box (open bottom, %.0f mm off the walls)" % MARGIN)
text(OX, py(Y_FOOT) - 8, 8, "FRONT")
text(OX + 104, by - 1, 8, "= 100.0 mm exactly, or the print is scaled")
text(OX, by - 8, 8, "Check against the printed shell before drilling: it should sit on the dashed outline.")

content = "\n".join(s_)
objs = [
    "<< /Type /Catalog /Pages 2 0 R >>",
    "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
    "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %s %s] "
    "/Resources << /Font << /F1 4 0 R >> >> /Contents 5 0 R >>" % (mm(PAGE_W), mm(PAGE_H)),
    "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
    "<< /Length %d >>\nstream\n%s\nendstream" % (len(content), content),
]
out = "%PDF-1.4\n"
xref = []
for i, o in enumerate(objs):
    xref.append(len(out))
    out += "%d 0 obj\n%s\nendobj\n" % (i + 1, o)
start = len(out)
out += "xref\n0 %d\n0000000000 65535 f \n" % (len(objs) + 1)
for x in xref:
    out += "%010d 00000 n \n" % x
out += "trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n" % (len(objs) + 1, start)

path = os.path.join(HERE, "lean_to_mount_template.pdf")
open(path, "wb").write(out.encode("latin-1"))
print("wrote", path)
