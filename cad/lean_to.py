# Lean-to cabinet: Elecrow CrowPanel Advance 7.0 (V1.4), bolted rigid to the
# table top. No adjustment.
#
# The panel's stock front bezel frame is REMOVED and not used. The front wall
# is a plain 5.58mm plate (the LCD module's thickness) with a window for the
# LCD: the PCB bears on the back of the plate, the LCD fills the window flush.
# Four #4-40 x 5/8 socket heads go in from the front, heads proud of the face,
# through the panel's own corner holes (PCB, middle frame, acrylic
# back; drill the last two out to 1/8), nuts behind.
# The back is a flap on a snap-in hinge (pins into the rails), latched by a
# hairpin spring that is a separate part, printed on its side so it bends along
# its layers, and CA-glued to the flap. No heat-sets anywhere. The bottom is open: cables drop straight
# through a hole in the table. Four 1/4-20 bolts come up from under the table
# into hex nuts slid sideways into two floor rails.
#
# Every panel number below was measured from Elecrow's STEP
# (3D file/00-7_0-view-v2_12_10.stp in Elecrow-RD/CrowPanel-Advance-7-HMI-...)
# and is in that file's own frame ("panel coords"): X along the long edge,
# Y along the short edge, Z front (-7) to back (+8). Do not "fix" them.
#
# World frame: X across the table, Y away from the operator, Z up, table = Z0.
#
# Print: the shell goes FACE DOWN. The side and foot walls
# then all stand straight up off the face; the roof and the rails lean at 15-30
# degrees from vertical. No supports.
#
# Run from the FreeCAD Python console:  exec(open("cad/lean_to.py").read())

import FreeCAD as App
import Part
import Mesh
import MeshPart
import math
import os

HERE = os.path.dirname(os.path.abspath(__file__)) if "__file__" in dir() else "cad"
PANEL_STEP = os.path.join(HERE, "ref", "crowpanel_advance7.stp")   # 40MB, gitignored; mockup

# ---- panel, measured (panel coords) -----------------------------------------
UNIT_X0, UNIT_X1 = -2.20, 179.06     # stock case outline
UNIT_Y0, UNIT_Y1 = -2.20, 106.16
UNIT_R = 5.2                         # case corner radius
STOCK_BEZEL_Z = (-7.0, 0.0)          # removed; only used to leave it out of the mockup
PCB_Z_FRONT = -1.60                  # PCB front face: bears on the back of the face plate
LCD_X0, LCD_X1 = 6.00, 170.86        # LCD module outline
LCD_Y0, LCD_Y1 = 2.11, 102.07
UNIT_Z_BACK = 8.0                    # acrylic back
BACK_PROUD = 1.0                     # connectors stand this far off the back
CASE_SCREW_X = (3.00, 173.86)        # case corner holes: PCB 3.2, middle + back 2.7 (drill to 1/8)
CASE_SCREW_Y = (3.00, 100.96)
USB_Y, USB_Z = 51.98, 1.11           # USB-C centre, on the X1 edge

# ---- mounting choices --------------------------------------------------------
LEAN_DEG = 30.0          # face back from vertical
ROOF_DEG = 45.0          # roof falls away from the face top; 45 prints unsupported
FACE_T = 5.58            # front wall = LCD module thickness (measured): LCD flush
LCD_CLR, LCD_WIN_R = 0.2, 0.5
WALL = 3.0
R_FILLET = 1.5           # every inside corner except fit features (see finish_edges)
CHAMFER = 1.0            # every external edge of the shell except fit features
RIM = 12.0               # face border outside the panel outline
FIT = 0.3                # clearance round the case behind the face
BACK_CLEAR = 30.0        # cable room behind the panel (plugs + bend radius)

# ---- panel snap pins, printed with the shell (print it in PETG) -------------
STACK_HOLE_D = 3.2       # PCB as made; middle frame + acrylic drilled to 1/8
N440_CLR = 3.2           # #4-40 free fit (1/8)

# ---- table fixing: 1/4-20 up from underneath --------------------------------
BOLT_CLR = 6.8
NUT_AF, NUT_H = 11.11 + 0.3, 5.56 + 0.4
RAIL_W, RAIL_H = 18.0, 12.0
UNDER_CLEAR = 2.0        # panel's lowest corner above the rail tops

# ---- status bar: 8-px SK6812 stick in the roof, clear PLA lens ---------------
PIX_L, PIX_W = 49.4, 4.75      # lit strip only; that is all that shows
PIX_CLR = 0.3
BOARD_L, BOARD_W, BOARD_T = 51.0, 10.2, 1.6
LED_H = 1.6                    # stick is 3.2 over the LEDs
LENS_FLANGE_T = 1.0            # clamped between roof and LEDs
LENS_CLR = 0.15
POCKET_CLR = 0.3
PAD_WALL = 2.0
SLOT_AT = 0.35                 # centre, as a fraction down the roof from the face top
                               # (forward of the flap latch's catch ledge)

