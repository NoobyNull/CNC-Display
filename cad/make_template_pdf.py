#!/usr/bin/env python3
# 1:1 paper cutout template for the switch plate's wood opening.
# Raw PDF, no dependencies. Numbers mirror switch_plate.py's build
# (plate 160.0 x 110.0, opening 148.0 x 98.0 + 0.5/side slop).
# PRINT AT 100% SCALE — the 100mm bar on the page is the check.

PLATE_W, PLATE_H = 160.0, 110.0
CUT_W, CUT_H = 149.0, 99.0       # opening incl. slop; 5.5 in from the edge
OX, OY = 25.0, 100.0             # plate outline's page position, mm

PT = 72 / 25.4                   # mm -> pt


def mm(v):
    return "%.3f" % (v * PT)


s = []
s.append("0.4 w")
# Calibration bar: exactly 100mm, ticks every 10.
by = OY - 25.0
s.append("[] 0 d")
s.append("%s %s m %s %s l S" % (mm(OX), mm(by), mm(OX + 100), mm(by)))
for i in range(11):
    s.append("%s %s m %s %s l S" % (mm(OX + 10 * i), mm(by),
                                    mm(OX + 10 * i), mm(by + 3)))
# Outer outline (plate footprint), dashed.
s.append("[%s %s] 0 d" % (mm(2), mm(2)))
s.append("%s %s %s %s re S" % (mm(OX), mm(OY), mm(PLATE_W), mm(PLATE_H)))
# Cut line, solid and heavier.
s.append("[] 0 d")
s.append("0.8 w")
ix, iy = OX + (PLATE_W - CUT_W) / 2, OY + (PLATE_H - CUT_H) / 2
s.append("%s %s %s %s re S" % (mm(ix), mm(iy), mm(CUT_W), mm(CUT_H)))
# Centrelines, dash-dot, poking 8mm past the outline.
s.append("0.3 w")
s.append("[%s %s %s %s] 0 d" % (mm(4), mm(1.5), mm(0.5), mm(1.5)))
cxm, cym = OX + PLATE_W / 2, OY + PLATE_H / 2
s.append("%s %s m %s %s l S" % (mm(cxm), mm(OY - 8), mm(cxm),
                                mm(OY + PLATE_H + 8)))
s.append("%s %s m %s %s l S" % (mm(OX - 8), mm(cym),
                                mm(OX + PLATE_W + 8), mm(cym)))
s.append("[] 0 d")


def text(x, y, size, t):
    s.append("BT /F1 %g Tf %s %s Td (%s) Tj ET" % (size, mm(x), mm(y), t))


text(OX, OY + PLATE_H + 16, 11,
     "Corgi switch plate - wood cutout template  (PRINT AT 100% SCALE)")
text(OX, OY + PLATE_H + 10, 8,
     "Solid line = cut line, %.0f x %.0f mm, slop included. "
     "Dashed = finished plate footprint." % (CUT_W, CUT_H))
text(OX + 104, by - 1, 8, "= 100.0 mm exactly, or the print is scaled")
text(OX, by - 8, 8,
     "Corners: R4 or tighter; square corners are fine (extra clearance).")

content = "\n".join(s)
objs = []
objs.append("<< /Type /Catalog /Pages 2 0 R >>")
objs.append("<< /Type /Pages /Kids [3 0 R] /Count 1 >>")
objs.append("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595.28 841.89] "
            "/Resources << /Font << /F1 4 0 R >> >> /Contents 5 0 R >>")
objs.append("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")
objs.append("<< /Length %d >>\nstream\n%s\nendstream" % (len(content),
                                                         content))

out = "%PDF-1.4\n"
xref = []
for i, o in enumerate(objs):
    xref.append(len(out))
    out += "%d 0 obj\n%s\nendobj\n" % (i + 1, o)
start = len(out)
out += "xref\n0 %d\n0000000000 65535 f \n" % (len(objs) + 1)
for x in xref:
    out += "%010d 00000 n \n" % x
out += ("trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n"
        % (len(objs) + 1, start))

import os
here = os.path.dirname(os.path.abspath(__file__))
path = os.path.join(here, "switch_plate_template.pdf")
open(path, "wb").write(out.encode("latin-1"))
print("wrote", path)
