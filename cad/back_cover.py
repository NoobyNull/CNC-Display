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

# microSD opens upward (+Z) at the PCB's top edge -- the G-code source.
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
CORNER_R = 4.0

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

# Cable exits, in the right wall next to the terminal bank.
CABLE_D = 8.0
CABLE_Y = -22.0                 # level with where the wires leave a mated plug
CABLE_Z = (-14.0, 0.0, 14.0)    # power / UART / I2C, spread over the bank


def box(x0, x1, y0, y1, z0, z1):
    return Part.makeBox(x1 - x0, y1 - y0, z1 - z0, App.Vector(x0, y0, z0))


def tube_y(x, z, dia, y0, y1):
    """Cylinder with its axis along Y."""
    return Part.makeCylinder(dia / 2.0, y1 - y0, App.Vector(x, y0, z),
                             App.Vector(0, 1, 0))


def build():
    ix0, ix1 = BRACKET_X0 + WALL, BRACKET_X1 - WALL
    iz0, iz1 = BOTTOM_Z + WALL, BRACKET_Z1 - WALL

    shell = box(BRACKET_X0, BRACKET_X1, BACK_Y, BRACKET_BACK_Y, BOTTOM_Z, BRACKET_Z1)

    # Round the four upright corners before any hole exists, so the fillet has
    # simple edges to work with.
    verticals = [e for e in shell.Edges
                 if abs(e.tangentAt(e.FirstParameter).y) > 0.99]
    shell = shell.makeFillet(CORNER_R, verticals)

    # Hollow it out. Open at the front (+Y), where the panel goes.
    shell = shell.cut(box(ix0, ix1, CAVITY_BACK_Y, BRACKET_BACK_Y + 1, iz0, iz1))

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
            shell = shell.cut(tube_y(x, z, VESA_HOLE_D,
                                     BACK_Y - 1, BACK_Y + VESA_HOLE_DEPTH))

    # Cable exits for power / UART / I2C.
    for z in CABLE_Z:
        shell = shell.cut(Part.makeCylinder(
            CABLE_D / 2.0, WALL + 2, App.Vector(ix1 - 1, CABLE_Y, z),
            App.Vector(1, 0, 0)))

    # USB-C. One slot covering both ports: a plug has to reach 23mm inboard to
    # find the socket, so the opening is sized for the overmould, not the shell.
    shell = shell.cut(box(BRACKET_X0 - 1, ix0 + 1,
                          USB_Y0 - 5.0, USB_Y1 + 4.0,
                          USB_Z0 - 4.0, USB_Z1 + 4.0))

    # microSD access through the top wall. See the note in the report: the card
    # sits 22mm down this hole.
    shell = shell.cut(box(SD_X0 - 3.0, SD_X1 + 3.0, -17.0, -5.0,
                          iz1 - 1, BRACKET_Z1 + 1))

    # BOOT / RESET / UART-select slot in the bottom wall.
    shell = shell.cut(box(SW_X0 - 3.0, SW_X1 + 3.0, -15.0, -5.0,
                          BOTTOM_Z - 1, iz0 + 1))

    return shell.removeSplitter()


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