# ---- back flap: snap-in hinge on the rails, detents up top -------------------
COVER_T, COVER_GAP = 3.0, 0.2
HINGE_PIN_D, HINGE_BORE_D = 3.0, 3.4
HINGE_ENGAGE = 2.0       # pin depth into the rail
HINGE_Z = 8.0            # axis height, inside the rail
HINGE_FWD = 3.5          # axis this far in front of the flap's inner face
HINGE_TAB_T, HINGE_TAB_GAP = 2.5, 0.3
SWING_CLEAR = 0.5        # flap's bottom corner over the table as it swings
DOOR_W = 120.0           # the door; the shell's back wall closes the rest
KNUCKLE_W = 5.0          # hinge knuckle at each back-wall section's inner bottom corner
LIP_T, LIP_OVER = 2.0, 2.0   # door stop on each back-wall section: thickness, reach over the door
# hairpin latch, top centre. A lever: stiff lower leg off the flap, thin U-bend
# as the hinge, upper leg + flush thumb tab pivot about it. Pull the tab out to
# tip the upper leg and drop its barb from behind a shallow catch ledge.
HP_W, HP_GAP = 12.0, 2.5 # width (X); gap between the legs (= the stop: full travel)
HP_T_UP, HP_T_LOW, HP_T_BEND = 2.0, 3.0, 1.2   # upper leg, lower leg, U-bend
HP_L = 12.0              # legs reach this far forward of the flap's inner face
LEDGE_D = 3.0            # catch ledge depth (Y), shallow, close to the flap
LEDGE_BACK_CLR = 0.8     # ledge back face to the flap's inner face
CATCH_FRACTION = 0.75    # barb engagement, as a share of its drop at full travel
GROOVE_R, GROOVE_DROP = 1.0, 1.6   # fingernail groove across the tab: radius, below its top
BEND_LEAD_UP = 5.0       # upper leg's outer face runs from this far back of the bend
                         # as a flank tangent to the U (the lower one runs to the foot)
FOOT_T = 3.5             # hairpin's mounting foot under the lower leg, on the flap
FOOT_H = 12.0            # glue pad below the lower leg (CA to the flap's inner face)
LATCH_DROP = 4.0         # upper leg's top this far below the flap's top edge
NOTCH_CLR = 0.5          # flap notch either side of the tab
CATCH_DEG = 75.0         # steep: it only lets go when the tab is pulled; 75 is the
                         # steepest the ledge can be and still print face-down
LATCH_CLR = 0.3          # upper leg under the catch ledge; barb in front of it

# ---- derived -----------------------------------------------------------------
s, c = math.sin(math.radians(LEAN_DEG)), math.cos(math.radians(LEAN_DEG))
unit_w, unit_h = UNIT_X1 - UNIT_X0, UNIT_Y1 - UNIT_Y0
W = unit_w + 2 * RIM
L = unit_h + 2 * RIM
xc = (UNIT_X0 + UNIT_X1) / 2
y_base = UNIT_Y0 - RIM               # face bottom edge, panel Y
z_face = PCB_Z_FRONT - FACE_T        # face front; LCD front lands here


def world_yz(py, pz, kick):
    """Panel (y, z) -> world (Y, Z) with the face bottom edge at (0, kick)."""
    dy, dz = py - y_base, pz - z_face
    return dy * s + dz * c, kick + dy * c - dz * s


# Kick = height of the face's bottom edge. Just enough that the panel's lowest
# corner (bottom edge, back, plus connectors) clears the rails.
_, z_low0 = world_yz(UNIT_Y0 - FIT, UNIT_Z_BACK + BACK_PROUD, 0.0)
KICK = max(RAIL_H + UNDER_CLEAR - z_low0, RAIL_H + WALL)

Z_TOP = KICK + L * c
Y_TOP = L * s
Y_BACK = world_yz(UNIT_Y1 + FIT, UNIT_Z_BACK + BACK_CLEAR, KICK)[0] + COVER_T
tr = math.tan(math.radians(ROOF_DEG))
Z_ROOF_END = Z_TOP - (Y_BACK - Y_TOP) * tr
Y_FOOT = KICK * c / s                # foot wall meets the table here


def roof_inner_z(y):
    return Z_TOP - (y - Y_TOP) * tr - WALL / math.cos(math.radians(ROOF_DEG))


# Panel coords -> world. Columns are where panel X, Y, Z land; det = +1.
M = App.Matrix(-1, 0, 0, xc,
               0, s, c, -(s * y_base + c * z_face),
               0, c, -s, KICK - (c * y_base - s * z_face),
               0, 0, 0, 1)


def place(shape):
    sh = shape.copy()
    sh.transformShape(M)
    return sh


def rbox(x0, y0, z0, x1, y1, z1, r):
    b = Part.makeBox(x1 - x0, y1 - y0, z1 - z0, App.Vector(x0, y0, z0))
    vert = [e for e in b.Edges if abs(e.Vertexes[0].Z - e.Vertexes[1].Z) > 1e-6]
    return b.makeFillet(r, vert) if r > 0 else b


def box(x0, y0, z0, x1, y1, z1):
    return Part.makeBox(x1 - x0, y1 - y0, z1 - z0, App.Vector(x0, y0, z0))


def inset(pts, t):
    """Offset each edge of a clockwise (Y, Z) polygon inward by its own t."""
    n, lines = len(pts), []
    for i in range(n):
        (ay, az), (by, bz) = pts[i], pts[(i + 1) % n]
        ln = math.hypot(by - ay, bz - az)
        dy, dz = (by - ay) / ln, (bz - az) / ln
        lines.append(((ay + dz * t[i], az - dy * t[i]), (dy, dz)))
    out = []
    for i in range(n):
        (p, d), (q, e) = lines[i - 1], lines[i]
        den = d[0] * e[1] - d[1] * e[0]
        k = ((q[0] - p[0]) * e[1] - (q[1] - p[1]) * e[0]) / den
        out.append((p[0] + k * d[0], p[1] + k * d[1]))
    return out


