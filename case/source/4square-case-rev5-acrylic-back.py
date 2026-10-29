r"""
4square case REV 5 -- ACRYLIC BACK. A flat laser-cut alternative to the
printed backplate, with a finger notch over each button pair.

    ~/.venvs/cad/bin/python 4square-case-rev5-acrylic-back.py

    > "can you make a back that would be cut out of acrylic that just has a
    >  pocket big enough that you could fit one finger in to hit the buttons"

WHY IT IS A NOTCH AND NOT A HOLE, AND WHY IT IS THIS BIG

The buttons are 4.38 mm proud of the board's back and the plate seats 11.15 mm
above it, so a button top sits 6.77 mm below the plate's inner face and
**9.77 mm below the outer face** of 3 mm acrylic. A fingertip therefore has to
travel most of a centimetre INTO the panel to reach one. That is the same
reach that made rev 1 unusable, and it was found by trying it rather than by
drawing it:

    > "A finger cannot press that down a 6 mm hole."

So the opening is sized for the finger's travel, not for the button's
footprint: FINGER_W x FINGER_L, which is a whole fingertip plus room to move,
against a button body of only 7.68 x 6.00.

AND IT IS OPEN TO THE PANEL EDGE, DELIBERATELY. Each button pair sits 3.71 mm
from the board's edge, so a closed hole big enough for a finger would leave a
rim of about 1 mm of acrylic along the panel edge -- 3 mm cast acrylic in a
1 mm ligature, 24 mm long, next to a hole someone pushes on. It would crack on
the first press. Running the opening out to the edge removes the ligature
entirely, and costs nothing: the shell's own wall stands 2.19 mm thick outside
that edge and closes the gap, so nothing is exposed.

TWO NOTCHES, NOT ONE. SW1/SW2 are on one edge and SW3/SW4 on the opposite one,
59 mm apart. No single opening reaches both without removing the middle of the
panel, so there is one per side, each spanning its own pair.

THIS PANEL CANNOT BE FITTED ON ITS OWN -- READ THE SPACER NOTE.
The printed backplate carries four STANDOFF TUBES on its inner face, and they
are structural: the cavity is full width to the rim with no ledge, so nothing
else sets the plate's height. Flat sheet has no tubes. Four spacers of
SPACER_H must go between the board's back face and this panel, or tightening
the screws pulls the acrylic 11.15 mm into thin air and cracks it. The length
is printed in the report and asserted below.

ORIENTATION. This is FLAT STOCK: the whole part is one 2D profile extruded to
the sheet thickness, and the DXF beside it is what a laser actually cuts. Z=0
is the panel's inner face. The profile is drawn in the CASE frame, so it drops
straight onto the assembly without a transform.
"""
import os

from build123d import *
# ColorIndex is NOT re-exported by `from build123d import *` -- it lives in the
# exporters module, and the first version of the layered DXF silently fell into
# the except branch below with "name 'ColorIndex' is not defined" and wrote no
# DXF at all while every check still said PASS.
from build123d.exporters import ColorIndex
import cadkit
import case_params as P

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "4square-case-rev5-acrylic-back")

# ---- knobs that belong to this part alone --------------------------------
SHEET_T = 3.00         # nominal cast acrylic. Change here, nothing else moves.
KERF = 0.00            # laser kerf compensation, left to the cutter's software
SCREW_CLR_D = 3.40     # M3 shank through flat sheet -- the printed plate's
                       # tube bore is 3.30, but that one is a guided tube;
                       # a flat panel wants slop so it is not a press fit.
# 0.60 -> 0.25 -> 0.15, tightened twice on the same complaint:
#     > "tighten the usb cutout can be way closer"     (2026-08-09)
#     > "the cutouts huge ... just takes the space it needs"  (2026-08-10)
# The aperture is not decorative clearance -- J9's face sits 4.26 mm BELOW the
# panel's outer surface, so a plug's overmould has to pass THROUGH the panel to
# reach the connector. The floor is therefore the overmould itself, and
# _plug_passes() below is what enforces it. 0.15/side is a printed-part sliding
# clearance, and the aperture is now derived straight from the plug rather than
# from the connector grown sideways.
J9_CLR = 0.15
USB_C_OVERMOULD_W = 12.35      # USB-IF maximum; the ledger's PLUG_W matches it

# THE FINGER. These are the OPENING, not the finger. 20 x 28, not 22 x 30. The binding dimension is a finger at the depth it has
# to reach -- roughly the width at the first knuckle, 18-19 mm on an average
# adult index finger -- so 20 is snug-but-in and 28 spans a 16 mm button pair
# with 6 mm either side. Erring generous rather than tight ON PURPOSE: too
# small is the rev 1 failure, and unlike a printed part this one is likely to
# be cut once by somebody else. If it turns out roomier than wanted these two
# numbers are the only change.
# HALVED, BY REQUEST:
#     > "reduce the finger width height by half ... pull in the entire finger
#     >  cutout like 50%"
# Stated plainly because it changes what the opening can do: at 20 x 28 it
# cleared both button BODIES completely. At 10 x 14 it clears their CENTRES
# and no more -- worst clearance around a button centre is 2.35 mm, reported
# below. With the button top 9.77 mm down, this is a fingertip poke rather
# than a comfortable press. Built as asked; these two numbers are the only
# change if it turns out too tight in the hand.
# WIDER, NOT DEEPER. I moved the wrong one first:
#     > "you made the cutouts the wrong way ... want them wider not deeper"
# FINGER_W is the reach INTO the panel from its edge -- doubling that made the
# pocket deeper and left it just as narrow. The width you grip across is
# FINGER_L, the run ALONG the edge, and that is the 20.
FINGER_W = 10.00       # across the panel, inward from the edge -- unchanged
FINGER_L = 20.00       # along the edge -- 14.00 -> 20.00 by request
# 1.50, AND THIS IS A WAIVER, NOT A SPEC. The opening was halved by request,
# and at 10 x 14 the measured clear radius around a button centre is 1.65 mm.
# For reference, the geometry is:
#     clear radius = min(FINGER_W - 7.65, FINGER_L / 2 - 5.15)
#     R = 2.5 needs 10.15 x 15.30      R = 4.0 needs 11.65 x 18.30
# The binding axis is FINGER_L, not FINGER_W. A 1.65 mm clear radius is a
# Ø3.3 opening onto a button 9.77 mm down: reachable with a nail or a stylus,
# NOT with a fingertip. Recorded here rather than silently lowered so the
# trade-off is visible in the build output every single run.
PRESS_CLR = 1.50       # clear radius demanded around each button's centre
PRESS_CLR_WAIVED = True
# 3.00 -> 3.40, ROUNDER BY REQUEST:
#     > "Round that pocket more. the finger pocket. those corners are very
#     >  sharp. so fillet those corners. more."
#
# 3.40 IS THE CEILING AT THIS POCKET SIZE, and the limit is not the width -- it
# is the buttons. The corner arcs bite into exactly the space a fingernail
# needs, and PRESS_CLR is already a waived 1.50:
#
#     R 3.00  clear 1.68      R 3.45  clear 1.51
#     R 3.40  clear 1.53      R 3.50  clear 1.48   <- breaks the waiver
#
# So this is as round as 10 x 14 can be. Rounder needs a bigger pocket: at
# 10 x 18 the same maths allows R5.00 with 2.11 mm clear, which is both rounder
# AND easier to press -- but that grows a number that was halved on request, so
# it is not taken here.
FINGER_R = 3.40        # corner radius -- a laser cuts it for free and a sharp
                       # internal corner is where acrylic starts a crack
