"""
Rear enclosure for the Waveshare ESP32-S3-Touch-LCD-7B, with a VESA 75 pattern.

Run inside FreeCAD:  exec(open("cad/back_cover.py").read())

Every panel dimension below was measured off the vendor STEP
(ESP32-S3-Touch-LCD-7B_Drawing.zip), not off the datasheet picture, so the
frame is the assembly frame of that model:

    +X right, +Z up, -Y rearward.  Front glass at Y=+0.80.

Nothing here is a guessed number. If a value looks odd, it is because the board
is odd -- see the comment next to it.
"""

import FreeCAD as App
import Part
import os

# ---------------------------------------------------------------- panel facts
# Metal bracket behind the glass. The cover's footprint matches it, so the
# cover hides behind the glass overhang (glass is 192.96 x 110.76).
BRACKET_X0, BRACKET_X1 = -81.05, 84.35
BRACKET_Z1 = 51.18
BRACKET_BACK_Y = -7.70          # rim seats on this face

# M3 threaded holes in the bracket -- 126.20 x 65.70 pattern. All four sit
# clear of the PCB in X, but they are ~17mm inboard of the walls, so the cover
# reaches them with columns rather than a flange.
M3_X = (-61.60, 64.60)
M3_Z = (-32.80, 32.90)

# The PCB is flush with the bracket's bottom edge and the BOOT/RESET/UART
# switches overhang it by another 0.45mm. This is the one dimension that
# decides the cover's outline: the bottom wall has to sit below it.
PCB_BOTTOM_Z = -46.87

# PH2.0 terminal bank: power, UART, I2C, CAN, RS485, sensor. Six connectors in
# a vertical column at the right edge, all mating rearward (-Y).
PH_Z0, PH_Z1 = -22.95, 23.91
PH_TIP_Y = -17.60               # rear-most point on the whole assembly

# Both USB-C ports face -X off the left edge of the PCB. One of them is the
# flashing port, so it is never allowed to end up sealed in.
USB_X_FACE = -55.03
USB_Z0, USB_Z1 = -14.99, 9.05
USB_Y0, USB_Y1 = -12.55, -8.49

# microSD opens upward (+Z) at the PCB's top edge. Recorded as a panel fact
# only: no opening is cut for it, see build().
SD_X0, SD_X1 = -44.32, -28.22

# BOOT / RESET / UART-select, all facing down off the bottom edge.
SW_X0, SW_X1 = -10.03, 19.26

# ------------------------------------------------------------------- the part
WALL   = 2.5
FLOOR  = 3.0                    # rear wall, thicker because the arm hangs off it
CLEAR  = 1.6                    # gap between an inner wall face and the PCB

# Cavity depth. Budget, working rearward from the connector tips:
#   -17.60  PH2.0 mating face
#   -21.0   mated JST PHR housing
#   -28.0   room for the wire to turn 90 deg toward the side exits
CAVITY_BACK_Y = -28.0
BACK_Y = CAVITY_BACK_Y - FLOOR  # outer rear face, -31.0

BOTTOM_Z = PCB_BOTTOM_Z - CLEAR - WALL     # -51.0, still 3.4mm inside the glass
# MEASURED off the vendor STEP: the glass corner arcs are R7.88, centred at
# (88.6, 48.5), reaching exactly X 96.48 / Z 56.38 -- the glass outline.
#
# This was R4. A squarer corner carries MORE material than a rounder one, so at
# R4 the cover stood proud of the glass by ~1.2mm diagonally at all four
# corners. An inward offset reduces a convex radius by the offset, so matching
# the 0.3mm inset everywhere means 7.88 - 0.3.
CORNER_R = 7.58

# Cover-to-bracket screws: M3x25 socket cap, head fully sunk in the rear face.
M3_COL_D    = 9.0
M3_BORE_D   = 3.4
M3_CBORE_D  = 6.2