def prism(pts, x0, x1):
    poly = Part.makePolygon([App.Vector(x0, y, z) for y, z in pts] + [App.Vector(x0, *pts[0])])
    return Part.Face(poly).extrude(App.Vector(x1 - x0, 0, 0))


def hexagon(x, y, z0, af, h):
    r = af / math.sqrt(3)
    pts = [App.Vector(x + r * math.cos(math.radians(60 * i)), y + r * math.sin(math.radians(60 * i)), z0)
           for i in range(7)]
    return Part.Face(Part.makePolygon(pts)).extrude(App.Vector(0, 0, h))


refused = []                 # midpoints of the last finish_edges call's refusals
reduced = []                 # radii the last call fell back to


def watertight(shape):
    """Every mesh edge shared by exactly two triangles. OCC will happily return
    a 'valid' solid whose chamfer-meets-fillet corners tessellate with cracks."""
    tri = MeshPart.meshFromShape(Shape=shape, LinearDeflection=0.1, AngularDeflection=0.5,
                                 Relative=False).Topology[1]
    count = {}
    for f in tri:
        for a, b in ((f[0], f[1]), (f[1], f[2]), (f[2], f[0])):
            k = (a, b) if a < b else (b, a)
            count[k] = count.get(k, 0) + 1
    return all(n == 2 for n in count.values())


def finish_edges(shape, size, keep_sharp=(), concave=True, only_on=None):
    """Fillet every concave edge (concave=True) or chamfer every convex one,
    except edges touching a keep_sharp solid. only_on: a shape the edge's
    midpoint must lie on (e.g. the outer skin).

    Concave test: step off the edge midpoint along (nA - nB). On a convex edge
    that lands outside the part, on a concave one inside it.
    Returns (shape, done, refused).
    """
    picked = []
    for e in shape.Edges:
        fs = shape.ancestorsOfType(e, Part.Face)
        if len(fs) != 2:
            continue
        p = e.valueAt((e.FirstParameter + e.LastParameter) / 2)
        if any(k.isInside(p, 1e-3, True) for k in keep_sharp):
            continue
        if only_on is not None and only_on.distToShape(Part.Vertex(p))[0] > 1e-3:
            continue
        na, nb = (f.normalAt(*f.Surface.parameter(p)) for f in fs)
        d = na - nb
        if d.Length < 0.1:                       # tangent join, nothing to round
            continue
        if shape.isInside(p + d.normalize() * 0.05, 1e-4, False) == concave:
            picked.append(e)

    refused.clear()
    reduced.clear()

    def attempt(sh, edges):
        # One op per batch; a batch OCC refuses is split in half.
        if not edges:
            return sh, 0, 0
        try:
            res = (sh.makeFillet if concave else sh.makeChamfer)(size, edges)
            if not res.isValid() or len(res.Solids) != 1 or not watertight(res):
                raise ValueError("op left an invalid solid")
            return res, len(edges), 0
        except Exception:
            if len(edges) == 1:
                # alone and still refused: a smaller fillet usually fits (not chamfers:
                # those are a stated size)
                for s2 in ((size * 2 / 3, size / 3) if concave else ()):
                    try:
                        res = sh.makeFillet(s2, edges)
                        if res.isValid() and len(res.Solids) == 1 and watertight(res):
                            reduced.append(s2)
                            return res, 1, 0
                    except Exception:
                        pass
                refused.append(edges[0].valueAt((edges[0].FirstParameter + edges[0].LastParameter) / 2))
                return sh, 0, 1
            h = len(edges) // 2
            sh1, ok1, bad1 = attempt(sh, edges[:h])
            # edges of the second half, found again on the new shape by midpoint
            mids = [e.valueAt((e.FirstParameter + e.LastParameter) / 2) for e in edges[h:]]
            rest = [e2 for m in mids for e2 in sh1.Edges
                    if e2.distToShape(Part.Vertex(m))[0] < 1e-4][:len(mids)]
            sh2, ok2, bad2 = attempt(sh1, rest)
            return sh2, ok1 + ok2, bad1 + bad2 + (len(mids) - len(rest))

    return attempt(shape, picked)


def fuse_all(shapes):
    out = shapes[0]
    for sh in shapes[1:]:
        out = out.fuse(sh)
    return out


# ---- shell: side profile, clockwise from the foot's front-bottom corner ------
profile = [(Y_FOOT, 0.0), (0.0, KICK), (Y_TOP, Z_TOP), (Y_BACK, Z_ROOF_END), (Y_BACK, 0.0)]
#          foot wall      face         roof           back (open)  bottom (open)
outer = prism(profile, -W / 2, W / 2)
cavity = prism(inset(profile, [WALL, FACE_T, WALL, -1.0, -1.0]), -W / 2 + WALL, W / 2 - WALL)
shell = outer.cut(cavity)

adds, cuts = [], []