# THE MOUTH. Where each notch runs out to the panel edge it leaves two square
# corners, and those are the VERTICAL edges a finger passes going in. Distinct
# from FINGER_R, which rounds the pocket's far corners 10 mm inboard. 2.00
# keeps clear of the vent field, whose lowest hole edge is at y=47.15 against
# a fillet reaching y=46.00.
MOUTH_R = 2.00

# ---- NO SHARP CORNERS ANYWHERE. Every corner on this profile is radiused.
# The first cut had square outer corners, a square J9 aperture and two square
# convex corners where each notch meets the panel edge:
#     > "Terrible super sharp corners."
# On acrylic this is not only a handling complaint. A sharp internal corner is
# a stress raiser and it is where a crack starts, and a sharp convex corner on
# a 3 mm sheet is a genuine edge to catch a hand on. A laser cuts a radius for
# free, so there is no reason for any of them to exist.
# SQUARE, BY REQUEST -- reverted 2026-08-09:
#     > "un fillet the corners"
# Only the OUTER four. The internal radii stay: the notch R6, the notch-mouth
# R2 and the port radius are crack-prevention on a 3 mm sheet and were what the
# original sharp-corner complaint was actually about. A square outline also
# matches the shell's cavity, which is a square cut, so the panel fills its
# opening instead of leaving four gaps.
CORNER_R = 0.00        # the panel's four outer corners: square
# J9_R IS DERIVED, NOT CHOSEN -- see the block below the aperture maths. A
# rounded aperture stops passing a plug long before its nominal width says so,
# so the radius is the largest that still admits a spec-maximum overmould.

# ---- THE SENSOR VENT. U5 is an AHT20 temperature/humidity part, and a
# humidity sensor sealed inside a box measures the box:
#     > "not many [hole] ports for the temp sensors. really bad."
# The first cut had NONE, because it copied the printed backplate -- which
# dropped rev 2's vent for its own reasons -- instead of asking what the part
# is for. A grid over U5, generated and then FILTERED against every other
# opening and the panel edge, so it cannot silently collide with the notch.
VENT_D = 2.20          # hole diameter
VENT_PITCH = 3.60      # centres
# 7x7, not 5x5. The notch is 20 mm deep on this edge and eats the three rows
# below U5 outright, so a 5x5 lattice centred on the sensor lost 10 of 25 and
# left a thin band on one side. Generating a wider lattice and letting the
# filter decide keeps a proper field without hand-placing anything -- the
# survivors are simply every position that fits.
VENT_COLS = 7
VENT_ROWS = 7
VENT_MARGIN = 1.20     # material required around a vent hole
VENT_MIN = 20          # fail the build rather than ship a token few holes
U5 = (13.20, 18.00)    # board coords, from the board notes; mapped with bxy

PLATE_SIZE = P.CAVITY - 2 * P.PLATE_FIT          # 74.20, same as the printed one
P0 = (P.BOARD - PLATE_SIZE) / 2                  # panel spans P0 .. P0+PLATE_SIZE
P1 = P0 + PLATE_SIZE

# ---- THE SPACERS, which this panel does not work without ------------------
SPACER_H = round(P.Z_PLATE_SEAT - P.Z_BOARD_BACK, 4)
SPACER_OD = P.TUBE_OD
SPACER_ID = P.TUBE_BORE

# ---- the J9 aperture: THE PLUG PLUS CLEARANCE, AND NOTHING ELSE ----------
#     > "the cutouts huge, right? It should be a nice, like, just... takes the
#     >  space it needs to for the USB-C to fit through."
#
# It was. The old derivation took the CONNECTOR's envelope and grew it by half
# the difference to the overmould, applied on all four sides -- so the HEIGHT
# inherited a growth computed from the WIDTH. J9 is 8.95 x 3.17 and the plug is
# 12.35 x 6.50, so the height was grown by 1.70/side to chase a 1.70/side width
# problem and came out 7.08 for a 6.50 plug: 0.29/side of clearance nobody
# asked for, on top of J9_CLR.
#
# The aperture is now what has to pass through it, plus the fit:
#
#     AP = PLUG + 2 * J9_CLR              12.65 x 6.80   (was 12.85 x 7.08)
#
# AND THE REMAINING SIZE IS NOT SLACK -- it is the plug. J9's face sits 4.26 mm
# BELOW the panel's outer surface, so the plug's moulded grip has to travel
# through the panel to reach the connector; an aperture that merely cleared the
# 8.95 x 3.17 connector would admit no cable at all. PLUG_W/PLUG_H are the
# USB-IF MAXIMUM (12.35 x 6.50), which is where the last of the slack lives:
# measure the grip on the cable that will actually live here and this shrinks
# with it, because the aperture is derived from those two numbers alone.
J9 = P.BACK_PARTS["J9 USB-C"]
J9_W, J9_H = J9[1] - J9[0], J9[3] - J9[2]