# VESA 75, centred on the glass (Z centre 1.02), not on the cover -- the cover
# is asymmetric because of the bottom extension, the display is not.
VESA_PITCH  = 75.0
VESA_CX, VESA_CZ = 0.0, 1.02
VESA_BOSS_D = 11.0
VESA_BOSS_H = 9.0
# 3.3 = tap straight into the plastic for M4. Drill to 5.6 if you would rather
# melt in an M4 heat-set insert, which is the better joint for a panel on an arm.
VESA_HOLE_D = 3.3
VESA_HOLE_DEPTH = 11.0

# Cable exit. One slot through the REAR wall rather than holes in the side, so
# the loom leaves pointing back and down -- straight at the arm and the table,
# which is where it is going anyway. Out of the side it stuck into free space
# beside the panel with nothing to follow, which is what snags.
#
# The slot lives in the corridor right of the VESA plate (which covers X -50..50)
# and between the two M3 columns (X 64.6, Z +/-32.8), so it needs no thin walls.
# Moved to the BOTTOM WALL, centred. It used to be a rear-face slot at X 66 --
# the corridor right of the VESA plate -- but exiting downward puts the loom
# straight at the arm without a rearward loop, and the bottom edge is clear of
# the VESA plate entirely (that plate covers X -50..50, Z -48.98..51.02, so a
# rear-face slot at bottom-middle would have opened behind it).
CABLE_CX = 0.0                  # centred
CABLE_W  = 11.0                 # across the wall, in Y
CABLE_SPAN = 30.0               # along X

# ============================================================================
# REV 2 -- full back, battery bay, hardwired link, restart button.
# See docs/superpowers/specs/2026-08-12-enclosure-rev2-design.md
# ============================================================================

# The cover now matches the GLASS, not the bracket. The script never recorded
# the glass origin, but it does state the VESA pattern is centred on the glass
# and gives that centre, which fixes the outline:
GLASS_CX, GLASS_CZ = VESA_CX, VESA_CZ        # 0.00, 1.02
GLASS_W,  GLASS_H  = 192.96, 110.76
EDGE_INSET = 0.3            # so print tolerance cannot leave a lip proud of the glass

COVER_X0 = GLASS_CX - GLASS_W / 2.0 + EDGE_INSET   # -96.18
COVER_X1 = GLASS_CX + GLASS_W / 2.0 - EDGE_INSET   #  96.18
COVER_Z0 = GLASS_CZ - GLASS_H / 2.0 + EDGE_INSET   # -54.06
COVER_Z1 = GLASS_CZ + GLASS_H / 2.0 - EDGE_INSET   #  56.10

# The rim still seats on the bracket's back face. Pushing the outboard skirt
# further forward to actually meet the glass needs the glass BACK face Y, which
# is not recorded anywhere -- so it is deliberately not attempted here.

# Port scallops. Moving the walls out to the glass moves them away from the
# ports behind them: USB-C reach would go 23mm -> ~38mm, which defeats a normal
# overmould on what is also the flashing port. So the wall is locally recessed
# back to roughly where it used to be.
SCALLOP_PAD = 6.0

# --- glass support lip -----------------------------------------------------
# The glass overhangs the bracket by 15.4mm left, 12.1 right, 5.2 top, 7.3
# bottom, and until now nothing came forward to meet it -- the rim stopped at
# the bracket plane, leaving that edge cantilevered over a void. A lip runs
# around the inside of the outer wall to just behind the glass, so the extreme
# edge has something to bear on.
#
# It does NOT clamp the glass. GLASS_GAP leaves it standing off, so a knock is
# caught after a fraction of a millimetre of flex rather than the glass being
# preloaded against a rigid plastic edge, which is how printed enclosures crack
# panels.
#
# MEASURED off the vendor STEP: the touchscreen spans Y -0.20..+0.80 at the
# panel edge, so the glass is 1.0 thick with its back face at -0.20. (This was
# assumed to be 1.1 until the model was actually loaded -- close, and wrong in
# the safe direction, but no longer a guess.)
GLASS_THICK  = 1.0
GLASS_FRONT_Y = 0.80
GLASS_BACK_Y = GLASS_FRONT_Y - GLASS_THICK
GLASS_GAP    = 0.4
SUPPORT_Y    = GLASS_BACK_Y - GLASS_GAP