# LCD window through the front wall.
cuts.append(place(rbox(LCD_X0 - LCD_CLR, LCD_Y0 - LCD_CLR, z_face - 1,
                       LCD_X1 + LCD_CLR, LCD_Y1 + LCD_CLR, PCB_Z_FRONT + 0.01, LCD_WIN_R)))

# Panel bolts: #4-40 through at the case corner holes, heads proud of the face.
for sx in CASE_SCREW_X:
    for sy in CASE_SCREW_Y:
        cuts.append(place(Part.makeCylinder(N440_CLR / 2, FACE_T + 2, App.Vector(sx, sy, z_face - 1))))
snaps = []                                             # (the old printed snap pins; none now)

# USB-C (flashing) through the side wall. Sized for the overmould.
USB_SLOT_H, USB_SLOT_D = 14.0, 9.0
usb_cut = place(box(UNIT_X1 - 3, USB_Y - USB_SLOT_H / 2, USB_Z - USB_SLOT_D / 2,
                    UNIT_X1 + RIM + 1, USB_Y + USB_SLOT_H / 2, USB_Z + USB_SLOT_D / 2))
cuts.append(usb_cut)

# Floor rails with side-entry 1/4-20 nut slots; they also carry the flap's
# hinge bores in their inner faces.
nut_r = NUT_AF / math.sqrt(3)
rail_y1 = Y_BACK - COVER_T
hinge_y = rail_y1 - HINGE_FWD
bolt_y = (Y_FOOT + WALL + nut_r + 1, hinge_y - HINGE_BORE_D / 2 - 2 - nut_r - 1)
nut_z0 = (RAIL_H - NUT_H) / 2
rail_xc = W / 2 - WALL - RAIL_W / 2
rail_in = W / 2 - WALL - RAIL_W                # |X| of the rails' inner faces
cover_top = roof_inner_z(Y_BACK) - COVER_GAP
cw = W / 2 - WALL - COVER_GAP
for sx in (-1, 1):
    rx0, rx1 = sorted((sx * (W / 2 - WALL + 0.01), sx * (W / 2 - WALL - RAIL_W)))
    adds.append(box(rx0, 0, 0, rx1, rail_y1, RAIL_H).common(outer))
    for by in bolt_y:
        x = sx * rail_xc
        cuts.append(Part.makeCylinder(BOLT_CLR / 2, RAIL_H + 2, App.Vector(x, by, -1)))
        cuts.append(hexagon(x, by, nut_z0, NUT_AF, NUT_H))
        sx0, sx1 = sorted((x, sx * (W / 2 - WALL - RAIL_W - 1)))
        cuts.append(box(sx0, by - NUT_AF / 2, nut_z0, sx1, by + NUT_AF / 2, nut_z0 + NUT_H))

# Back wall either side of the door, part of the shell: each section carries a
# hinge knuckle (the door's pins go into its bore; bow the door to fit them) and
# a lip along its inner edge that the shut door stops against. Face-down these
# sections overhang at 30 degrees: the shell needs supports under them.
dw = DOOR_W / 2
tab_top = HINGE_Z + HINGE_PIN_D / 2 + 2.0          # top of the door's hinge tabs
for sx in (-1, 1):
    x_in = sx * (dw + HINGE_TAB_GAP)
    bx0, bx1 = sorted((x_in, sx * (W / 2 - WALL + 0.5)))
    adds.append(box(bx0, rail_y1, 0, bx1, Y_BACK, Z_TOP + 1).common(outer))
    kx0, kx1 = sorted((x_in, x_in + sx * KNUCKLE_W))
    adds.append(box(kx0, hinge_y - HINGE_BORE_D / 2 - 2.0, 0, kx1, rail_y1 + 0.5, HINGE_Z + HINGE_BORE_D / 2 + 2.0))
    cuts.append(Part.makeCylinder(HINGE_BORE_D / 2, HINGE_ENGAGE + 1.0,
                                  App.Vector(x_in - sx * 0.5, hinge_y, HINGE_Z), App.Vector(sx, 0, 0)))
    lx0, lx1 = sorted((x_in + sx * 4.0, sx * (dw - LIP_OVER)))       # rooted 4mm onto the wall
    adds.append(box(lx0, rail_y1 - LIP_T, tab_top + 1.0, lx1, rail_y1, cover_top - R_FILLET - 1.0))

# Catch ledge for the flap's hairpin, hanging from the roof: shallow, close to
# the flap. Its front face is the catch, CATCH_DEG off horizontal (steeper than
# the roof, or it never meets it).
hp_top = cover_top - LATCH_DROP                   # upper leg's top face
bend_y = rail_y1 - HP_L                           # U-bend centre
bend_z = hp_top - HP_T_UP - HP_GAP / 2
hp_low = bend_z - HP_GAP / 2 - HP_T_LOW           # lower leg's bottom face
ledge_z = hp_top + LATCH_CLR
ledge_back = rail_y1 - LEDGE_BACK_CLR
ledge_fb = ledge_back - LEDGE_D                   # bottom-front corner
cot_c = 1 / math.tan(math.radians(CATCH_DEG))
barb_back = ledge_fb - LATCH_CLR / math.sin(math.radians(CATCH_DEG)) + LATCH_CLR * cot_c
# Full travel: the upper leg tips about the bend until its rear bottoms on the
# lower leg, HP_GAP down at HP_L out. The barb, nearer the bend, drops in
# proportion; it engages CATCH_FRACTION of that, and closing needs all of
# BARB_H (engagement + clearance), which must still fit inside full travel.
theta_max = HP_GAP / HP_L
barb_drop_max = theta_max * (barb_back - bend_y)
latch_defl = CATCH_FRACTION * barb_drop_max
BARB_H = latch_defl + LATCH_CLR
barb_tip = barb_back - BARB_H * cot_c - BARB_H    # 45 degree lead-in ahead of the catch
lz = roof_inner_z(ledge_fb) + WALL                     # well into the roof; outer clips it
ledges = [prism([(ledge_fb, ledge_z), (ledge_back, ledge_z), (ledge_back, lz + 20),
                 (ledge_fb - (lz + 20 - ledge_z) * cot_c, lz + 20)],
                -HP_W / 2 - 3, HP_W / 2 + 3).common(outer)]