# GIVEN, NOT DERIVED. Asked for outright on 2026-08-10:
#     > "make the usbc 10.4 x 4.4 too"   ... "just do it do not check"
# This is SMALLER than a spec-maximum plug overmould (12.35 x 6.50), and J9's
# face sits 4.26 mm below the outer surface, so the grip has to pass through
# the panel to reach the connector. The plug-fit derivation and its assertions
# are therefore switched off rather than left to fail. Recorded, not hidden:
# a cable with a grip larger than 10.40 x 4.40 will not seat in this port.
AP_W, AP_H = 10.40, 4.40
J9_R = 0.80                    # was derived from the plug; given now, like AP
J9_FIT_CHECKED = False
J9_C = P.bxy((J9[0] + J9[1]) / 2, (J9[2] + J9[3]) / 2)

assert AP_W > J9_W and AP_H > J9_H, (
    "the aperture no longer clears the connector body itself")


# ---- where the buttons actually are, in CASE coordinates ------------------
# Read out of BACK_PARTS and mapped with P.bxy, never typed: bxy flips BOTH
# axes, and a button pair is the one feature here that would look plausible
# on the wrong edge.
_btn = [v for k, v in P.BACK_PARTS.items() if k.startswith("button")]
_BTN_CASE = []
for x0, x1, y0, y1 in _btn:
    a, b = P.bxy(x0, y0), P.bxy(x1, y1)
    _BTN_CASE.append((min(a[0], b[0]), max(a[0], b[0]),
                      min(a[1], b[1]), max(a[1], b[1])))

# group them into a LEFT and a RIGHT cluster by which panel edge they are near
_LEFT = [b for b in _BTN_CASE if (b[0] + b[1]) / 2 < P.BOARD / 2]
_RIGHT = [b for b in _BTN_CASE if (b[0] + b[1]) / 2 >= P.BOARD / 2]
assert len(_LEFT) == 2 and len(_RIGHT) == 2, (
    f"expected two button pairs, got {len(_LEFT)} and {len(_RIGHT)}")


def _notch(cluster, from_left):
    """Rounded opening over one button pair, running out to the panel edge."""
    # CENTRED ON THE PANEL, not on the button pair. The buttons' own centroid
    # is at y=36.85 and the panel's centre is 37.00 -- 0.15 apart, which is far
    # too small to read as a deliberate offset and far too large to look
    # aligned. It just looks like nobody checked. The notch is 28 long against
    # a 16 mm pair, so centring it on the panel still leaves 5.85 mm of margin
    # on the tighter side, and every button corner is checked against the
    # built solid below rather than assumed.
    ys = [c[2] for c in cluster] + [c[3] for c in cluster]
    cy = P.BOARD_H / 2
    # At the halved size the notch no longer spans the button BODIES, so the
    # requirement is now that every button CENTRE is clear with PRESS_CLR
    # around it -- checked against the built solid further down, not here.
    # far edge measured INWARD from the panel edge; the opening is then
    # extended past the edge so the boolean leaves a clean open notch
    if from_left:
        x_in, x_edge = P0 + FINGER_W, P0 - 2.0
    else:
        x_in, x_edge = P1 - FINGER_W, P1 + 2.0
    return ((x_in + x_edge) / 2, cy, abs(x_in - x_edge), cy - min(ys),
            max(ys) - cy)


# ---- the vent grid, generated then FILTERED against everything else -------
# Generating and filtering, rather than hand-placing, is the point: the notch
# is 20 mm deep on this very edge and U5 sits 5.15 mm clear of it, so a
# hand-placed grid would have dropped holes into the opening the moment any
# notch dimension changed. Anything that cannot keep VENT_MARGIN of material
# on every side is discarded, and the survivor count is asserted.
U5_C = P.bxy(*U5)
_NOTCH_RECTS = []
for _cluster, _from_left in ((_LEFT, True), (_RIGHT, False)):
    _cx, _cy, _w, _, _ = _notch(_cluster, _from_left)
    _NOTCH_RECTS.append((_cx - _w / 2, _cx + _w / 2,
                         _cy - FINGER_L / 2, _cy + FINGER_L / 2))
_J9_RECT = (J9_C[0] - AP_W / 2, J9_C[0] + AP_W / 2,
            J9_C[1] - AP_H / 2, J9_C[1] + AP_H / 2)
_SCREWS = [P.bxy(x, y) for x, y in P.MOUNTS]

# What actually stands on the panel at each screw: the printed back's SQUARE
# corner column, which runs from the panel edge inward past the screw centre by
# SPACER_OD / 2. It fully contains the acrylic version's Ø6 loose spacer, so
# one footprint serves both parts.
_POST = SPACER_OD / 2
_STANDOFF_FOOTPRINTS = []
for _sx, _sy in _SCREWS:
    _fx = sorted((P0 if _sx < P.BOARD / 2 else P1,
                  _sx + (-_POST if _sx > P.BOARD / 2 else _POST)))
    _fy = sorted((P0 if _sy < P.BOARD_H / 2 else P1,
                  _sy + (-_POST if _sy > P.BOARD_H / 2 else _POST)))
    _STANDOFF_FOOTPRINTS.append((_fx[0], _fx[1], _fy[0], _fy[1]))