# 3.0 against a 2.5 wall leaves the lip overhanging the cavity by only 0.5mm,
# which prints clean with the rear face on the bed -- a full shelf reaching in
# to the bracket would be a 15mm unsupported overhang.
#
# It was 4.0 until the interference check ran against the vendor STEP: at that
# width the top lip's inner edge landed at Z 52.10 and fouled something standing
# proud behind the glass at Z 52.10..52.89, for 16.8 mm3. 3.0 puts the edge at
# 53.10 and still bears on the outer 3mm of glass all the way round.
LIP_W = 3.0

# --- battery: DROPPED ------------------------------------------------------
# A UPS bay was designed here and then removed. The cell's leads are only 3"
# long, and the cell could not be placed anywhere that both reached the board's
# battery header AND left room for the leads to turn at its end -- at every such
# position its Ø19 bore intersected the adapter board. The left strip is the
# only region whose depth is confirmed clear, and it cannot hold both.
#
# Consequences, all good: the 6mm bump-out goes, depth returns to 23.3mm behind
# the bracket, the whole left strip frees up for the adapter, and a sealed
# li-ion cell no longer sits in an unventilated box next to the ESP32.

# --- restart switch --------------------------------------------------------
# 6.66mm hole, 14mm flush-face to prong tips, in the REAR FACE at the upper
# corner. The 14mm body runs forward into the cavity, which is 20.3mm deep here
# -- and this spot is outboard of USB_X_FACE = -55.03, so there is no PCB behind
# it and that depth is genuinely clear rather than nominally so.
#
# Clear of the M3 column at X 64.6 / Z 32.9, the loom bridge (X 57..75,
# Z 18..24) and the side-wall vents, which start at X 91.18.
SW_HOLE_D  = 6.9            # 6.66 nominal + fit
SW_CX      = 82.0    # right as you face the SCREEN; clear of the wall vents at X 91.18
SW_CZ      = 47.0
SW_CBORE_D = 14.0           # recessed so a knock lands on plastic, not the plunger
SW_CBORE_T = 1.5

# --- vents -----------------------------------------------------------------
# The README already flagged no ventilation around an ESP32-S3 driving a
# 1024x600 panel. A sealed li-ion cell makes that a safety question, so vents
# are part of this revision rather than a later fix. Walls, not the top face,
# so chips cannot drop straight in.
VENT_W, VENT_H, VENT_GAP = 3.0, 14.0, 5.0
# Slots must stay on the STRAIGHT part of the wall. The first version centred
# each group 12mm from the end and ran five slots at 8mm pitch, which spanned
# past the cover edge and cut straight through the R4 corner fillet -- a ragged
# hole wrapping the corner. Keep clear of the radius by a whole slot width.
VENT_Z_MARGIN = CORNER_R + VENT_W

# --- fan seat + grill ------------------------------------------------------
# Not a knockout. A recessed seat with a grill already cut through it: today it
# is simply more ventilation, and when a 20mm fan turns up it drops into the
# recess and the grill becomes its guard. Nothing to snap, nothing to trim, and
# no reprint either way -- which is what the knockout was for, achieved without
# leaving a disc of dead plastic in the wall doing nothing in the meantime.
#
# The fan is held by the seat walls, not by bosses. Screw holes would commit to
# the hole pattern of a fan nobody has bought, and a boss in the wrong place
# cannot be un-printed.
# The seat is on the INSIDE. A 20mm fan is ~10mm thick against a 3mm wall, so
# seated from the outside it would stand ~8mm proud of the back -- exposed on a
# machine, and only able to exhaust the shell. Inside it sits in 20.3mm of clear
# cavity, protected, moving air over the board itself.
FAN_SEAT_W     = 20.4        # 20mm fan + 0.4 so it seats without forcing
FAN_FLOOR_T    = 1.0         # grill floor left at the OUTER face
FAN_GRILL_D    = 18.0        # airflow opening of a 20mm fan
GRILL_SLOT_W   = 3.0
GRILL_WEB      = 1.2         # 1.2 on a 1.0 floor is still a guard, and ~71% open
FAN_CX, FAN_CZ = -80.0, 32.0