adds.extend(ledges)

# Status bar. Built in a roof frame: x across, u down the slope from the face
# top, z out of the roof (depth is negative z). The pixels show through a slot;
# the board sits in a pocket in a pad under the roof, LEDs pressing the lens
# flange into place. Glue or tape the board in.
ra = math.radians(ROOF_DEG)
RM = App.Matrix(1, 0, 0, 0,
                0, math.cos(ra), math.sin(ra), Y_TOP,
                0, -math.sin(ra), math.cos(ra), Z_TOP,
                0, 0, 0, 1)


def on_roof(shape):
    sh = shape.copy()
    sh.transformShape(RM)
    return sh


slot_u = SLOT_AT * (Y_BACK - Y_TOP) / math.cos(ra)
win_l, win_w = PIX_L + 2 * PIX_CLR, PIX_W + 2 * PIX_CLR
pk_l, pk_w = BOARD_L + 2 * POCKET_CLR, BOARD_W + 2 * POCKET_CLR
z_pk0 = -WALL                                       # lens plug runs the full roof: flush
z_pk1 = z_pk0 - LENS_FLANGE_T - LED_H - BOARD_T     # pad underside; board back sits level with it


def rbox_u(l, w, z0, z1):
    return box(-l / 2, slot_u - w / 2, z0, l / 2, slot_u + w / 2, z1)


adds.append(on_roof(rbox_u(pk_l + 2 * PAD_WALL, pk_w + 2 * PAD_WALL, z_pk1, z_pk0 + 0.5)))
cuts.append(on_roof(rbox_u(win_l, win_w, z_pk0 - 0.01, 1.0)))
cuts.append(on_roof(rbox_u(pk_l, pk_w, z_pk1 - 1.0, z_pk0)))
lens_local = rbox_u(pk_l - 2 * LENS_CLR, pk_w - 2 * LENS_CLR, z_pk0 - LENS_FLANGE_T, z_pk0).fuse(
             rbox_u(win_l - 2 * LENS_CLR, win_w - 2 * LENS_CLR, z_pk0 - 0.01, 0.0)).removeSplitter()
lens = on_roof(lens_local)
stick = on_roof(rbox_u(BOARD_L, BOARD_W, z_pk1, z_pk0 - LENS_FLANGE_T))

shell = fuse_all([shell] + adds)
for h in cuts:
    shell = shell.cut(h)
shell = shell.removeSplitter()
# Fit features stay sharp: LCD window, nut pockets/slots, insert holes, LED
# window + board pocket (square parts sit in them), and the snap pins (a root
# fillet would hold the PCB off the plate). Plus the flap's seat: a fillet on
# the rail ends or wall edges there would stand into its space.
cover_seat = box(-W, rail_y1 - 1e-3, -1, W, Y_BACK + 1, Z_TOP + 1)
shell, fil_ok, fil_bad = finish_edges(shell, R_FILLET,
                                        [h for h in cuts if h is not usb_cut] + snaps + [cover_seat]
                                        + ledges)
fil_refused, fil_reduced = list(refused), list(reduced)
# Then 1mm off every external edge (on the outer skin), fit features kept sharp.
shell, ch_ok, ch_bad = finish_edges(shell, CHAMFER, [h for h in cuts if h is not usb_cut] + snaps + ledges,
                                    concave=False, only_on=outer.Shells[0])