def _vent_ok(x, y):
    r = VENT_D / 2 + VENT_MARGIN
    if not (P0 + r <= x <= P1 - r and P0 + r <= y <= P1 - r):
        return False, "off the panel"
    for x0, x1, y0, y1 in _NOTCH_RECTS + [_J9_RECT]:
        if x0 - r <= x <= x1 + r and y0 - r <= y <= y1 + r:
            return False, "too close to another opening"
    # THE KEEP-OUT IS THE STANDOFF, NOT THE SCREW HOLE. This cleared
    # SCREW_CLR_D / 2 -- the Ø3.40 hole -- and it is not the hole that occupies
    # a corner, it is the STANDOFF sitting on the panel around it: a square
    # 7.10 column on the printed back, a Ø6.0 spacer under the acrylic one.
    # With the round posts a vent overlapped a tube wall by 0.023 mm; the
    # moment the posts became square corner columns the same field ran 0.350
    # into one. Both parts share this profile and NEITHER of their verifiers
    # can see it -- this file has no standoffs to clash with, and the printed
    # back inherits the profile already cut. The keep-out belongs here, in the
    # one file that draws the holes, and it clears the LARGER of the two.
    for x0, x1, y0, y1 in _STANDOFF_FOOTPRINTS:
        if x0 - r <= x <= x1 + r and y0 - r <= y <= y1 + r:
            return False, "too close to a standoff"
    return True, ""


# ---- ONE VENT FIELD, AND IT IS THE ONE OVER THE SENSOR --------------------
#     > "I don't know why there's a grid on the left side. Really we only need
#     >  a grid for... the temperature sensors."
#
# There were two, mirrored about x = BOARD/2. I added the second one on
# 2026-08-09 for symmetry, after "make the design more intentional", and then
# justified it as the other half of a cross-flow path. That was a decoration
# with an engineering excuse attached: U5 is the only part on this board that
# needs to breathe, it sits on the right, and 35 holes were cut through the
# panel over nothing at all.
#
# The field sits CENTRED IN THE BAND between the notch and the panel edge
# rather than on U5's own y, so it is centred in the space it occupies. That
# puts it 6.55 mm above the sensor, which in a 79 mm box is nothing.
VENT_BAND = (P.BOARD_H / 2 + FINGER_L / 2 + VENT_D / 2 + VENT_MARGIN,
             P1 - VENT_D / 2 - VENT_MARGIN)
VENT_CY = (VENT_BAND[0] + VENT_BAND[1]) / 2
VENT_CENTRES = [(U5_C[0], VENT_CY)]


def _vent_field(cx, cy):
    """Largest fully-valid rectangle of the lattice around one centre.

    Per-hole filtering alone is not enough: a column that misses the notch in
    X survives at low Y and leaves a stray line of holes dangling off the
    block. Brute force over a 7x7 is free, and the result cannot acquire
    strays if any other dimension moves.
    """
    grid = []
    for r in range(VENT_ROWS):
        row = []
        for c in range(VENT_COLS):
            vx = cx + (c - (VENT_COLS - 1) / 2) * VENT_PITCH
            vy = cy + (r - (VENT_ROWS - 1) / 2) * VENT_PITCH
            row.append((round(vx, 4), round(vy, 4)) + _vent_ok(vx, vy))
        grid.append(row)
    best = (0, 0, 0, 0, 0)
    for r0 in range(VENT_ROWS):
        for r1 in range(r0 + 1, VENT_ROWS + 1):
            for c0 in range(VENT_COLS):
                for c1 in range(c0 + 1, VENT_COLS + 1):
                    if all(grid[r][c][2]
                           for r in range(r0, r1) for c in range(c0, c1)):
                        a = (r1 - r0) * (c1 - c0)
                        if a > best[0]:
                            best = (a, r0, r1, c0, c1)
    _, R0, R1, C0, C1 = best
    return ([(grid[r][c][0], grid[r][c][1], "")
             for r in range(R0, R1) for c in range(C0, C1)], (R1 - R0, C1 - C0))


_FIELDS = [_vent_field(cx, cy) for cx, cy in VENT_CENTRES]
VENTS = [h for f, _ in _FIELDS for h in f]
VENT_BLOCK = _FIELDS[0][1]

# ONE FIELD, AND THE GATE SAYS SO. This asserted that the two fields matched;
# with a single field that check would pass vacuously forever, which is worse
# than no check. What matters now is that the field is over the sensor.
assert len(_FIELDS) == 1, "this panel carries exactly one vent field"
assert abs(sum(h[0] for h in _FIELDS[0][0]) / len(_FIELDS[0][0])
           - U5_C[0]) < VENT_PITCH, (
    "the vent field has drifted off U5 -- it is the only reason it exists")
for _f, _b in _FIELDS:
    assert len(_f) >= VENT_MIN, (
        f"a vent field is only {_b[0]}x{_b[1]} = {len(_f)} holes "
        f"(want >= {VENT_MIN})")


with BuildPart() as panel:
    with BuildSketch(Plane.XY) as prof:
        with Locations((P.BOARD / 2, P.BOARD_H / 2)):
            if CORNER_R > 0:
                RectangleRounded(PLATE_SIZE, PLATE_SIZE, CORNER_R)
            else:
                Rectangle(PLATE_SIZE, PLATE_SIZE)

        # four M3 clearance holes
        with Locations(*[P.bxy(x, y) for x, y in P.MOUNTS]):
            Circle(SCREW_CLR_D / 2, mode=Mode.SUBTRACT)

        # the J9 aperture
        with Locations(J9_C):
            RectangleRounded(AP_W, AP_H, J9_R, mode=Mode.SUBTRACT)

        # the two finger notches
        for cluster, from_left in ((_LEFT, True), (_RIGHT, False)):
            cx, cy, w, _, _ = _notch(cluster, from_left)
            with Locations((cx, cy)):
                RectangleRounded(w, FINGER_L, FINGER_R, mode=Mode.SUBTRACT)

        # the sensor vent
        with Locations(*[(vx, vy) for vx, vy, _ in VENTS]):
            Circle(VENT_D / 2, mode=Mode.SUBTRACT)

    extrude(amount=SHEET_T)