# --- loom bridge -----------------------------------------------------------
# The 11x30 slot has carried the loom since rev 1 with nothing holding it, so a
# tug on the loom outside goes straight to the PH2.0 plugs inside. A moulded
# bridge just inboard of the slot fixes that with no hardware -- the same thing
# the arm already does for its cable tray, where two bridges hold the loom
# captive without a zip tie.
#
# The passage has to swallow a MATED PH2.0 plug (8 x 4.5 per the README) or the
# loom cannot be threaded after the plugs are on.
BRIDGE_X0, BRIDGE_X1 = 57.0, 75.0
BRIDGE_Z0, BRIDGE_Z1 = 18.0, 24.0          # above the slot, clear of the M3 column at Z 32.9
BRIDGE_GAP_W = 10.0                        # > 8mm plug width
BRIDGE_GAP_H = 8.0                         # > 4.5mm plug height
BRIDGE_BAR_T = 4.0

# --- cable strain relief ---------------------------------------------------
# The link cable is hardwired onto the adapter's terminal block, so a pull on
# it must never reach those joints. A knot grips only the jacket and lets the
# conductors creep inside it; a screwed saddle would be a second printed part
# and two more fasteners.
#
# Instead: a serpentine of three posts just inboard of the cable slot. The
# cable weaves between them and is captive against a straight pull with no
# hardware at all -- the same trick the arm already uses for its loom, where
# two moulded bridges hold the cable without a zip tie.
#
# CABLE_OD is UNVERIFIED: 6.0 is a common jacketed 6-core, but the real cable
# has not been measured. The weave gap is derived from it, so setting it right
# is the only change needed if the cable differs.
# MEASURED: the link cable is 4.2-4.8mm over the jacket. The two ends of that
# range pull opposite ways, so each feature is derived from the end that governs
# it -- the bore has to pass the fattest cable, the weave has to still grip the
# thinnest. Nothing here is typed in twice; change the two numbers and the rest
# follows.
CABLE_OD_MIN, CABLE_OD_MAX = 4.2, 4.8

RELIEF_POST_D = 6.0
RELIEF_H      = 8.0                    # stops short of the PH2.0 tips at Y -17.60

# Sliding fit on the fattest cable, so it threads without forcing.
CABLE_HOLE_D  = CABLE_OD_MAX + 1.0     # 5.8
CABLE_ENTRY_X = 48.0                   # clear of the loom slot, which starts at 60.5
CABLE_ENTRY_Z = -10.0

# The S-bend does the gripping, and the gap between the posts must therefore be
# NARROWER than the cable -- otherwise the cable runs dead straight between them
# and the posts are decoration. The first attempt used
# post_R + cable/2 + slack, which left a 6.2mm channel for a 4.2mm cable: a
# straight-rod test drove clean through it. Verified now by that same test.
#
# 80% of the thinnest cable: enough to deform a jacket into a press fit without
# squeezing the conductors. A 4.8mm cable sits at ~70%, firmer still.
RELIEF_CZ     = CABLE_ENTRY_Z
RELIEF_GAP    = 0.8 * CABLE_OD_MIN
RELIEF_OFFSET = (RELIEF_POST_D + RELIEF_GAP) / 2.0
RELIEF_SPACING = 3.0 * CABLE_OD_MAX    # ~3 diameters between posts to bend around
RELIEF_X      = (CABLE_ENTRY_X - RELIEF_SPACING * 1.0,
                 CABLE_ENTRY_X - RELIEF_SPACING * 2.0,
                 CABLE_ENTRY_X - RELIEF_SPACING * 3.0)