# ---- back door: DOOR_W wide, in the shell's back wall ------------------------
# Its pins go into the knuckles on the back wall (bow the door to fit them). It is held
# shut by the hairpin (a separate part, CA-glued to the door): the stiff lower
# leg comes off the flap, U-turns at a thin bend, and the upper leg runs back
# under the roof's shallow catch ledge to a flush thumb tab. Closing, the ledge
# rides the barb's lead-in and tips the upper leg about the bend; the barb
# snaps up in front of the ledge. Pull the tab out by its fingernail groove to
# tip it again and open. The bottom edge is lifted just enough that its back corner clears the table through the swing.
cover_lift = HINGE_Z - math.sqrt((HINGE_Z - SWING_CLEAR) ** 2 - (COVER_T + HINGE_FWD) ** 2)
cover = box(-dw, rail_y1, cover_lift, dw, Y_BACK, cover_top)
tab_front = hinge_y - HINGE_PIN_D / 2 - 2.0
pin_len = HINGE_TAB_GAP + HINGE_ENGAGE
for sx in (-1, 1):
    # the door's hinge tab at its edge, pin outward into the knuckle
    xo = sx * dw
    tx0, tx1 = sorted((xo, xo - sx * HINGE_TAB_T))
    cover = cover.fuse(box(tx0, tab_front, cover_lift, tx1, rail_y1 + 0.5, tab_top))
    cover = cover.fuse(Part.makeCylinder(HINGE_PIN_D / 2, pin_len - 0.6 + 0.3,
                                         App.Vector(xo - sx * 0.3, hinge_y, HINGE_Z), App.Vector(sx, 0, 0)))
    # 45 degree gusset on the tab's inner side, from its tip down to the door:
    # stiffens it against the pin loads and the bow when the door is snapped in
    ti = xo - sx * HINGE_TAB_T
    reach = rail_y1 - tab_front
    gus = Part.Face(Part.makePolygon([App.Vector(ti + sx * 0.01, rail_y1 + 0.01, cover_lift),
                                      App.Vector(ti + sx * 0.01, tab_front, cover_lift),
                                      App.Vector(ti - sx * reach, rail_y1 + 0.01, cover_lift),
                                      App.Vector(ti + sx * 0.01, rail_y1 + 0.01, cover_lift)])).extrude(
        App.Vector(0, 0, tab_top - cover_lift))
    cover = cover.fuse(gus)
    cover = cover.fuse(Part.makeCone(HINGE_PIN_D / 2, HINGE_PIN_D / 2 - 0.5, 0.6,        # lead-in tip
                                     App.Vector(xo + sx * (pin_len - 0.6), hinge_y, HINGE_Z),
                                     App.Vector(sx, 0, 0)))
hx = HP_W / 2
cover = cover.cut(box(-hx - NOTCH_CLR, rail_y1 - 1, hp_low + HP_T_LOW,          # notch: the leg gap runs
                      hx + NOTCH_CLR, Y_BACK + 1, cover_top + 1))              # out through the back
cover_plain = cover.removeSplitter()                     # no latch: for the swing check
r_i = HP_GAP / 2
# The hairpin is its own part, CA-glued to the flap's inner face by its foot;
# the tab sitting in the flap's notch locates it.
groove = Part.makeCylinder(GROOVE_R, HP_W + 2, App.Vector(-hx - 1, Y_BACK, cover_top - GROOVE_DROP),
                           App.Vector(1, 0, 0))
# The spring (both legs and the U) is one side profile, extruded: each leg's
# outer face runs back from the U as a flank tangent to its outer arc, so the
# thin bend blends into the thicker legs with no step to start a crack. The
# inside of the U is already tangent to the gap.
r_bo = r_i + HP_T_BEND


def tangent_pt(py, pz, upper):
    """Where the line from (py, pz) touches the U's outer circle."""
    d = math.hypot(py - bend_y, pz - bend_z)
    phi, alpha = math.atan2(pz - bend_z, py - bend_y), math.acos(r_bo / d)
    a = phi + alpha if upper else phi - alpha
    return a, (bend_y + r_bo * math.cos(a), bend_z + r_bo * math.sin(a))


a_up, t_up = tangent_pt(bend_y + BEND_LEAD_UP, hp_top, True)
BEND_LEAD_LOW = HP_L - FOOT_T                     # lower flank runs all the way to the foot
a_lo, t_lo = tangent_pt(bend_y + BEND_LEAD_LOW, hp_low, False)
a_lo += 2 * math.pi
arc = [(bend_y + r_bo * math.cos(a), bend_z + r_bo * math.sin(a))
       for a in (a_up + (a_lo - a_up) * k / 48 for k in range(49))]
spring = prism([(rail_y1 + 0.5, hp_top), (bend_y + BEND_LEAD_UP, hp_top)] + arc +
               [(bend_y + BEND_LEAD_LOW, hp_low), (rail_y1, hp_low), (rail_y1, bend_z),
                (rail_y1 + 0.5, bend_z)], -hx, hx)
gap = prism([(bend_y + r_i * math.cos(a), bend_z + r_i * math.sin(a))
             for a in (math.pi / 2 + math.pi * k / 32 for k in range(33))] +
            [(Y_BACK + 1, bend_z - r_i), (Y_BACK + 1, bend_z + r_i)], -hx - 1, hx + 1)
spring = spring.cut(gap)
flank_deg = (math.degrees(math.atan2(hp_top - t_up[1], bend_y + BEND_LEAD_UP - t_up[0])),
             math.degrees(math.atan2(t_lo[1] - hp_low, bend_y + BEND_LEAD_LOW - t_lo[0])))
lever = [spring.common(box(-hx - 1, bend_y, bend_z, hx + 1, Y_BACK, cover_top + 1)),  # upper leg

         box(-hx, rail_y1, hp_top - HP_T_UP, hx, Y_BACK, cover_top).cut(groove),     # flush tab
         prism([(barb_tip, hp_top - 0.01), (barb_tip + BARB_H, hp_top + BARB_H),     # barb
                (barb_back, hp_top - 0.01)], -hx, hx)]
hooks = lever + [spring,                                                         # legs + U
                 box(-hx, rail_y1 - FOOT_T, hp_low + HP_T_LOW - FOOT_H, hx, rail_y1,
                     hp_low + HP_T_LOW)]                                             # foot