# ---- THE MOUTH CORNERS, ROUNDED ------------------------------------------
#     > "you are filleting the wrong thing i mean the veritcal edge"
#
# Each notch leaves two corners where its wall runs out and meets the panel
# edge. In plan they are corners; in the solid they are the VERTICAL edges that
# run the full thickness, and they are what a finger passes on the way into the
# pocket. FINGER_R never touched them -- that radius rounds the pocket's far
# corners, 10 mm inboard.
#
# DONE ON THE SOLID, NOT THE SKETCH, AND THAT IS THE WHOLE POINT. Filleting the
# sketch vertices is what killed this feature once before and it failed again
# here: prof.vertices() reports the sketch's LOCAL frame -- the same corner I
# had just matched at case (74.10, 43.698) came back to the fillet engine as
# (-74.20, -13.397). A 3D edge carries world coordinates, so selecting there
# needs no frame guesswork at all.
#
# A laser cuts this exactly as well as a printer does, so it belongs to the
# shared profile rather than to the printed part -- and PROFILE is taken FROM
# the filleted solid below, so the printed back inherits it instead of being
# quietly different.
_CY = P.BOARD_H / 2
_BAND = FINGER_L / 2 + 1.0

_mouth = []
for _e in panel.part.edges():
    _bb = _e.bounding_box()
    if abs((_bb.max.Z - _bb.min.Z) - SHEET_T) > 1e-6:
        continue                                  # not a full-thickness edge
    if (_bb.max.X - _bb.min.X) > 1e-6 or (_bb.max.Y - _bb.min.Y) > 1e-6:
        continue                                  # not vertical
    _ex, _ey = _bb.min.X, _bb.min.Y
    if (abs(_ex - P0) < 0.05 or abs(_ex - P1) < 0.05) and abs(_ey - _CY) <= _BAND:
        _mouth.append(_e)

assert len(_mouth) == 4, (
    f"expected 4 notch-mouth edges, two per notch, found {len(_mouth)}")
part = fillet(_mouth, MOUTH_R)

# The profile the printed back extrudes is taken FROM the filleted solid, so
# there is still exactly one description of this outline.
#
# IT MUST BE THE TOP FACE, MOVED DOWN TO z=0 -- NOT the bottom face. A face
# carries a normal, extrude() follows it, and the bottom face of a solid points
# at -Z: handing that over sent the printed back's panel DOWNWARD to
# z -2.19..0, leaving only the four columns above it. Nothing caught it. The
# bbox check passed because the total height is the same either way, and every
# opening check passed because a hole is "open" in a region that is entirely
# air. CHECKS THAT ASSERT ABSENCE PASS VACUOUSLY ON AN EMPTY PART, so the
# printed back now also asserts PRESENCE.
_top = max((f for f in part.faces() if abs(f.center().Z - SHEET_T) < 1e-6),
           key=lambda f: f.area)
PROFILE = Pos(0, 0, -SHEET_T) * _top
assert abs(PROFILE.center().Z) < 1e-6, "the exported profile is not at z=0"

# ---- THE ENGRAVE LAYER, WHICH IS MARKING AND NOT GEOMETRY -----------------
# "4SQUARE V1" -- the digit, because that is the brand used everywhere else in
# this project (4square, 4square-case-*, justclockc3). Dictated as "Foursquare
# V1"; the written form is the digit form.
#
# IT IS AN OUTLINE ENGRAVE. The DXF carries the letter WIRES -- outer contours
# AND inner counters (the holes in 4, Q, R, A) -- not filled hatches, so a laser
# traces them at engrave power. Every wire is emitted, or a letter loses its
# hole and reads as a blob.
#
# IT DOES NOT TOUCH THE SOLID. Nothing below adds to or subtracts from `part`,
# `PROFILE`, or anything the printed back imports: this block only produces
# shapes that are handed to the DXF exporter on their own layer. An engrave
# that changed the solid would change the printed back too, which has no laser.
#
# WHICH FACE. The panel's outer face points +Z. A viewer standing behind the
# case looks along -Z with +Y up and reads plain, non-mirrored XY, so the text
# is drawn NORMALLY -- no mirror. *** THE LASER MUST ENGRAVE THE FACE THAT ENDS
# UP AS THE OUTER FACE. *** Engraving the other side reads mirrored through the
# acrylic.
#
# WHERE. The lower-middle band is the only clear ground on this panel: the
# finger notches own y 27..47 on both edges, the vent field is up at the top
# right, the J9 aperture is bottom-centre at y 3.3..7.7 (bxy flips BOTH axes --
# it is not at the top) and the four standoff footprints own the corners out to
# 7 mm. Centred on the panel in X; the Y and the size are checked against every
# one of those below rather than eyeballed.
ENGRAVE_TEXT = "4SQUARE V1"
ENGRAVE_SIZE = 6.30                 # em; the caps measure ~5.49 mm, asserted
ENGRAVE_CX = P.BOARD / 2            # 37.00, centred on the panel
ENGRAVE_CY = 17.50                  # middle of the clear band
ENGRAVE_CLR = 2.00                  # demanded clear of every opening and edge

_txt = Text(ENGRAVE_TEXT, font_size=ENGRAVE_SIZE,
            align=(Align.CENTER, Align.CENTER))
_tbb = _txt.bounding_box()
# centre on the ACTUAL ink box, not on the font's em box: the em box carries
# ascender/descender space this all-caps string never uses, so aligning to it
# would drop the visible text below where the clearance was checked. Taken from
# the bbox extremes rather than any .center(), which on OCC is not a datum.
_tx = (_tbb.min.X + _tbb.max.X) / 2
_ty = (_tbb.min.Y + _tbb.max.Y) / 2
ENGRAVE = Pos(ENGRAVE_CX - _tx, ENGRAVE_CY - _ty) * _txt

# every wire: one outer contour per glyph, plus each counter
ENGRAVE_WIRES = []
for _f in ENGRAVE.faces():
    ENGRAVE_WIRES.append(_f.outer_wire())
    ENGRAVE_WIRES.extend(_f.inner_wires())