RELIEF_Z      = (RELIEF_CZ - RELIEF_OFFSET,
                 RELIEF_CZ + RELIEF_OFFSET,
                 RELIEF_CZ - RELIEF_OFFSET)


def box(x0, x1, y0, y1, z0, z1):
    return Part.makeBox(x1 - x0, y1 - y0, z1 - z0, App.Vector(x0, y0, z0))


def tube_y(x, z, dia, y0, y1):
    """Cylinder with its axis along Y."""
    return Part.makeCylinder(dia / 2.0, y1 - y0, App.Vector(x, y0, z),
                             App.Vector(0, 1, 0))


def tube_z(x, z0, z1, dia, y):
    """Cylinder with its axis along Z -- the cell cradle."""
    return Part.makeCylinder(dia / 2.0, z1 - z0, App.Vector(x, y, z0),
                             App.Vector(0, 0, 1))


def tube_x(x0, x1, dia, y, z):
    """Cylinder with its axis along X -- the switch bore."""
    return Part.makeCylinder(dia / 2.0, x1 - x0, App.Vector(x0, y, z),
                             App.Vector(1, 0, 0))


# Real helical threads in the VESA holes. OFF, and not only for speed: in
# FreeCAD 1.1.3 / OCC 7.8 the rib-into-shell fuse fails -- straight into the
# filleted shell it returns a null shape, and via an intermediate sleeve it
# "succeeds" while quietly shredding the shell into fragments (9 solids,
# 2.3cm3). Until OCC can do this boolean, the tap-drill hole IS the model;
# the thread is cut by the tap, and nothing in the assembly can see it anyway.
MODEL_THREADS = False

# M4 x 0.7 coarse. ISO thread height is 0.541*P, and 3.3 was the tap drill --
# with a real thread modelled the bore opens to the major diameter and the
# ridges bring it back in.
THREAD_MAJOR, THREAD_PITCH = 4.0, 0.7


def thread_ribs(cx, cz, y0, y1, major=THREAD_MAJOR, pitch=THREAD_PITCH):
    """Helical ridges for an internal thread, to FUSE into a bore of `major`.

    Built along Z because makeHelix is, then laid over onto the Y axis. Returns
    None if the sweep fails rather than silently producing a plain hole -- a
    thread that quietly did not happen is worse than an error.
    """
    length = y1 - y0
    depth = 0.541 * pitch
    try:
        # The ridge starts OUTSIDE the bore wall by BITE. Landing it exactly on
        # the wall makes the fuse a tangency, and OCC returns a null shape for
        # that -- which is how this first failed.
        bite = 0.15
        r_out = major / 2.0 + bite
        helix = Part.makeHelix(pitch, length + pitch, r_out)
        prof = Part.makePolygon([
            App.Vector(r_out, 0, -pitch / 2.0),
            App.Vector(r_out, 0, pitch / 2.0),
            App.Vector(r_out - depth - bite, 0, 0),
            App.Vector(r_out, 0, -pitch / 2.0)])
        sweep = Part.Wire(helix.Edges).makePipeShell([prof], True, True)
        sweep.rotate(App.Vector(0, 0, 0), App.Vector(1, 0, 0), -90)
        sweep.translate(App.Vector(cx, y0, cz))
        if sweep.isNull() or not sweep.Solids:
            return None
        return sweep
    except Exception as e:
        print("[thread] sweep failed: %s" % e)
        return None


def fillet_verticals(shape, r):
    """Round every edge running along Y, i.e. the upright corners.

    The cavity and the lip pocket are cut with plain boxes, which leaves sharp
    INTERNAL corners inside rounded external ones -- so the wall goes from 2.5mm
    on the flats to nearly 5mm diagonally at each corner. Rounding the cutter by
    the outer radius less the wall keeps the shell one thickness the whole way
    round.
    """
    if r <= 0:
        return shape
    edges = [e for e in shape.Edges
             if abs(e.tangentAt(e.FirstParameter).y) > 0.99]
    return shape.makeFillet(r, edges) if edges else shape