hairpin = fuse_all(hooks).removeSplitter()
# Round the hairpin's remaining inside corners (lower leg into the foot, upper
# leg into the tab); the barb keeps its sharp catch.
barb_zone = box(-hx - 1, barb_tip - 1, hp_top - 0.5, hx + 1, barb_back + 1, hp_top + BARB_H + 1)
hairpin, hf_ok, hf_bad = finish_edges(hairpin, R_FILLET, [barb_zone])
hf_reduced, hf_refused = list(reduced), list(refused)
cover = cover_plain

# ---- document ----------------------------------------------------------------
if App.listDocuments().get("lean_to"):
    App.closeDocument("lean_to")
doc = App.newDocument("lean_to")
parts = {"Shell": shell, "Cover": cover, "Hairpin": hairpin, "Lens": lens}
for name, sh in parts.items():
    doc.addObject("Part::Feature", name).Shape = sh

envelope = place(rbox(UNIT_X0, UNIT_Y0, PCB_Z_FRONT, UNIT_X1, UNIT_Y1,
                      UNIT_Z_BACK + BACK_PROUD, UNIT_R))
mock = None
if os.path.exists(PANEL_STEP):
    raw = Part.read(PANEL_STEP)
    keep = [so for so in raw.Solids      # minus the stock bezel frame: it comes off
            if so.BoundBox.XMin > UNIT_X0 - 3 and so.BoundBox.XMax < UNIT_X1 + 3
            and not (abs(so.BoundBox.ZMin - STOCK_BEZEL_Z[0]) < 0.05 and abs(so.BoundBox.ZMax - STOCK_BEZEL_Z[1]) < 0.05
                     and abs(so.BoundBox.XMin - UNIT_X0) < 0.05)]
    mock = place(Part.makeCompound(keep))
    doc.addObject("Part::Feature", "Mockup_CrowPanel").Shape = mock
else:
    doc.addObject("Part::Feature", "Mockup_Envelope").Shape = envelope
doc.recompute()

if App.GuiUp:
    import FreeCADGui as Gui
    for o in doc.Objects:
        vo = o.ViewObject
        if o.Name == "Shell":
            vo.ShapeColor = (0.12, 0.12, 0.12)
        elif o.Name == "Lens":
            vo.ShapeColor, vo.Transparency = (0.9, 0.95, 1.0), 50
        elif o.Name.startswith("Mockup"):
            vo.ShapeColor, vo.Transparency = (0.55, 0.6, 0.7), 40
        else:
            vo.ShapeColor = (0.25, 0.25, 0.28)
    Gui.activeDocument().activeView().viewIsometric()
    Gui.SendMsgToActiveView("ViewFit")


# ---- checks: clashes, with a positive control that must clash ---------------
def clash(a, b):
    return a.common(b).Volume


printed = [shell, cover, hairpin, lens]
# Behind the face: PCB, middle frame and back, with the corner holes as drilled.
drilled = [place(Part.makeCylinder(STACK_HOLE_D / 2, 20, App.Vector(sx, sy, PCB_Z_FRONT - 5)))
           for sx in CASE_SCREW_X for sy in CASE_SCREW_Y]
env_shrunk = place(rbox(UNIT_X0 + 0.05, UNIT_Y0 + 0.05, PCB_Z_FRONT + 0.05,
                        UNIT_X1 - 0.05, UNIT_Y1 - 0.05, UNIT_Z_BACK - 0.05, UNIT_R))
for h in drilled:
    env_shrunk = env_shrunk.cut(h)
control = env_shrunk.copy()
control.translate(App.Vector(0, -1.0 * c, 1.0 * s))     # 1mm forward into the face plate
report = {
    "stack envelope vs printed": sum(clash(env_shrunk, p) for p in printed),
    "control (1mm into face)": clash(control, shell),
    "flap vs shell (shut)": clash(cover, shell),
    "control (door pushed 0.5 in: lips)": clash(shell, cover.translated(App.Vector(0, -0.5, 0))),
    "hairpin vs shell / flap": clash(hairpin, shell) + clash(hairpin, cover),
    "lens vs shell": clash(lens, shell),
    "LED stick vs shell/lens": clash(stick, shell) + clash(stick, lens),
    "LED stick vs panel stack": clash(stick, env_shrunk),
}
if mock is not None:
    as_drilled = [so.cut(Part.makeCompound(drilled)) for so in mock.Solids]
    report["vendor STEP vs printed"] = sum(clash(so, p) for so in as_drilled for p in printed)
    fwd = mock.copy()
    fwd.translate(App.Vector(0, -0.5 * c, 0.5 * s))
    report["control (STEP 0.5mm fwd)"] = sum(clash(so, shell) for so in fwd.Solids)
report["panel lowest Z"] = envelope.BoundBox.ZMin
report["kick"] = KICK
report["footprint X x Y"] = "%.1f x %.1f" % (W, Y_BACK)
report["height"] = Z_TOP
report["bolt centres X"] = "+/-%.2f" % rail_xc
report["bolt centres Y"] = "%.2f, %.2f  (behind the foot's front edge)" % tuple(b - Y_FOOT for b in bolt_y)
report["shell volume cm3"] = shell.Volume / 1000
table = box(-W, -100, -100, W, 300, 0)
swing = []
for ang in (5, 15, 30, 45, 60, 75, 90):            # past 90 it is lying on the table
    f = cover_plain.copy()
    f.rotate(App.Vector(0, hinge_y, HINGE_Z), App.Vector(1, 0, 0), -ang)   # top swings back
    swing.append(clash(f, shell) + clash(f, table))