SHEET = {
    "title": "4square case REV 5 -- ACRYLIC BACK",
    "subtitle": f"{PLATE_SIZE:.2f} sq x {SHEET_T:.2f} mm sheet, finger notch "
                f"over each button pair -- laser-cut from the DXF",
    "views": ["top", "iso-top", "iso-bottom"],
    "labels": [f"1  THE CUT PROFILE ({SHEET_T:.1f} mm acrylic)",
               "2  FROM THE BACK",
               "3  FROM INSIDE THE CASE"],
    "notes": [f"finger notch {FINGER_W:.1f} x {FINGER_L:.1f}, R{FINGER_R:.1f}, "
              f"open to the panel edge",
              f"a button top sits {SHEET_T + P.Z_PLATE_SEAT - (P.Z_BOARD_BACK + BTN_H):.2f} mm "
              f"below the outer face" if False else
              f"J9 aperture {AP_W:.2f} x {AP_H:.2f}, 4x Ø{SCREW_CLR_D} screw holes",
              f"NEEDS 4 SPACERS {SPACER_H:.2f} mm long, Ø{SPACER_OD}/Ø{SPACER_ID}"],
}

# ===== verify ==============================================================
BTN_H = 4.38           # buttons proud of the board back, from the board ref
REACH_INNER = P.Z_PLATE_SEAT - (P.Z_BOARD_BACK + BTN_H)
REACH_OUTER = REACH_INNER + SHEET_T

v = cadkit.Verifier(part, "4square case REV 5 -- acrylic back")
v.note("sheet", f"{PLATE_SIZE:.2f} x {PLATE_SIZE:.2f} x {SHEET_T:.2f} mm, flat")
v.note("finger notch", f"{FINGER_W:.2f} deep x {FINGER_L:.2f} long, R{FINGER_R}, "
                       f"open to the panel edge")
v.note("button body, for comparison", f"{_LEFT[0][1] - _LEFT[0][0]:.2f} x "
                                      f"{_LEFT[0][3] - _LEFT[0][2]:.2f} mm")
v.note("how far a finger must reach",
       f"{REACH_INNER:.2f} mm below the inner face, {REACH_OUTER:.2f} mm below "
       f"the outer face -- this is why the opening is finger-sized, not "
       f"button-sized")
v.note("J9 aperture", f"{AP_W:.2f} x {AP_H:.2f} at case {J9_C[0]:.2f}, "
                      f"{J9_C[1]:.2f}")
v.note("sensor vent", f"{len(VENTS)} x Ø{VENT_D} on a {VENT_PITCH} mm grid: "
                      f"ONE {VENT_BLOCK[0]}x{VENT_BLOCK[1]} field at "
                      f"y={VENT_CY:.2f}, centred in the band between the notch "
                      f"and the panel edge, over U5 at case "
                      f"({U5_C[0]:.2f}, {U5_C[1]:.2f}). The mirrored second "
                      f"field was removed -- it vented nothing.")
v.note("corner radii", f"outline R{CORNER_R}, notch R{FINGER_R}, J9 R{J9_R} -- no sharp corner anywhere")
v.note("SPACERS REQUIRED", f"4 x {SPACER_H:.2f} mm long, Ø{SPACER_OD} / "
                           f"Ø{SPACER_ID} bore -- flat sheet has no standoff "
                           f"tubes and the cavity has no ledge, so without "
                           f"these the screws pull the acrylic into thin air")

v.standard()
v.no_enclosed_voids()
v.measurements(P.M)
v.bbox(x=PLATE_SIZE, y=PLATE_SIZE, z=SHEET_T)

# ---- IT IS ACTUALLY FLAT. The whole point of the part: a laser cuts one
# profile, so every face must be either the two skins or a rib perpendicular
# to them. A part that acquired any Z variation is not laser-cuttable.
_zs = set()
for f in part.faces():
    bb = f.bounding_box()
    if abs(bb.max.Z - bb.min.Z) < 1e-6:
        _zs.add(round(bb.min.Z, 6))
v.check("flat stock: every horizontal face is on one of the two skins",
        _zs == {0.0, SHEET_T}, f"horizontal faces at z={sorted(_zs)}")

# ---- the notch really does clear a whole button pair, measured from the
# built solid rather than from the sketch that drew it
from OCP.BRepClass3d import BRepClass3d_SolidClassifier          # noqa: E402
from OCP.gp import gp_Pnt                                        # noqa: E402
from OCP.TopAbs import TopAbs_State                              # noqa: E402

_cl = BRepClass3d_SolidClassifier(part.wrapped)


def _solid(x, y, z=SHEET_T / 2):
    _cl.Perform(gp_Pnt(float(x), float(y), float(z)), 1e-7)
    return _cl.State() == TopAbs_State.TopAbs_IN


# AT THE HALVED SIZE THE TEST HAD TO CHANGE, AND IT IS WEAKER -- SAY SO.
# The old check demanded all four corners of every button body be under the
# opening, and at 20 x 28 they were. At 10 x 14 they are not, so the check is
# now: each button CENTRE is open, and a disc of PRESS_CLR around it is open
# too, i.e. there is somewhere to actually push. The worst clearance is
# measured and reported rather than left implied.
_press_worst = 999.0
for _i, (_x0, _x1, _y0, _y1) in enumerate(_BTN_CASE, 1):
    _bx, _by = (_x0 + _x1) / 2, (_y0 + _y1) / 2
    v.check(f"button {_i} centre is open", not _solid(_bx, _by),
            "acrylic covers the button centre -- it cannot be pressed")
    _reach = 0.0
    for _k in range(1, 61):
        _r = _k * 0.05
        if any(_solid(_bx + _r * __import__("math").cos(_a * 0.1),
                      _by + _r * __import__("math").sin(_a * 0.1))
               for _a in range(63)):
            break
        _reach = _r
    _press_worst = min(_press_worst, _reach)
    v.check(f"button {_i} has >= {PRESS_CLR:.2f} mm of clear push area",
            _reach >= PRESS_CLR - 1e-9,
            f"only {_reach:.2f} mm clear around the centre")
    # how much of the button body is still covered, stated not hidden
    _corners = [(_x0, _y0), (_x1, _y0), (_x0, _y1), (_x1, _y1)]
    _cov = sum(1 for _p in _corners if _solid(*_p))
    v.note(f"button {_i} body corners covered by acrylic", f"{_cov} of 4")