def build():
    ix0, ix1 = COVER_X0 + WALL, COVER_X1 - WALL
    iz0, iz1 = COVER_Z0 + WALL, COVER_Z1 - WALL

    shell = box(COVER_X0, COVER_X1, BACK_Y, SUPPORT_Y, COVER_Z0, COVER_Z1)

    # Round the four upright corners before any hole exists, so the fillet has
    # simple edges to work with.
    verticals = [e for e in shell.Edges
                 if abs(e.tangentAt(e.FirstParameter).y) > 0.99]
    shell = shell.makeFillet(CORNER_R, verticals)

    # Hollow forward of the bracket plane, leaving only the lip standing proud.
    shell = shell.cut(fillet_verticals(
        box(COVER_X0 + LIP_W, COVER_X1 - LIP_W,
            BRACKET_BACK_Y, SUPPORT_Y + 1,
            COVER_Z0 + LIP_W, COVER_Z1 - LIP_W), CORNER_R - LIP_W))

    # Hollow it out. Open at the front (+Y), where the panel goes.
    shell = shell.cut(fillet_verticals(
        box(ix0, ix1, CAVITY_BACK_Y, BRACKET_BACK_Y + 1, iz0, iz1),
        CORNER_R - WALL))


    # --- columns out to the bracket's M3 holes ---
    for x in M3_X:
        for z in M3_Z:
            shell = shell.fuse(tube_y(x, z, M3_COL_D, CAVITY_BACK_Y, BRACKET_BACK_Y))

    # --- VESA bosses, standing off the inside of the rear wall ---
    for dx in (-1, 1):
        for dz in (-1, 1):
            x = VESA_CX + dx * VESA_PITCH / 2.0
            z = VESA_CZ + dz * VESA_PITCH / 2.0
            shell = shell.fuse(tube_y(x, z, VESA_BOSS_D,
                                      CAVITY_BACK_Y, CAVITY_BACK_Y + VESA_BOSS_H))

    shell = shell.removeSplitter()

    # --- now the holes ---
    for x in M3_X:
        for z in M3_Z:
            shell = shell.cut(tube_y(x, z, M3_BORE_D, BACK_Y - 1, BRACKET_BACK_Y + 1))
            shell = shell.cut(tube_y(x, z, M3_CBORE_D, BACK_Y - 1, CAVITY_BACK_Y))

    for dx in (-1, 1):
        for dz in (-1, 1):
            x = VESA_CX + dx * VESA_PITCH / 2.0
            z = VESA_CZ + dz * VESA_PITCH / 2.0
            if MODEL_THREADS:
                shell = shell.cut(tube_y(x, z, THREAD_MAJOR,
                                         BACK_Y - 1, BACK_Y + VESA_HOLE_DEPTH))
                ribs = thread_ribs(x, z, BACK_Y, BACK_Y + VESA_HOLE_DEPTH)
                if ribs is None:
                    raise RuntimeError("thread sweep failed at %.1f,%.1f" % (x, z))
                # Fusing the helix straight into the filleted shell returns a
                # null shape from OCC. Fuse it into a plain sleeve first: the
                # sleeve-into-shell fuse is then a simple volume overlap, which
                # is robust, and the helix only ever meets a clean cylinder.
                sleeve = tube_y(x, z, THREAD_MAJOR + 2.0,
                                BACK_Y, BACK_Y + VESA_HOLE_DEPTH)
                sleeve = sleeve.cut(tube_y(x, z, THREAD_MAJOR,
                                           BACK_Y - 1, BACK_Y + VESA_HOLE_DEPTH + 1))
                shell = shell.fuse(sleeve.fuse(ribs))
            else:
                shell = shell.cut(tube_y(x, z, VESA_HOLE_D,
                                         BACK_Y - 1, BACK_Y + VESA_HOLE_DEPTH))

    # Loom exit through the bottom wall: a stadium so there is no corner for a
    # wire to sit on, running along X and centred.
    y_mid = -18.5
    z0, z1 = COVER_Z0 - 1, COVER_Z0 + WALL + 1
    for dx in (-1, 1):
        shell = shell.cut(Part.makeCylinder(
            CABLE_W / 2.0, z1 - z0,
            App.Vector(CABLE_CX + dx * (CABLE_SPAN - CABLE_W) / 2.0, y_mid, z0),
            App.Vector(0, 0, 1)))
    shell = shell.cut(box(CABLE_CX - (CABLE_SPAN - CABLE_W) / 2.0,
                          CABLE_CX + (CABLE_SPAN - CABLE_W) / 2.0,
                          y_mid - CABLE_W / 2.0, y_mid + CABLE_W / 2.0, z0, z1))

    # USB-C. One slot covering both ports: a plug has to reach 23mm inboard to
    # find the socket, so the opening is sized for the overmould, not the shell.
    # A PLAIN SLOT, not a notch in the outline. Once the panel is on the machine
    # the USB-C ports go unused -- not even for power -- so there is no reason to
    # spend the outline on reaching them. If one is ever needed it is a short
    # pigtail pushed down the slot plus an extension, which is a five-minute
    # inconvenience rather than something to shape the enclosure around.
    #
    # Deleting the notch also makes the left edge continuous, which means the
    # glass support lip runs unbroken down that whole side instead of stopping
    # either end of a 36mm gap.
    shell = shell.cut(box(COVER_X0 - 1, COVER_X0 + WALL + 1,
                          USB_Y0 - 5.0, USB_Y1 + 4.0,
                          USB_Z0 - 4.0, USB_Z1 + 4.0))

    # NO microSD opening. The card slot is not used -- G-code comes over the
    # link -- so the top wall stays closed. That keeps the outline unbroken
    # along the top and, with it, the glass support lip: the step-down used to
    # cut a 40mm gap out of both. It also retires the rev 1 caveat that the
    # card sat 22mm down its own hole and needed tweezers.

    # BOOT / RESET / UART-select slot REMOVED. It occupied X -13..22 in this
    # same wall and would have merged with the loom exit into one ragged
    # opening. Its only purpose was manual flashing over USB-C, which is not
    # being used -- see the USB slot note above. Easily restored if that changes.

    # --- rev 2 ---------------------------------------------------------


    # Restart switch, through the rear face, recessed so a knock lands on
    # plastic rather than on the plunger.
    shell = shell.cut(tube_y(SW_CX, SW_CZ, SW_HOLE_D, BACK_Y - 1, CAVITY_BACK_Y + 1))
    shell = shell.cut(tube_y(SW_CX, SW_CZ, SW_CBORE_D, BACK_Y - 1, BACK_Y + SW_CBORE_T))

    # NO adapter bosses. The RJ12 board is not being used, so the cable lands
    # straight on a PH2.0 pigtail to the UART header. That drops the board's
    # TVS protection on the UART pair -- the strain relief below matters more
    # for it, since nothing else now stands between the cable and the joints.

    # Dedicated entry for the hardwired link cable, edge broken both sides.
    shell = shell.cut(tube_y(CABLE_ENTRY_X, CABLE_ENTRY_Z, CABLE_HOLE_D,
                             BACK_Y - 1, CAVITY_BACK_Y + 1))
    for yc, d0, d1 in ((BACK_Y, CABLE_HOLE_D + 2.0, CABLE_HOLE_D),
                       (CAVITY_BACK_Y, CABLE_HOLE_D, CABLE_HOLE_D + 2.0)):
        shell = shell.cut(Part.makeCone(d0 / 2.0, d1 / 2.0, 1.0,
                                        App.Vector(CABLE_ENTRY_X, yc, CABLE_ENTRY_Z),
                                        App.Vector(0, 1, 0)))

    # Cable strain relief: three posts inboard of the slot, cable weaving
    # between them. Captive against a straight pull, no fastener.
    for px, pz in zip(RELIEF_X, RELIEF_Z):
        shell = shell.fuse(tube_y(px, pz, RELIEF_POST_D,
                                  CAVITY_BACK_Y, CAVITY_BACK_Y + RELIEF_H))

    # Loom bridge: thread the loom under it and it is captive, no zip tie.
    bx = (BRIDGE_X0 + BRIDGE_X1) / 2.0
    shell = shell.fuse(box(BRIDGE_X0, BRIDGE_X1,
                           CAVITY_BACK_Y, CAVITY_BACK_Y + BRIDGE_GAP_H + BRIDGE_BAR_T,
                           BRIDGE_Z0, BRIDGE_Z1))
    shell = shell.cut(box(bx - BRIDGE_GAP_W / 2.0, bx + BRIDGE_GAP_W / 2.0,
                          CAVITY_BACK_Y - 1, CAVITY_BACK_Y + BRIDGE_GAP_H,
                          BRIDGE_Z0 - 1, BRIDGE_Z1 + 1))

    # Fan seat: pocket in the INNER face, so the fan lives in the cavity.
    shell = shell.cut(box(FAN_CX - FAN_SEAT_W / 2.0, FAN_CX + FAN_SEAT_W / 2.0,
                          BACK_Y + FAN_FLOOR_T, CAVITY_BACK_Y + 1,
                          FAN_CZ - FAN_SEAT_W / 2.0, FAN_CZ + FAN_SEAT_W / 2.0))

    # Grill through the floor of the seat: parallel slots, clipped to the round
    # airflow opening so the webs meet solid wall at both ends rather than
    # running out into the corners of the pocket.
    pitch = GRILL_SLOT_W + GRILL_WEB
    n = int(FAN_GRILL_D // pitch)
    slots = None
    for i in range(n):
        cx = FAN_CX - (n - 1) * pitch / 2.0 + i * pitch
        b = box(cx - GRILL_SLOT_W / 2.0, cx + GRILL_SLOT_W / 2.0,
                BACK_Y - 1, BACK_Y + FAN_FLOOR_T + 0.1,
                FAN_CZ - FAN_GRILL_D, FAN_CZ + FAN_GRILL_D)
        slots = b if slots is None else slots.fuse(b)
    slots = slots.common(tube_y(FAN_CX, FAN_CZ, FAN_GRILL_D,
                                BACK_Y - 1, BACK_Y + FAN_FLOOR_T + 0.1))
    shell = shell.cut(slots)

    # Vents: low inlets, high outlets, in the side walls only.
    pitch = VENT_W + VENT_GAP
    z_lo = COVER_Z0 + VENT_Z_MARGIN
    z_hi = COVER_Z1 - VENT_Z_MARGIN - VENT_W
    n = 5
    for base in (z_lo, z_hi - (n - 1) * pitch):
        for i in range(n):
            zz = base + i * pitch
            if zz < z_lo or zz + VENT_W > z_hi + VENT_W:
                continue
            shell = shell.cut(box(COVER_X1 - WALL - 1, COVER_X1 + 1,
                                  -24.0, -24.0 + VENT_H, zz, zz + VENT_W))

    # removeSplitter() chokes on the helical thread faces ("Bnd_Box is void").
    # It only merges cosmetic seam faces, so losing it costs nothing real.
    try:
        return shell.removeSplitter()
    except Exception as e:
        print("[refine] skipped: %s" % e)
        return shell


doc_name = "WS7B_back_cover"
if doc_name in App.listDocuments():
    App.closeDocument(doc_name)
doc = App.newDocument(doc_name)
obj = doc.addObject("Part::Feature", "BackCover")
obj.Shape = build()
doc.recompute()

here = os.path.dirname(os.path.abspath(__file__)) if "__file__" in dir() else "cad"
doc.saveAs(os.path.join(here, "back_cover.FCStd"))
obj.Shape.exportStep(os.path.join(here, "back_cover.step"))
import Mesh
Mesh.export([obj], os.path.join(here, "back_cover.stl"))