latch_defl = BARB_H - LATCH_CLR
pulled = [h.copy() for h in hooks]
for h in pulled:
    h.translate(App.Vector(0, 0.6, 0))                 # flap top pulled 0.6mm open
report["control (hooks pulled 0.6 open)"] = sum(clash(h, shell) for h in pulled)
# Bend strain at full travel: theta spread over the half-turn of the U.
bend_strain = theta_max / (math.pi * (r_i + HP_T_BEND / 2)) * HP_T_BEND / 2
report["bend blend"] = "legs run into the U on tangent flanks: %.1f / %.1f deg over %.0f / %.0f mm, no steps" % (
    flank_deg[0], flank_deg[1], BEND_LEAD_UP, BEND_LEAD_LOW)
report["hairpin fillets"] = "%d inside corners R%.1f%s, %d refused" % (
    hf_ok, R_FILLET, " (smaller: %s)" % hf_reduced if hf_reduced else "", hf_bad) + (
    " at %s" % [(round(q.y - rail_y1, 1), round(q.z - hp_top, 1)) for q in hf_refused] if hf_refused else "")
report["latch travel"] = ("full: tab tips %.1f deg, its top pulls out %.1f mm, its foot drops %.1f mm "
                          "onto the lower leg (the stop); barb drops %.2f mm" % (
                              math.degrees(theta_max), theta_max * (cover_top - bend_z), HP_GAP, barb_drop_max))
report["latch"] = "%.2f mm engagement (%.0f%% of that) at %.0f deg; closing needs %.2f of %.2f mm; bend %.1f%% strain" % (
    latch_defl, 100 * CATCH_FRACTION, CATCH_DEG, BARB_H, barb_drop_max, 100 * bend_strain)
# The hairpin through the swing: at rest it must bind for the first few degrees
# (that is the latch) and then clear; pressed, it must clear all the way.
# "Pressed" = pulled to full travel: the lever tipped about the bend.
tipped = [s.copy() for s in lever]
for s in tipped:
    s.rotate(App.Vector(0, bend_y, bend_z), App.Vector(1, 0, 0), -math.degrees(theta_max))
still = [s for s in hooks if not any(s is l for l in lever)]
hp_rest, hp_pressed = [], []
for ang in (0.5, 1, 2, 3, 5, 7, 10, 20, 45, 90):
    for group, acc in ((hooks, hp_rest), (tipped + still, hp_pressed)):
        hk = Part.makeCompound(group)
        hk.rotate(App.Vector(0, hinge_y, HINGE_Z), App.Vector(1, 0, 0), -ang)
        acc.append((ang, sum(clash(s, shell) for s in hk.Solids)))
report["hairpin swing"] = "at rest binds to %s deg then clear%s; pressed max %.3f mm3" % (
    max([a for a, v in hp_rest if v > 1e-3] or [0]),
    "" if all(v < 1e-3 for a, v in hp_rest if a >= 10) else " (NOT clear after 10 deg)",
    max(v for a, v in hp_pressed))
report["flap lift / swing clash"] = "%.2f mm / max %.3f mm3 over 5-90 deg" % (cover_lift, max(swing))
report["inside fillets"] = "%d edges R%.1f (%d at a smaller R: %s), %d refused %s" % (
    fil_ok, R_FILLET, len(fil_reduced), ["R%.1f" % r for r in fil_reduced], fil_bad,
    [(round(q.x), round(q.y), round(q.z)) for q in fil_refused])
report["outside chamfers"] = "%d edges x%.1f, %d refused" % (ch_ok, CHAMFER, ch_bad)

# ---- export: STEP in model pose, STL seated in print pose --------------------

LINEAR, ANGULAR = 0.05, 0.3


def seated(shape, axis=None, deg=0.0):
    sh = shape.copy()
    if axis:
        sh.rotate(App.Vector(0, 0, 0), App.Vector(*axis), deg)
    m = MeshPart.meshFromShape(Shape=sh, LinearDeflection=LINEAR, AngularDeflection=ANGULAR,
                               Relative=False)
    bb = m.BoundBox
    m.translate(-(bb.XMin + bb.XMax) / 2, -(bb.YMin + bb.YMax) / 2, -bb.ZMin)
    return m


Part.makeCompound([shell, cover, hairpin, lens]).exportStep(os.path.join(HERE, "lean_to.step"))
out = os.path.join(HERE, "print")
if not os.path.isdir(out):
    os.mkdir(out)
for fname, m in (("lean_to_shell_x1", seated(shell, (1, 0, 0), 90 + LEAN_DEG)),   # face down
                 ("lean_to_cover_x1", seated(cover, (1, 0, 0), -90)),             # flat, back face down
                 ("lean_to_hairpin_x1", seated(hairpin, (0, 1, 0), 90)),         # on its side: bends along layers
                 ("lean_to_lens_x1_clear", seated(lens_local))):                     # flange down
    m.write(os.path.join(out, fname + ".stl"))
    bb = m.BoundBox
    report[fname] = "%.1f x %.1f x %.1f  solid=%s" % (bb.XLength, bb.YLength, bb.ZLength, m.isSolid())

for k, v in report.items():
    print("%-28s %s" % (k, "%.3f" % v if isinstance(v, float) else v))