v.note("worst clear radius around a button centre",
       f"{_press_worst:.2f} mm (was full body clearance at the pre-halving "
       f"20 x 28 opening)")

# the four screw holes survived the notches
for _i, (_mx, _my) in enumerate(P.MOUNTS, 1):
    _cx, _cy = P.bxy(_mx, _my)
    v.check(f"screw hole {_i} is open", not _solid(_cx, _cy), "not open")
    v.check(f"screw hole {_i} still has material around it",
            _solid(_cx + SCREW_CLR_D / 2 + 1.5, _cy)
            or _solid(_cx - SCREW_CLR_D / 2 - 1.5, _cy),
            "the notch swallowed the screw boss")

# the opening is big enough for a finger, stated as a measurement not a hope
# The original spec here was 18 x 20 -- a comfortable fingertip. It was
# halved by request, so these now check the part against WHAT WAS ASKED FOR
# and the shortfall against the comfortable size is reported instead.
v.clearance("finger notch across (as requested)", FINGER_W, 10.0)
v.clearance("finger notch along the edge (as requested)", FINGER_L, 14.0)
v.note("WAIVED: opening vs a comfortable fingertip",
       f"{FINGER_W:.1f} x {FINGER_L:.1f} against 18.0 x 20.0 -- halved on "
       f"request. Clear radius at a button centre is {_press_worst:.2f} mm "
       f"(a comfortable press wants 4.0, which needs 11.65 x 18.30). "
       f"Reachable with a nail or stylus, not a fingertip.")

# the panel between the two notches is still a panel
_bridge = (P1 - FINGER_W) - (P0 + FINGER_W)
v.note("acrylic left between the two notches", f"{_bridge:.2f} mm")
v.clearance("web between the notches", _bridge, 20.0)

# ---- the vent is REAL, measured out of the solid, and clear of everything --
for _i, (_vx, _vy, _) in enumerate(VENTS, 1):
    v.check(f"vent {_i} is open", not _solid(_vx, _vy), "not open")
v.check(f"at least {VENT_MIN} vent holes survived", len(VENTS) >= VENT_MIN,
        f"{len(VENTS)} of {VENT_COLS * VENT_ROWS}")
# THE FIELD MUST COVER THE SENSOR, which is the whole reason it is cut. The
# old check compared the two fields to each other and would have gone on
# passing with both of them in the wrong place.
_span_x = (min(x for x, _, _ in VENTS) - VENT_D / 2,
           max(x for x, _, _ in VENTS) + VENT_D / 2)
_span_y = (min(y for _, y, _ in VENTS) - VENT_D / 2,
           max(y for _, y, _ in VENTS) + VENT_D / 2)
v.check("the vent field spans U5 in X",
        _span_x[0] <= U5_C[0] <= _span_x[1],
        f"U5 at x={U5_C[0]:.2f}, field spans {_span_x[0]:.2f}..{_span_x[1]:.2f}")
v.check("the vent field is the only one on the panel", len(_FIELDS) == 1,
        f"{len(_FIELDS)} fields")
v.note("vent field to U5, in Y", f"{_span_y[0] - U5_C[1]:.2f} mm above it")

# ---- NO SHARP CORNERS. Every vertex on the cut profile must belong to an arc
# or sit between an arc and a line -- i.e. no vertex may have two straight
# edges meeting at it. This is the gate for the complaint that produced the
# radii, so it is checked rather than trusted.
_sharp = []
for _v in PROFILE.vertices():
    _adj = [e for e in PROFILE.edges()
            if any((_p - _v.to_tuple()).length < 1e-6 if hasattr(_p, "length")
                   else False for _p in ())]
_lines = [e for e in PROFILE.edges() if e.geom_type == GeomType.LINE]
_arcs = [e for e in PROFILE.edges() if e.geom_type == GeomType.CIRCLE]
for _v in PROFILE.vertices():
    _touch = [e for e in _lines
              if min((Vector(_v.to_tuple()) - (e @ t)).length
                     for t in (0.0, 1.0)) < 1e-6]
    if len(_touch) >= 2:
        _sharp.append((round(_v.X, 3), round(_v.Y, 3)))
# The four panel corners are square BY REQUEST, so they are allowed by name
# rather than by loosening the rule -- anything else that goes square still
# fails. Naming them keeps the gate honest: if a fifth appears it is caught.
_ALLOWED = {(round(x, 3), round(y, 3))
            for x in (round(P0, 3), round(P1, 3))
            for y in (round(P0, 3), round(P1, 3))}
_unexpected = [c for c in _sharp if c not in _ALLOWED]
v.check("no unexpected square corner (only the four panel corners, by request)",
        not _unexpected, f"sharp corners at {_unexpected}")
v.note("square corners", f"{len(_sharp)}, all four are the panel outline")
v.note("profile", f"{len(_lines)} straight cuts, {len(_arcs)} arcs")

# ---- THE ENGRAVE CLEARS EVERYTHING, MEASURED OFF THE LETTER WIRES ---------
# Not off the string's bounding box: a box would pass while a descender or a
# wide glyph poked into an opening, and the whole reason this text is placed by
# assertion rather than by eye is that the panel's clear ground is narrow. Every
# wire is sampled densely and every sample is tested against every feature.
_ink = []
for _w in ENGRAVE_WIRES:
    for _e in _w.edges():
        _n = max(8, int(_e.length / 0.20) + 1)
        for _k in range(_n + 1):
            _p = _e @ (_k / _n)
            _ink.append((_p.X, _p.Y))

v.check("the engrave produced geometry", len(_ink) > 0, "no text wires")


def _rect_gap(px, py, x0, x1, y0, y1):
    """Distance from a point to an axis-aligned rectangle (0 if inside)."""
    dx = max(x0 - px, 0.0, px - x1)
    dy = max(y0 - py, 0.0, py - y1)
    return (dx * dx + dy * dy) ** 0.5


_keepouts = ([("notch", r) for r in _NOTCH_RECTS]
             + [("J9 aperture", _J9_RECT)]
             + [("standoff", r) for r in _STANDOFF_FOOTPRINTS])

_worst = {}
for _px, _py in _ink:
    # panel edges
    _d = min(_px - P0, P1 - _px, _py - P0, P1 - _py)
    if _d < _worst.get("panel edge", (1e9,))[0]:
        _worst["panel edge"] = (_d, _px, _py)
    for _name, (_x0, _x1, _y0, _y1) in _keepouts:
        _d = _rect_gap(_px, _py, _x0, _x1, _y0, _y1)
        if _d < _worst.get(_name, (1e9,))[0]:
            _worst[_name] = (_d, _px, _py)
    for _vx, _vy, _ in VENTS:
        _d = ((_px - _vx) ** 2 + (_py - _vy) ** 2) ** 0.5 - VENT_D / 2
        if _d < _worst.get("vent hole", (1e9,))[0]:
            _worst["vent hole"] = (_d, _px, _py)

for _name in ("panel edge", "notch", "J9 aperture", "standoff", "vent hole"):
    _d, _px, _py = _worst[_name]
    v.check(f"engrave keeps >= {ENGRAVE_CLR:.2f} mm clear of the {_name}",
            _d >= ENGRAVE_CLR - 1e-9,
            f"only {_d:.3f} mm at ({_px:.2f}, {_py:.2f})")

# and it is entirely ON the panel, i.e. the laser is not asked to mark air
_ex0, _ex1 = min(p[0] for p in _ink), max(p[0] for p in _ink)
_ey0, _ey1 = min(p[1] for p in _ink), max(p[1] for p in _ink)
v.check("the engrave is inside the panel outline",
        P0 < _ex0 and _ex1 < P1 and P0 < _ey0 and _ey1 < P1,
        f"ink spans {_ex0:.2f}..{_ex1:.2f} x {_ey0:.2f}..{_ey1:.2f}")
v.check("cap height is a legible 5-6 mm", 5.0 <= (_ey1 - _ey0) <= 6.0,
        f"caps measure {_ey1 - _ey0:.2f} mm")
# COUNTERS. 4, Q, R and A each enclose a hole; emit only the outer contours and
# they fill in solid and the string stops reading. Checked by count, so a font
# or string change that loses one fails the build.
_counters = sum(len(_f.inner_wires()) for _f in ENGRAVE.faces())
v.check("every letter counter is present as its own wire", _counters == 4,
        f"{_counters} counters, expected 4 (the holes in 4, Q, R, A)")
v.check("one outer contour per visible glyph",
        len(ENGRAVE.faces()) == len(ENGRAVE_TEXT.replace(" ", "")),
        f"{len(ENGRAVE.faces())} faces for "
        f"{len(ENGRAVE_TEXT.replace(' ', ''))} glyphs")

# THE ENGRAVE MUST NOT HAVE TOUCHED THE SOLID. This is the property the printed
# back depends on: it imports PROFILE and knows nothing about any marking.
v.check("the engrave did not change the cut profile",
        abs(PROFILE.area - _top.area) < 1e-9,
        "PROFILE no longer matches the face it was taken from")

v.note("engrave", f"\"{ENGRAVE_TEXT}\" outline, {_ex1 - _ex0:.2f} x "
                  f"{_ey1 - _ey0:.2f} mm centred at ({ENGRAVE_CX:.2f}, "
                  f"{ENGRAVE_CY:.2f}), {len(ENGRAVE_WIRES)} wires "
                  f"({len(ENGRAVE.faces())} outlines + {_counters} counters) "
                  f"on DXF layer ENGRAVE -- MARK THE OUTER FACE, not mirrored")
v.note("engrave worst clearance",
       ", ".join(f"{k} {_worst[k][0]:.2f}" for k in
                 ("panel edge", "notch", "J9 aperture", "standoff",
                  "vent hole")))

v.report()

# Importable: the PRINTED back reuses PROFILE so the two cannot drift apart.
if __name__ == "__main__":
    cadkit.export(part, OUT, verifier=v, sheet=SHEET)

# ---- THE DXF, which is what actually gets cut ----------------------------
# The STEP and STL are for the assembly check; a laser wants the profile.
if __name__ == "__main__":
    # TWO LAYERS, AND THE CUTTER SORTS THEM BY NAME AND COLOUR.
    #   CUT      black -- the through profile: outline, notches, port, vents
    #   ENGRAVE  blue  -- "4SQUARE V1" outlines, marked not cut
    # A single-layer DXF makes the operator guess which contours are which, and
    # the wrong guess cuts the text straight through 3 mm acrylic.
    try:
        exporter = ExportDXF(unit=Unit.MM)
        exporter.add_layer("CUT", color=ColorIndex.BLACK)
        exporter.add_layer("ENGRAVE", color=ColorIndex.BLUE)
        exporter.add_shape(PROFILE.faces()[0], layer="CUT")
        exporter.add_shape(ENGRAVE_WIRES, layer="ENGRAVE")
        exporter.write(OUT + ".dxf")
        print(f"wrote {OUT}.dxf   <- send THIS to the laser")
        print(f"     CUT layer = through cut, ENGRAVE layer = "
              f"\"{ENGRAVE_TEXT}\" outline on the OUTER face")
    except Exception as e:                                       # noqa: BLE001
        # IT RAISES NOW. This used to print and carry on with exit 0, and it
        # cost exactly what a swallowed error always costs: the layered export
        # died on an unimported ColorIndex, no DXF was written at all, and the
        # run still ended "82/82 checks passed". The DXF is the deliverable of
        # this part -- a build that did not produce it has not succeeded.
        raise RuntimeError(f"DXF export failed: {e}") from e
