r"""
4square case REV 6 -- ACRYLIC BACK.

    ~/.venvs/cad/bin/python 4square-case-rev6-acrylic-back.py

REV 6 CHANGES ONLY THE ACRYLIC PANEL. Rev 5's files are untouched, and the
PRINTED back still imports rev 5's PROFILE -- so the "both backs share one
profile" property is DELIBERATELY BROKEN by this revision. That divergence is
measured and reported at the bottom of every run rather than being papered
over; the user asked for the acrylic only.

WHAT CHANGED, AND WHAT IT WAS MEASURED AGAINST
----------------------------------------------

1. THE VENT FIELD LOSES ITS RIGHTMOST COLUMN AND ITS BOTTOM ROW.
   Rev 5 generated a 7x7 lattice and filtered it to a 5 row x 7 column block,
   35 holes, at x 50.0 .. 71.6 and y 49.75 .. 64.15. The user counted "about 5
   in the column and 6 in the row, 11 total". That reconciles exactly:
       rightmost column (x = 71.6)          5 holes
       bottom row (y = 49.75), column gone  6 holes
       ------------------------------------------
                                           11 holes removed, 24 left = 4 x 6
   The 24 survivors keep their rev 5 centres to the micron -- asserted against
   a hardcoded golden set below, so a future change to the filter cannot move
   them silently.

2. THE REMAINING HOLES SHRINK 20%.  2.20 -> 1.76 mm diameter, asserted as
   exactly 0.8x. Centres unchanged.

3. THE TWO FINGER NOTCHES GROW TOWARD EACH OTHER.  This is the reconciliation
   of the user's "the slot to slot inside distance ... 54.3":

       rev 5 left notch  inner edge  x = P0 + FINGER_W = 9.90
       rev 5 right notch inner edge  x = P1 - FINGER_W = 64.10
       inside distance                             = 54.20 mm   <- their 54.3

   "widen to 10 mm ... 10 on this side, 10 on that side" therefore means each
   notch grows 10 mm INWARD, outer (panel-edge) ends staying put, so the
   inside distance drops by 20:  FINGER_W 10.00 -> 20.00, inside 54.20 ->
   34.20. FINGER_L, the run ALONG the edge, is NOT touched -- that is the
   length, and the request named the width.

4. THE ENGRAVE GOES ON THE INSIDE FACE.  See the long note above the DXF
   export. Short version: the whole drawing is emitted in the INSIDE-FACE-UP
   frame, both layers mirrored about x = 37.00.

5. THE ENGRAVE LAYOUT IS A NUMBERED VARIANT.  Twenty of them, rendered by
   rev6-variant-gallery.py. Variant 1 -- "4SQUARE V1" top-left -- is the
   provisional default and is what this script exports.

6. THE VENT PATTERN IS ALSO A NUMBERED VARIANT (21..32). The default is the
   plain shrunk-hole grid of change 1/2 above. The sensor-local motifs are
   built by the same code path, not drawn only in the preview.

ORIENTATION. Flat stock, one 2D profile extruded to the sheet thickness. Z=0
is the panel's INNER face, +Z the OUTER face, and the profile is drawn in the
CASE frame so it drops onto the assembly with no transform. The DXF is the
one place a different frame is used, and it says so.
"""
import math
import os
import sys

from build123d import *
from build123d.exporters import ColorIndex          # not re-exported by *
import cadkit
import case_params as P

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "4square-case-rev6-acrylic-back")

# ---- which layout to build -----------------------------------------------
# Overridable so the gallery can build every one without editing the file.
# THE CHOSEN CONFIGURATION, picked by the user off the variant sheet on
# 2026-08-19: engrave layout 35 (the final combined one) and vent pattern 23
# (full-footprint vertical slots). The 1..20 / 21..34 study layouts are all
# still buildable through these two variables; the gallery uses them.
ENGRAVE_VARIANT = int(os.environ.get("REV6_ENGRAVE", "35"))
VENT_VARIANT = int(os.environ.get("REV6_VENT", "23"))

# ---- knobs inherited unchanged from rev 5 --------------------------------
SHEET_T = 3.00
KERF = 0.00
SCREW_CLR_D = 3.40
J9_CLR = 0.15
AP_W, AP_H = 10.40, 4.40           # given outright, not derived (rev 5 note)
J9_R = 0.80

# ---- (3) THE NOTCHES -----------------------------------------------------
FINGER_W_REV5 = 10.00
FINGER_W = 20.00                   # 10.00 -> 20.00: +10 per side, inward
FINGER_L = 20.00                   # UNCHANGED -- the length, not the width
FINGER_R = 3.40
MOUTH_R = 2.00
PRESS_CLR = 1.50                   # rev 5's waived value, read not re-argued
CORNER_R = 0.00

# ---- (1)(2) THE VENT -----------------------------------------------------
VENT_D_REV5 = 2.20
VENT_SHRINK = 0.80
VENT_D = round(VENT_D_REV5 * VENT_SHRINK, 6)        # 1.76
VENT_PITCH = 3.60
VENT_COLS = 7
VENT_ROWS = 7
VENT_MARGIN = 1.20
VENT_MIN = 20
U5 = (13.20, 18.00)                 # AHT20, board coords -- mapped with bxy

# THE LEDs, READ OUT OF THE BOARD FILE, NOT OUT OF CASE.md.
# justclock-pcb.kicad_pcb footprint origins minus the (100,100) page origin,
# with the F.CrtYd courtyard extents that go with them:
#     D2 (19.0, 10.0)  D4 (19.0, 17.5)  D5 (19.0, 25.0)   courtyard 6.90 x 5.50
#     U5 (13.2, 18.0)  AHT20                              courtyard 3.40 x 3.00
#     R7 ( 9.5,  9.0)  light sensor                       courtyard 3.36 x 1.90
# CASE.md's LED line agrees; the project rules say the board CAD wins, so these
# are taken from the board and CASE.md is only a cross-check.
LEDS_BOARD = [("D2", 19.0, 10.0), ("D4", 19.0, 17.5), ("D5", 19.0, 25.0)]
LED_CRTYD = (6.90, 5.50)
U5_CRTYD = (3.40, 3.00)
R7_BOARD = (9.50, 9.00)
R7_CRTYD = (3.36, 1.90)
LED_CLR = 1.20                      # material the vent must keep off a LED

PLATE_SIZE = P.CAVITY - 2 * P.PLATE_FIT
P0 = (P.BOARD - PLATE_SIZE) / 2
P1 = P0 + PLATE_SIZE
PANEL_CX = P.BOARD / 2              # 37.00 -- the mirror axis for the DXF

SPACER_H = round(P.Z_PLATE_SEAT - P.Z_BOARD_BACK, 4)
SPACER_OD = P.TUBE_OD
SPACER_ID = P.TUBE_BORE

J9 = P.BACK_PARTS["J9 USB-C"]
J9_W, J9_H = J9[1] - J9[0], J9[3] - J9[2]
J9_C = P.bxy((J9[0] + J9[1]) / 2, (J9[2] + J9[3]) / 2)
assert AP_W > J9_W and AP_H > J9_H, "the aperture no longer clears J9 itself"


def _rect_of(cx, cy, w, h):
    return (cx - w / 2, cx + w / 2, cy - h / 2, cy + h / 2)


U5_C = P.bxy(*U5)
U5_RECT = _rect_of(U5_C[0], U5_C[1], *U5_CRTYD)
LED_CASE = [(n,) + P.bxy(x, y) for n, x, y in LEDS_BOARD]
LED_RECTS = [(n,) + _rect_of(cx, cy, *LED_CRTYD) for n, cx, cy in LED_CASE]
R7_C = P.bxy(*R7_BOARD)
R7_RECT = _rect_of(R7_C[0], R7_C[1], *R7_CRTYD)

# ---- buttons, read out of BACK_PARTS and mapped, never typed --------------
_btn = [v for k, v in P.BACK_PARTS.items() if k.startswith("button")]
_BTN_CASE = []
for x0, x1, y0, y1 in _btn:
    a, b = P.bxy(x0, y0), P.bxy(x1, y1)
    _BTN_CASE.append((min(a[0], b[0]), max(a[0], b[0]),
                      min(a[1], b[1]), max(a[1], b[1])))
_LEFT = [b for b in _BTN_CASE if (b[0] + b[1]) / 2 < P.BOARD / 2]
_RIGHT = [b for b in _BTN_CASE if (b[0] + b[1]) / 2 >= P.BOARD / 2]
assert len(_LEFT) == 2 and len(_RIGHT) == 2, "expected two button pairs"


def _notch(cluster, from_left):
    ys = [c[2] for c in cluster] + [c[3] for c in cluster]
    cy = P.BOARD_H / 2
    if from_left:
        x_in, x_edge = P0 + FINGER_W, P0 - 2.0
    else:
        x_in, x_edge = P1 - FINGER_W, P1 + 2.0
    return ((x_in + x_edge) / 2, cy, abs(x_in - x_edge), cy - min(ys),
            max(ys) - cy)


_NOTCH_RECTS = []
for _cluster, _from_left in ((_LEFT, True), (_RIGHT, False)):
    _cx, _cy, _w, _, _ = _notch(_cluster, _from_left)
    _NOTCH_RECTS.append((_cx - _w / 2, _cx + _w / 2,
                         _cy - FINGER_L / 2, _cy + FINGER_L / 2))
_J9_RECT = (J9_C[0] - AP_W / 2, J9_C[0] + AP_W / 2,
            J9_C[1] - AP_H / 2, J9_C[1] + AP_H / 2)
_SCREWS = [P.bxy(x, y) for x, y in P.MOUNTS]

_POST = SPACER_OD / 2
_STANDOFF_FOOTPRINTS = []
for _sx, _sy in _SCREWS:
    _fx = sorted((P0 if _sx < P.BOARD / 2 else P1,
                  _sx + (-_POST if _sx > P.BOARD / 2 else _POST)))
    _fy = sorted((P0 if _sy < P.BOARD_H / 2 else P1,
                  _sy + (-_POST if _sy > P.BOARD_H / 2 else _POST)))
    _STANDOFF_FOOTPRINTS.append((_fx[0], _fx[1], _fy[0], _fy[1]))

# ---- THE TWO CLEAR SPANS EITHER SIDE OF THE USB-C PORT -------------------
# At the port's own height the credit line is boxed in on the outside by the
# BOTTOM corner standoffs' keep-out footprints (x <= 7.00 on the left, x >=
# 67.00 on the right) and on the inside by the J9 aperture. Those two numbers
# are what "centred between the holes and the port" means, so they are read
# off the same objects the clearance checker uses rather than typed.
_BOT_STANDOFFS = sorted((r for r in _STANDOFF_FOOTPRINTS
                         if abs(r[2] - P0) < 1e-9), key=lambda r: r[0])
assert len(_BOT_STANDOFFS) == 2, "expected two standoffs on the bottom edge"
CREDIT_SPAN_L = (_BOT_STANDOFFS[0][1], _J9_RECT[0])      # 7.00 .. 31.80
CREDIT_SPAN_R = (_J9_RECT[1], _BOT_STANDOFFS[1][0])      # 42.20 .. 67.00
CREDIT_CX_L = (CREDIT_SPAN_L[0] + CREDIT_SPAN_L[1]) / 2
CREDIT_CX_R = (CREDIT_SPAN_R[0] + CREDIT_SPAN_R[1]) / 2

BRIDGE = (P1 - FINGER_W) - (P0 + FINGER_W)
BRIDGE_REV5 = (P1 - FINGER_W_REV5) - (P0 + FINGER_W_REV5)


# ---- the rev 5 lattice, regenerated exactly, THEN trimmed -----------------
def _vent_ok(x, y, d=VENT_D):
    r = d / 2 + VENT_MARGIN
    if not (P0 + r <= x <= P1 - r and P0 + r <= y <= P1 - r):
        return False, "off the panel"
    for x0, x1, y0, y1 in _NOTCH_RECTS + [_J9_RECT]:
        if x0 - r <= x <= x1 + r and y0 - r <= y <= y1 + r:
            return False, "too close to another opening"
    for x0, x1, y0, y1 in _STANDOFF_FOOTPRINTS:
        if x0 - r <= x <= x1 + r and y0 - r <= y <= y1 + r:
            return False, "too close to a standoff"
    return True, ""


VENT_BAND = (P.BOARD_H / 2 + FINGER_L / 2 + VENT_D / 2 + VENT_MARGIN,
             P1 - VENT_D / 2 - VENT_MARGIN)
VENT_CY = (VENT_BAND[0] + VENT_BAND[1]) / 2


def _vent_field(cx, cy):
    """Largest fully-valid rectangle of the lattice around one centre."""
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
    return ([(grid[r][c][0], grid[r][c][1])
             for r in range(R0, R1) for c in range(C0, C1)], (R1 - R0, C1 - C0))


VENTS_FULL, VENT_BLOCK_FULL = _vent_field(U5_C[0], VENT_CY)

# THE FIELD THIS REVISION STARTS FROM MUST BE THE ONE REV 5 SHIPPED. If the
# filter ever picks a different block the removal below would silently take a
# different column and row, so the starting block is pinned, not assumed.
assert VENT_BLOCK_FULL == (5, 7), (
    f"rev 5 shipped a 5x7 vent block; this build generated "
    f"{VENT_BLOCK_FULL[0]}x{VENT_BLOCK_FULL[1]}")
assert len(VENTS_FULL) == 35, f"{len(VENTS_FULL)} holes before trimming"

# ---- (1) THE REMOVAL -----------------------------------------------------
# RIGHTMOST means largest X, and largest X really is the viewer's right: the
# DXF/preview frame used everywhere below is the OUTSIDE view (see the export
# note), in which +X runs right and +Y runs up.
_XS = sorted({round(x, 4) for x, _ in VENTS_FULL})
_YS = sorted({round(y, 4) for _, y in VENTS_FULL})
DROP_COL_X = _XS[-1]                    # rightmost column
DROP_ROW_Y = _YS[0]                     # bottom row

_after_col = [(x, y) for x, y in VENTS_FULL if abs(x - DROP_COL_X) > 1e-6]
VENT_CENTRES = [(x, y) for x, y in _after_col if abs(y - DROP_ROW_Y) > 1e-6]

N_COL_DROPPED = len(VENTS_FULL) - len(_after_col)
N_ROW_DROPPED = len(_after_col) - len(VENT_CENTRES)

# The user's own count, checked rather than trusted: "like 5 in the column and
# 6 in the row", 11 total.
assert N_COL_DROPPED == 5, f"dropped {N_COL_DROPPED} from the column"
assert N_ROW_DROPPED == 6, f"dropped {N_ROW_DROPPED} from the row"
assert len(VENT_CENTRES) == 24, f"{len(VENT_CENTRES)} holes left, expected 24"

VENT_BLOCK = (len({round(y, 4) for _, y in VENT_CENTRES}),
              len({round(x, 4) for x, _ in VENT_CENTRES}))
assert VENT_BLOCK == (4, 6), f"remaining block is {VENT_BLOCK}, expected 4x6"

# THE CENTRES ARE REV 5's, TO THE MICRON. Hardcoded from the rev 5 build so a
# future edit to the filter, the pitch or the band cannot move them quietly
# while every other check goes on passing.
_GOLDEN_XS = (50.0, 53.6, 57.2, 60.8, 64.4, 68.0)
_GOLDEN_YS = (53.35, 56.95, 60.55, 64.15)
_GOLDEN = {(round(x, 4), round(y, 4)) for x in _GOLDEN_XS for y in _GOLDEN_YS}
assert {(round(x, 4), round(y, 4)) for x, y in VENT_CENTRES} == _GOLDEN, (
    "the surviving vent centres are not rev 5's centres")

VENT_FOOTPRINT = (min(x for x, _ in VENT_CENTRES) - VENT_D / 2,
                  max(x for x, _ in VENT_CENTRES) + VENT_D / 2,
                  min(y for _, y in VENT_CENTRES) - VENT_D / 2,
                  max(y for _, y in VENT_CENTRES) + VENT_D / 2)

# ---- THE SENSOR-LOCAL REGION ---------------------------------------------
# The trimmed grid still runs straight over the LED line: D2/D4/D5 sit at case
# x = 55.00, courtyard x 51.55 .. 58.45, y 46.25 .. 66.75, and the footprint
# above spans x 49.12 .. 68.88. Reported as a flag, and it is why the small
# motifs exist. The corridor a sensor-local vent may use is bounded by:
#     LED courtyard right edge + LED_CLR   x >= 59.65
#     panel edge  - VENT_MARGIN - r        x <= ~72.0
#     notch top   + VENT_MARGIN            y >= ~49.0
#     R7 courtyard bottom - LED_CLR        y <= 62.85
LED_RIGHT = max(r[2] for r in LED_RECTS)        # 58.45
R7_BOTTOM = R7_RECT[2]                          # 64.05

# A SENSOR MOTIF IS ANCHORED BY ITS LEFT EDGE, NOT BY ITS CENTRE, and that is
# forced by arithmetic rather than chosen for tidiness. Two gates squeeze it
# from opposite sides:
#     clear of the LEDs   left edge >= 58.45 + 1.20 = 59.65
#     spans U5 in X       left edge <= 60.80
# -- a window 1.15 mm wide. Centring each motif on a fixed box instead put a
# narrow one (3x3 holes) at left edge 61.92 and it silently stopped covering
# the sensor it exists for. Anchoring the left edge satisfies both for every
# motif regardless of how wide it is.
SENSOR_X0 = 59.90
SENSOR_CY = U5_C[1]                             # 56.00, U5's own centreline
SENSOR_X_MAX = P1 - VENT_MARGIN - 0.10          # 72.90
SENSOR_HALF_H = min(SENSOR_CY - (P.BOARD_H / 2 + FINGER_L / 2 + VENT_MARGIN),
                    R7_BOTTOM - LED_CLR - SENSOR_CY)
assert SENSOR_X0 >= LED_RIGHT + LED_CLR - 1e-9, (
    f"sensor patch starts at {SENSOR_X0}, LEDs end at {LED_RIGHT}")
assert SENSOR_X0 <= U5_C[0], "the patch would start right of U5"


# ===== VENT PATTERNS =======================================================
# Every pattern is produced as shapely polygons in panel coordinates, so open
# area and the minimum web between cuts are MEASURED, not asserted from the
# drawing intent -- and the same polygons are what the solid subtracts, so the
# preview cannot drift from the part.
from shapely.geometry import Point, Polygon, box                  # noqa: E402
from shapely.ops import unary_union                               # noqa: E402

VENT_WEB_MIN = 1.00        # least acrylic allowed between two cuts
FOOT_W = VENT_FOOTPRINT[1] - VENT_FOOTPRINT[0]
FOOT_H = VENT_FOOTPRINT[3] - VENT_FOOTPRINT[2]
FOOT_C = ((VENT_FOOTPRINT[0] + VENT_FOOTPRINT[1]) / 2,
          (VENT_FOOTPRINT[2] + VENT_FOOTPRINT[3]) / 2)


def _circ(cx, cy, d):
    return Point(cx, cy).buffer(d / 2, quad_segs=32)


def _rrect(cx, cy, w, h, r):
    """Rounded rectangle as a polygon; r=0 gives a plain rectangle."""
    if r <= 1e-9:
        return box(cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2)
    r = min(r, w / 2 - 1e-6, h / 2 - 1e-6)
    core = box(cx - w / 2 + r, cy - h / 2 + r, cx + w / 2 - r, cy + h / 2 - r)
    return core.buffer(r, quad_segs=32)


def _slot(cx, cy, length, width, angle=0.0):
    """Stadium slot: a segment buffered by half the width."""
    from shapely.geometry import LineString
    a = math.radians(angle)
    hx, hy = math.cos(a) * (length - width) / 2, math.sin(a) * (length - width) / 2
    return LineString([(cx - hx, cy - hy), (cx + hx, cy + hy)]).buffer(
        width / 2, quad_segs=32)


def _grid_pts(cx, cy, cols, rows, pitch_x, pitch_y=None):
    py = pitch_y if pitch_y is not None else pitch_x
    return [(cx + (c - (cols - 1) / 2) * pitch_x,
             cy + (r - (rows - 1) / 2) * py)
            for r in range(rows) for c in range(cols)]


def _pat_grid_baseline():
    return [_circ(x, y, VENT_D) for x, y in VENT_CENTRES]


def _pat_footprint_hslots():
    ys = [FOOT_C[1] + (i - 1.5) * 3.60 for i in range(4)]
    return [_slot(FOOT_C[0], y, FOOT_W - 1.6, 1.30) for y in ys]


def _pat_footprint_vslots():
    xs = [FOOT_C[0] + (i - 2.5) * 3.60 for i in range(6)]
    return [_slot(x, FOOT_C[1], FOOT_H - 1.4, 1.30, 90) for x in xs]


def _sensor(fn):
    """Build a motif about the origin, then LAND it: left edge on SENSOR_X0,
    vertical centre on the sensor's own y. Every extent is then asserted, so a
    motif cannot be added later that quietly runs into a LED, into R7, into
    the notch or off the panel."""
    def wrapped():
        from shapely.affinity import translate
        gs = fn()
        u = unary_union(gs)
        x0, y0, x1, y1 = u.bounds
        dx = SENSOR_X0 - x0
        dy = SENSOR_CY - (y0 + y1) / 2
        out = [translate(g, dx, dy) for g in gs]
        u = unary_union(out)
        bx0, by0, bx1, by1 = u.bounds
        assert abs(bx0 - SENSOR_X0) < 1e-6, f"{fn.__name__} did not land"
        assert bx1 <= SENSOR_X_MAX + 1e-6, (
            f"{fn.__name__} reaches x={bx1:.2f}, panel allows {SENSOR_X_MAX}")
        assert (SENSOR_CY - by0) <= SENSOR_HALF_H + 1e-6 and \
               (by1 - SENSOR_CY) <= SENSOR_HALF_H + 1e-6, (
            f"{fn.__name__} is {by1 - by0:.2f} tall, allowed "
            f"{2 * SENSOR_HALF_H:.2f}")
        return out
    wrapped.__name__ = fn.__name__
    return wrapped


def _holes(pix, pitch, d, pitch_y=None):
    py = pitch_y if pitch_y is not None else pitch
    return [_circ(cx * pitch, cy * py, d) for cx, cy in pix]


@_sensor
def _pat_sensor_3x3():
    return _holes([(c, r) for r in (-1, 0, 1) for c in (-1, 0, 1)], 3.20, 1.76)


@_sensor
def _pat_sensor_4x4():
    return _holes([(c, r) for r in (-1.5, -0.5, 0.5, 1.5)
                   for c in (-1.5, -0.5, 0.5, 1.5)], 2.90, 1.60)


@_sensor
def _pat_sensor_foursquare():
    """The brand mark as perforation: four 2x2 blocks in a 2x2 super-grid.

    Webs, all measured by vent_stats afterwards rather than trusted:
        inside a block   2.65 - 1.60 = 1.05
        block to block   6.60 - 2.65 - 1.60 = 2.35

    The block pitch is 6.60 and not 5.60 because at 5.60 the two gaps were
    1.05 and 1.35 -- close enough that the thing read as one 4x4 grid and the
    whole point of the motif was lost. Checked by looking at the render, which
    is the only way that particular failure is visible.
    """
    out = []
    for bx in (-3.30, 3.30):
        for by in (-3.30, 3.30):
            out += [_circ(bx + dx, by + dy, 1.60)
                    for dx in (-1.325, 1.325) for dy in (-1.325, 1.325)]
    return out


@_sensor
def _pat_sensor_foursquare_blocks():
    """Same mark, four rounded pixel squares. Web 5.20 - 3.60 = 1.60."""
    return [_rrect(x, y, 3.60, 3.60, 0.60)
            for x in (-2.60, 2.60) for y in (-2.60, 2.60)]


@_sensor
def _pat_sensor_plus():
    """Nine holes on a cross. Ø1.50 on a 2.60 pitch leaves a 1.10 web."""
    return _holes([(0, 0), (0, 1), (0, 2), (0, -1), (0, -2),
                   (1, 0), (2, 0), (-1, 0), (-2, 0)], 2.60, 1.50)


@_sensor
def _pat_sensor_hex():
    """Honeycomb: a centre hole, a ring of six, a ring of six more.

    On a triangular lattice of pitch p the second ring sits at p*sqrt(3) and
    is p away from the first, so ONE number sets every web: 3.10 - 1.80 = 1.30.
    """
    p, d = 3.10, 1.80
    out = [_circ(0, 0, d)]
    for k in range(6):
        a = math.radians(60 * k)
        out.append(_circ(p * math.cos(a), p * math.sin(a), d))
    for k in range(6):
        a = math.radians(60 * k + 30)
        rr = p * math.sqrt(3)
        out.append(_circ(rr * math.cos(a), rr * math.sin(a), d))
    return out


@_sensor
def _pat_sensor_rings():
    """A centre hole ringed twice -- reads as a speaker grille.

    Six on the inner ring, not eight: at r=3.20 eight holes sit 2.45 apart and
    a Ø1.50 hole leaves a 0.95 web, under the minimum. Six leaves 1.70.
    """
    out = [_circ(0, 0, 2.20)]
    for rr, n, d, off in ((3.20, 6, 1.50, 0.0), (5.60, 12, 1.30, 15.0)):
        for k in range(n):
            a = math.radians(360 * k / n + off)
            out.append(_circ(rr * math.cos(a), rr * math.sin(a), d))
    return out


@_sensor
def _pat_sensor_slot_pairs():
    """Four stadium slots. 2.30 pitch on a 1.30 slot leaves a 1.00 web."""
    return [_slot(0, dy, 7.20, 1.30) for dy in (-3.45, -1.15, 1.15, 3.45)]


@_sensor
def _pat_sensor_slats():
    """Three 45 degree slots. Perpendicular spacing 3.40*sin45 = 2.40, so the
    web is 1.10 -- the spacing along X is NOT the web on a diagonal slat, and
    reading it as one is how the first cut came out at 0.89."""
    return [_slot(dx, 0, 6.40, 1.30, 45) for dx in (-3.40, 0.0, 3.40)]


@_sensor
def _pat_sensor_thermo():
    """A pixel thermometer: a bulb under a stem. Ø1.55 on 2.60 -> 1.05 web."""
    stem = [(0, 2), (0, 1), (0, 0)]
    bulb = [(-1, -1), (0, -1), (1, -1), (0, -2)]
    return _holes(stem + bulb, 2.60, 1.55)


@_sensor
def _pat_sensor_droplet():
    """A pixel droplet -- humidity, the AHT20's other half."""
    pix = [(0, 2), (-1, 1), (0, 1), (1, 1), (-1, 0), (0, 0), (1, 0),
           (-1, -1), (0, -1), (1, -1), (0, -2)]
    return _holes(pix, 2.60, 1.55)


VENT_PATTERNS = {
    21: ("Hole grid, shrunk (BASELINE / default export)", _pat_grid_baseline,
         "the rev 5 field minus its right column and bottom row, holes 20% "
         "smaller -- exactly what was asked for. Still runs over D2/D4."),
    22: ("Footprint: horizontal slots", _pat_footprint_hslots,
         "same footprint, four full-width stadium slots."),
    23: ("Footprint: vertical slots", _pat_footprint_vslots,
         "same footprint, six tall stadium slots."),
    24: ("Sensor patch: 3x3 holes", _pat_sensor_3x3,
         "smallest useful motif, dead over the AHT20, LEDs untouched."),
    25: ("Sensor patch: 4x4 holes", _pat_sensor_4x4,
         "a denser square block, still inside the sensor corridor."),
    26: ("Sensor patch: 4SQUARE mark, holes", _pat_sensor_foursquare,
         "four 2x2 hole blocks arranged as four squares -- the brand mark, "
         "perforated."),
    27: ("Sensor patch: 4SQUARE mark, pixels", _pat_sensor_foursquare_blocks,
         "the same mark cut as four rounded pixel squares."),
    28: ("Sensor patch: plus", _pat_sensor_plus,
         "a nine-hole cross centred on the sensor."),
    29: ("Sensor patch: honeycomb", _pat_sensor_hex,
         "thirteen holes on a triangular lattice."),
    30: ("Sensor patch: concentric rings", _pat_sensor_rings,
         "a centre hole ringed twice -- reads as a speaker grille."),
    31: ("Sensor patch: slot pairs", _pat_sensor_slot_pairs,
         "four short stadium slots, the least fiddly to cut."),
    32: ("Sensor patch: diagonal slats", _pat_sensor_slats,
         "three 45 degree slots."),
    33: ("Sensor patch: pixel thermometer", _pat_sensor_thermo,
         "an icon that says what the holes are for."),
    34: ("Sensor patch: pixel droplet", _pat_sensor_droplet,
         "humidity, the AHT20's other half."),
}
VENT_SENSOR_LOCAL = set(range(24, 35))


def vent_geoms(variant):
    return VENT_PATTERNS[variant][1]()


def vent_stats(geoms):
    """Open area, footprint, and the smallest web between any two cuts."""
    u = unary_union(geoms)
    bx0, by0, bx1, by1 = u.bounds
    foot = (bx1 - bx0) * (by1 - by0)
    web = 1e9
    for i in range(len(geoms)):
        for j in range(i + 1, len(geoms)):
            web = min(web, geoms[i].distance(geoms[j]))
    if len(geoms) < 2:
        web = float("nan")
    return {"n": len(geoms), "open_area": u.area, "foot": foot,
            "open_pct": 100.0 * u.area / foot if foot else 0.0,
            "web": web, "bounds": (bx0, bx1, by0, by1)}


VENT_GEOMS = vent_geoms(VENT_VARIANT)
VENT_STATS = vent_stats(VENT_GEOMS)

# EVERY PATTERN IS GATED AT IMPORT, NOT ONLY THE ONE BEING BUILT. The gallery
# is a menu, and a menu must not offer a dish that cannot be cooked: an option
# whose web is too thin or that sits on a LED would be picked by number and
# only fail later. Six of these motifs failed this gate on their first draft.
_LED_BOX = unary_union([box(r[1], r[3], r[2], r[4]) for r in LED_RECTS])
_U5_BOX = box(U5_RECT[0], U5_RECT[2], U5_RECT[1], U5_RECT[3])
VENT_AUDIT = {}
for _n in VENT_PATTERNS:
    _g = vent_geoms(_n)
    _s = vent_stats(_g)
    _u = unary_union(_g)
    _over = _u.intersection(_LED_BOX).area
    _gap = _u.distance(_LED_BOX)
    VENT_AUDIT[_n] = {"open_pct": _s["open_pct"], "web": _s["web"],
                      "led_overlap": _over, "led_gap": _gap,
                      "bounds": _s["bounds"], "n": _s["n"]}
    assert math.isnan(_s["web"]) or _s["web"] >= VENT_WEB_MIN - 1e-6, (
        f"vent pattern {_n} ({VENT_PATTERNS[_n][0]}) has a "
        f"{_s['web']:.3f} mm web, minimum is {VENT_WEB_MIN}")
    assert _s["bounds"][0] <= U5_C[0] <= _s["bounds"][1], (
        f"vent pattern {_n} does not span U5 in X")
    if _n in VENT_SENSOR_LOCAL:
        assert _over == 0.0 and _gap >= LED_CLR - 1e-6, (
            f"vent pattern {_n} ({VENT_PATTERNS[_n][0]}) is sensor-local but "
            f"comes within {_gap:.3f} mm of a LED (want {LED_CLR})")
        assert _u.intersects(_U5_BOX) or _u.distance(_U5_BOX) < 1.5, (
            f"vent pattern {_n} does not actually reach the AHT20")


# ===== THE ENGRAVE LAYOUTS =================================================
# A variant is a list of BLOCKS. Each block names its text, the size it would
# like, an anchor point and how the ink is aligned to it. The size is then
# reduced until the ink clears every opening by ENGRAVE_CLR -- so a layout can
# be described by where it belongs rather than by numbers that were tuned by
# eye, and a block that cannot be made to fit FAILS rather than shipping
# overlapping the notch.
ENGRAVE_CLR = 2.00
ENGRAVE_SIZE = 6.30                 # rev 5's size; caps come out 5.49
ENGRAVE_MIN_SIZE = 2.20             # below this it is not readable, so fail

BRAND = "4SQUARE V1"

# Clear ground on this panel, all in the OUTSIDE view:
#   bottom band   y  8..25   full width, minus the J9 aperture at x 31.8..42.2
#   top band      y 47..74   minus the vent field (top right) and two standoffs
#   middle band   y 27..47   only x 19.9..54.1, the 34.20 web between notches
#   bottom edge   y  0..3.3  a thin strip under J9
# _TOP_Y is 69.00 and that is a MEASURED ceiling, not a taste call. A 6.30 em
# cap is 5.49 mm, so a line needs 2.745 mm above its anchor plus ENGRAVE_CLR;
# the panel's top edge is at 74.10, so any anchor above 69.355 forces the
# solver to shrink the type. 70.00 -- the obvious round number -- cost variant
# 1 a third of its size for nothing.
_BOT_Y = 16.0
_TOP_Y = 69.0
_MID_Y = P.BOARD_H / 2


def _blk(text, cx, cy, size=ENGRAVE_SIZE, halign="center", valign="center",
         font="ttf", group=None):
    return {"text": text, "cx": cx, "cy": cy, "size": size,
            "halign": halign, "valign": valign, "font": font, "group": group}


# ---- NEWSTROKE, THE BOARD'S OWN SILKSCREEN FONT --------------------------
# Pulled out of KiCad by rev6-newstroke-strokes.py -- see that file for why it
# cannot simply be loaded like a TTF. Three facts about it drive the code
# below, and each one broke something when assumed instead of measured:
#
#   1. IT IS CENTRELINES, NOT OUTLINES. There are no faces and no counters, so
#      every glyph check written for the outline text is meaningless here and
#      is switched off by name rather than left to pass vacuously.
#   2. THE MARKED WIDTH IS NOT IN THE GEOMETRY. A stroke is a pen path; what
#      the laser actually removes is that path dilated by half the stroke
#      thickness. Clearance is therefore demanded on the DILATED ink, not on
#      the centreline, or the mark creeps ENGRAVE_STROKE/2 closer to every
#      opening than the check believes.
#   3. CAP HEIGHT == SIZE, but the INK BOX can be taller: "Q" descends and the
#      digit "4" rises. "4SQUARE V1" is 1.1429 x size tall and that is
#      correct.
NEWSTROKE_PATH = os.path.join(HERE, "rev6-newstroke-strokes.json")
try:
    import json as _json
    with open(NEWSTROKE_PATH) as _fh:
        NEWSTROKE = _json.load(_fh)
except FileNotFoundError as _e:                                  # noqa: BLE001
    raise RuntimeError(
        f"{NEWSTROKE_PATH} is missing -- run rev6-newstroke-strokes.py, which "
        f"extracts the real newstroke geometry from KiCad") from _e

NEWSTROKE_RATIO = NEWSTROKE["thickness_ratio"]      # 0.15, the board's own
assert abs(NEWSTROKE["cap_height"] - NEWSTROKE["size"]) < 0.02, (
    "the newstroke cache says cap height != size; the solver assumes it does")


ENGRAVE_VARIANTS = {
    1: ("4SQUARE V1 top-left",
        "the brand set into the top-left quadrant, left of the vent field and "
        "inboard of the corner standoff",
        [_blk(BRAND, 9.5, _TOP_Y, halign="left")]),
    2: ("4SQUARE V1 top-right",
        "mirror of 1: ranged right, threaded through the 9.07 mm band between "
        "the vent field and the panel's top edge",
        [_blk(BRAND, 64.5, 69.5, halign="right")]),
    3: ("4SQUARE V1 bottom-left",
        "low and left, in the wide clear band under the notches",
        [_blk(BRAND, 9.5, _BOT_Y, halign="left")]),
    4: ("4SQUARE V1 bottom-right",
        "low and right, ranged off the right edge",
        [_blk(BRAND, 64.5, _BOT_Y, halign="right")]),
    5: ("4SQUARE V1 bottom-centre (rev 5's placement)",
        "where rev 5 put it -- centred in the bottom band, above J9",
        [_blk(BRAND, PANEL_CX, 17.5)]),
    6: ("4SQUARE V1 top-centre",
        "centred along the top edge, tucked under the panel rim -- it gives "
        "up type to the vent field's left column",
        [_blk(BRAND, 30.0, _TOP_Y)]),
    7: ("4SQUARE V1 dead centre",
        "in the 34.20 mm web between the two finger notches",
        [_blk(BRAND, PANEL_CX, _MID_Y)]),
    8: ("CREATED BY / SAWYER SMITH flanking the USB-C",
        "the credit split either side of the J9 aperture, both halves "
        "vertically centred on the port",
        [_blk("CREATED BY", 29.3, J9_C[1], size=3.20, halign="right"),
         _blk("SAWYER SMITH", 44.7, J9_C[1], size=3.20, halign="left")]),
    9: ("4SQUARE stacked over V1",
        "two lines, centred low -- bigger type in the same footprint. The "
        "band between J9 and the notches is 15.3 mm, so both lines are set "
        "at 6.00 em rather than asking for a size that cannot fit",
        [_blk("4SQUARE", PANEL_CX, 21.0, size=6.00),
         _blk("V1", PANEL_CX, 13.0, size=6.00)]),
    10: ("4SQUARE V1 top-left, SAWYER SMITH bottom-right",
         "brand and maker on opposite diagonals",
         [_blk(BRAND, 9.5, _TOP_Y, halign="left"),
          _blk("SAWYER SMITH", 64.5, 12.0, size=3.60, halign="right")]),
    # The strip UNDER J9 was the obvious eleventh idea and it is not
    # available: the aperture's lower edge is at y=3.325 and the panel's is at
    # -0.10, so the whole strip is 3.43 mm and two 2.00 clearances eat it
    # outright. Replaced rather than shipped at an unreadable size.
    11: ("V1 alone, large, dead centre",
         "the revision mark on its own, set big in the 34.20 mm web between "
         "the notches",
         [_blk("V1", PANEL_CX, _MID_Y, size=14.00)]),
    12: ("Big 4SQUARE, small V1 beside it",
         "wordmark plus a revision tag set on its baseline",
         [_blk("4SQUARE", 9.5, 18.0, size=9.00, halign="left"),
          _blk("V1", 66.0, 18.0, size=4.00, halign="right")]),
    13: ("4SQUARE V1 ranged against the vent field",
         "set in the top band's left half and ranged right so its last letter "
         "lines up with the vent block's left edge",
         [_blk(BRAND, 47.0, 58.0, halign="right")]),
    14: ("Three lines, bottom-left",
         "brand, maker and year stacked into the bottom-left corner",
         [_blk("4SQUARE V1", 9.5, 22.0, size=5.00, halign="left"),
          _blk("SAWYER SMITH", 9.5, 16.0, size=3.20, halign="left"),
          _blk("2026", 9.5, 11.0, size=3.20, halign="left")]),
    15: ("4SQUARE V1 centred, SAWYER SMITH under it",
         "a centred two-line colophon in the bottom band",
         [_blk(BRAND, PANEL_CX, 20.5, size=5.60),
          _blk("SAWYER SMITH", PANEL_CX, 13.0, size=3.20)]),
    16: ("V1 alone, top-left; 4SQUARE across the bottom",
         "the revision tag as a corner mark, the wordmark as a baseline rule",
         [_blk("V1", 9.5, _TOP_Y, size=6.30, halign="left"),
          _blk("4 S Q U A R E", PANEL_CX, 14.0, size=5.20)]),
    17: ("4SQUARE V1 hugging the left notch",
         "set vertically-centred in the web, ranged off the left notch wall",
         [_blk(BRAND, 22.4, _MID_Y, size=5.20, halign="left")]),
    18: ("4SQUARE V1 hugging the right notch",
         "the same, ranged off the right notch wall",
         [_blk(BRAND, 51.6, _MID_Y, size=5.20, halign="right")]),
    19: ("SAWYER SMITH high left, 4SQUARE V1 low centre",
         "maker's name up in the empty top-left quadrant, brand where rev 5 "
         "had it",
         [_blk("SAWYER SMITH", 9.5, 58.0, size=4.00, halign="left"),
          _blk(BRAND, PANEL_CX, 17.5)]),
    20: ("Corner set: 4SQUARE top-left, V1 bottom-right",
         "the two halves of the mark thrown to opposite corners",
         [_blk("4SQUARE", 9.5, _TOP_Y, size=6.30, halign="left"),
          _blk("V1", 64.5, 12.0, size=6.30, halign="right")]),
}
# ---- 35: THE CHOSEN ONE ---------------------------------------------------
# Picked by the user out of the variant sheet: layout 8's flanking credit and
# layout 7's centred brand, combined, with "CREATED BY" dropped and the whole
# thing set in the BOARD'S OWN SILKSCREEN FONT.
#
#   "SAWYER"      centred in CREDIT_SPAN_L, the clear ground between the
#                 bottom-left corner standoff and the J9 aperture
#   "SMITH"       centred in CREDIT_SPAN_R, the mirror of it
#                 -- both vertically centred on the port, and LOCKED to one
#                    size by the group mechanism above
#   "4SQUARE V1"  dead centre, in the web between the widened notches. That
#                 web is now 34.20 mm rather than rev 5's 54.20, so the size
#                 is re-solved against the narrower space, not inherited.
#
# THE FIRST CUT RANGED BOTH WORDS A FIXED 2.50 mm OFF THE APERTURE, and that
# is the thing the user caught on the preview: equal at the port, wildly
# unequal at the far end, because the two words are not the same length.
# SAWYER (19.51 wide) sat 2.79 mm off the corner standoff; SMITH (14.46 wide)
# sat 7.85 mm off its own -- "Smith is way further away". Anchoring each word
# to the MIDPOINT OF ITS OWN SPAN instead makes both of its margins equal by
# construction, at any size, and the two spans happen to be identical
# (24.80 mm each) so the pair also lands mirror-symmetric about x = 37.00.
ENGRAVE_FINAL = {
    35: ("FINAL -- SAWYER | SMITH flanking the USB-C, 4SQUARE V1 centred",
         "the chosen rev 6 layout, set in newstroke, the same font the "
         "4square board's silkscreen is drawn in",
         [_blk("SAWYER", CREDIT_CX_L, J9_C[1], size=6.00,
               font="newstroke", group="credit"),
          _blk("SMITH", CREDIT_CX_R, J9_C[1], size=6.00,
               font="newstroke", group="credit"),
          _blk("4SQUARE V1", PANEL_CX, P.BOARD_H / 2, size=6.00,
               font="newstroke")]),
}
ENGRAVE_LAYOUTS = {**ENGRAVE_VARIANTS, **ENGRAVE_FINAL}

assert len(ENGRAVE_VARIANTS) == 20, "twenty layouts were asked for"
assert ENGRAVE_VARIANTS[1][2][0]["text"] == BRAND, "variant 1 is the brand"
assert any(b["text"] == "CREATED BY" for b in ENGRAVE_VARIANTS[8][2]), (
    "variant 8 is the CREATED BY / SAWYER SMITH flanking layout")


# ---- keepouts the engrave has to clear ------------------------------------
def keepouts(vent_geoms_):
    ks = ([("notch", r) for r in _NOTCH_RECTS]
          + [("J9 aperture", _J9_RECT)]
          + [("standoff", r) for r in _STANDOFF_FOOTPRINTS])
    return ks, unary_union(vent_geoms_)


def _rect_gap(px, py, x0, x1, y0, y1):
    dx = max(x0 - px, 0.0, px - x1)
    dy = max(y0 - py, 0.0, py - y1)
    return math.hypot(dx, dy)


def _ink_of(shape):
    pts = []
    for f in shape.faces():
        for w in [f.outer_wire()] + list(f.inner_wires()):
            for e in w.edges():
                n = max(8, int(e.length / 0.25) + 1)
                for k in range(n + 1):
                    p = e @ (k / n)
                    pts.append((p.X, p.Y))
    return pts


def _anchor_offset(block, bx0, bx1, by0, by1):
    """How far to move an ink box so it meets this block's anchor."""
    if block["halign"] == "left":
        ax = block["cx"] - bx0
    elif block["halign"] == "right":
        ax = block["cx"] - bx1
    else:
        ax = block["cx"] - (bx0 + bx1) / 2
    if block["valign"] == "bottom":
        ay = block["cy"] - by0
    elif block["valign"] == "top":
        ay = block["cy"] - by1
    else:
        ay = block["cy"] - (by0 + by1) / 2
    return ax, ay


def _place_newstroke(block, size):
    """One block as newstroke centreline polylines, in panel coordinates."""
    g = NEWSTROKE["strings"].get(block["text"])
    if g is None:
        raise RuntimeError(
            f"{block['text']!r} is not in the newstroke cache -- add it to "
            f"STRINGS in rev6-newstroke-strokes.py and re-run that script")
    s = size / NEWSTROKE["size"]
    ax, ay = _anchor_offset(block, 0.0, g["w"] * s, 0.0, g["h"] * s)
    return [[(x * s + ax, y * s + ay) for x, y in pl] for pl in g["polylines"]]


def _stroke_w(block, size):
    return size * NEWSTROKE_RATIO if block["font"] == "newstroke" else 0.0


def _place(block, size):
    """Text at `size`, moved so its INK box meets the block's anchor."""
    t = Text(block["text"], font_size=size,
             align=(Align.CENTER, Align.CENTER))
    bb = t.bounding_box()
    if block["halign"] == "left":
        ax = block["cx"] - bb.min.X
    elif block["halign"] == "right":
        ax = block["cx"] - bb.max.X
    else:
        ax = block["cx"] - (bb.min.X + bb.max.X) / 2
    if block["valign"] == "bottom":
        ay = block["cy"] - bb.min.Y
    elif block["valign"] == "top":
        ay = block["cy"] - bb.max.Y
    else:
        ay = block["cy"] - (bb.min.Y + bb.max.Y) / 2
    return Pos(ax, ay) * t


def _clearance_of(ink, ks, vent_union):
    worst = {}
    for px, py in ink:
        d = min(px - P0, P1 - px, py - P0, P1 - py)
        if d < worst.get("panel edge", (1e9,))[0]:
            worst["panel edge"] = (d, px, py)
        for name, (x0, x1, y0, y1) in ks:
            d = _rect_gap(px, py, x0, x1, y0, y1)
            if d < worst.get(name, (1e9,))[0]:
                worst[name] = (d, px, py)
    if not vent_union.is_empty:
        from shapely.geometry import MultiPoint
        d = MultiPoint(ink).distance(vent_union)
        worst["vent"] = (d, 0.0, 0.0)
    return worst


def _render_block(b, size):
    """One block at one size -> (shape_or_None, polylines_or_None, ink, bbox).

    The two fonts are genuinely different objects -- filled faces versus open
    pen paths -- so they are kept apart here and unified only as ink points.
    """
    if b["font"] == "newstroke":
        pls = _place_newstroke(b, size)
        ink = []
        for pl in pls:
            for (x0, y0), (x1, y1) in zip(pl, pl[1:]):
                n = max(2, int(math.hypot(x1 - x0, y1 - y0) / 0.25) + 1)
                for k in range(n + 1):
                    ink.append((x0 + (x1 - x0) * k / n,
                                y0 + (y1 - y0) * k / n))
        xs = [p[0] for p in ink]
        ys = [p[1] for p in ink]
        return None, pls, ink, (min(xs), max(xs), min(ys), max(ys))
    shp = _place(b, size)
    ink = _ink_of(shp)
    bb = shp.bounding_box()
    return shp, None, ink, (bb.min.X, bb.max.X, bb.min.Y, bb.max.Y)


def _fits(b, size, ks, vent_union, other_ink):
    """Clearance of this block at this size, on the DILATED ink."""
    _, _, ink, _ = _render_block(b, size)
    worst = _clearance_of(ink, ks, vent_union)
    gap = min(v[0] for v in worst.values())
    if other_ink:
        from shapely.geometry import MultiPoint
        gap = min(gap, MultiPoint(ink).distance(MultiPoint(other_ink)))
    # A stroke is drawn ON its centreline, so the material actually marked
    # reaches half the stroke width past it in every direction.
    return gap - _stroke_w(b, size) / 2.0, worst, ink


def build_engrave(variant, vent_geoms_=None):
    """Lay out one variant, shrinking any block that cannot clear its ground.

    Returns (shapes, wires, info). Raises if a block cannot be made to fit at
    a readable size -- a layout that does not fit is not shipped with a note.
    """
    vg = VENT_GEOMS if vent_geoms_ is None else vent_geoms_
    ks, vent_union = keepouts(vg)
    name, why, blocks = ENGRAVE_LAYOUTS[variant]

    # PASS 1 -- solve every block on its own.
    solved, placed_ink = {}, []
    for i, b in enumerate(blocks):
        size = b["size"]
        for _ in range(24):
            gap, worst, ink = _fits(b, size, ks, vent_union, placed_ink)
            if gap >= ENGRAVE_CLR - 1e-9:
                break
            size *= 0.94
            if size < ENGRAVE_MIN_SIZE:
                raise RuntimeError(
                    f"variant {variant}: block {b['text']!r} anchored at "
                    f"({b['cx']}, {b['cy']}) cannot clear {ENGRAVE_CLR} mm at "
                    f"a readable size -- worst gap {gap:.2f} mm")
        else:
            raise RuntimeError(f"variant {variant}: {b['text']!r} did not settle")
        solved[i] = size
        placed_ink += ink

    # PASS 2 -- A FLANKING PAIR MUST BE ONE SIZE. Solved independently,
    # "SAWYER" is bounded by the corner standoff at 3.8 mm and "SMITH" by
    # nothing until 5.1, so the pair would come out visibly mismatched either
    # side of the same port. Blocks sharing a group are all taken down to the
    # smallest that any of them could reach, then re-checked at that size --
    # shrinking can only increase clearance, but it is verified, not argued.
    groups = {}
    for i, b in enumerate(blocks):
        if b["group"] is not None:
            groups.setdefault(b["group"], []).append(i)
    for gname, idxs in groups.items():
        lock = min(solved[i] for i in idxs)
        for i in idxs:
            solved[i] = lock

    # PASS 3 -- build at the final sizes and record what was measured.
    shapes, polylines, info, ink_all = [], [], [], []
    for i, b in enumerate(blocks):
        size = solved[i]
        shp, pls, ink, bbox = _render_block(b, size)
        others = [p for j, bb2 in enumerate(blocks) if j < i
                  for p in _render_block(bb2, solved[j])[2]]
        gap, worst, _ = _fits(b, size, ks, vent_union, others)
        if gap < ENGRAVE_CLR - 1e-9:
            raise RuntimeError(
                f"variant {variant}: {b['text']!r} fails at its locked size "
                f"{size:.2f} -- {gap:.3f} mm")
        if shp is not None:
            shapes.append(shp)
        if pls is not None:
            polylines.extend(pls)
        ink_all += ink
        info.append({"text": b["text"], "size": round(size, 3),
                     "asked": b["size"], "font": b["font"],
                     "group": b["group"],
                     "stroke_w": round(_stroke_w(b, size), 3),
                     "cap_h": round(bbox[3] - bbox[2], 3),
                     "w": round(bbox[1] - bbox[0], 3),
                     "x0": round(bbox[0], 4), "x1": round(bbox[1], 4),
                     "clear": round(gap, 3),
                     "worst": {k: round(v[0], 3) for k, v in worst.items()}})

    wires, counters = [], 0
    for shp in shapes:
        for f in shp.faces():
            wires.append(f.outer_wire())
            inner = list(f.inner_wires())
            counters += len(inner)
            wires.extend(inner)
    for pl in polylines:
        wires.append(Polyline(*[(x, y) for x, y in pl]))

    return shapes, wires, {
        "name": name, "why": why, "blocks": info, "counters": counters,
        "ink": ink_all, "polylines": polylines,
        "glyphs": sum(len(s.faces()) for s in shapes),
        "stroke_font": any(b["font"] == "newstroke" for b in blocks)}


ENGRAVE_SHAPES, ENGRAVE_WIRES, ENGRAVE_INFO = build_engrave(ENGRAVE_VARIANT)


# ===== THE SOLID ===========================================================
def _face_from_poly(poly):
    """One shapely polygon -> a build123d face, exterior and holes.

    Built in ALGEBRA mode, outside any builder. Polyline belongs to BuildLine,
    so calling this from inside the BuildSketch context raised "BuildSketch
    doesn't have a Polyline object" -- and because only the default vent
    pattern takes the Circle path, that break was invisible until a polygon
    pattern was actually built. Hence the faces are made first, up here, and
    the sketch only add()s them.
    """
    ext = list(poly.exterior.coords)[:-1]
    outer = make_face(Polyline(*[(x, y) for x, y in ext], close=True))
    for ring in poly.interiors:
        pts = list(ring.coords)[:-1]
        outer -= make_face(Polyline(*[(x, y) for x, y in pts], close=True))
    return outer


VENT_FACES = [] if VENT_VARIANT == 21 else [_face_from_poly(g)
                                            for g in VENT_GEOMS]


with BuildPart() as panel:
    with BuildSketch(Plane.XY) as prof:
        with Locations((P.BOARD / 2, P.BOARD_H / 2)):
            if CORNER_R > 0:
                RectangleRounded(PLATE_SIZE, PLATE_SIZE, CORNER_R)
            else:
                Rectangle(PLATE_SIZE, PLATE_SIZE)

        with Locations(*[P.bxy(x, y) for x, y in P.MOUNTS]):
            Circle(SCREW_CLR_D / 2, mode=Mode.SUBTRACT)

        with Locations(J9_C):
            RectangleRounded(AP_W, AP_H, J9_R, mode=Mode.SUBTRACT)

        for cluster, from_left in ((_LEFT, True), (_RIGHT, False)):
            cx, cy, w, _, _ = _notch(cluster, from_left)
            with Locations((cx, cy)):
                RectangleRounded(w, FINGER_L, FINGER_R, mode=Mode.SUBTRACT)

        # THE VENT, from the same shapely polygons the preview draws. Circles
        # are placed as real circles so the default part carries exact arcs
        # rather than a 64-gon; every other pattern comes through the polygon
        # path.
        if VENT_VARIANT == 21:
            with Locations(*VENT_CENTRES):
                Circle(VENT_D / 2, mode=Mode.SUBTRACT)
        else:
            for _vf in VENT_FACES:
                add(_vf, mode=Mode.SUBTRACT)

    extrude(amount=SHEET_T)

# ---- the notch mouths, rounded on the SOLID (rev 5's hard-won note) -------
_CY = P.BOARD_H / 2
_BAND = FINGER_L / 2 + 1.0
_mouth = []
for _e in panel.part.edges():
    _bb = _e.bounding_box()
    if abs((_bb.max.Z - _bb.min.Z) - SHEET_T) > 1e-6:
        continue
    if (_bb.max.X - _bb.min.X) > 1e-6 or (_bb.max.Y - _bb.min.Y) > 1e-6:
        continue
    _ex, _ey = _bb.min.X, _bb.min.Y
    if (abs(_ex - P0) < 0.05 or abs(_ex - P1) < 0.05) and abs(_ey - _CY) <= _BAND:
        _mouth.append(_e)
assert len(_mouth) == 4, f"expected 4 notch-mouth edges, found {len(_mouth)}"
part = fillet(_mouth, MOUTH_R)

def _flat_at(f, z):
    """Is this face flat and lying at height z?  Selected by BOUNDING BOX, not
    by face centroid: a centroid is not a datum, and rule 9 exists because
    centroid-based selection is how a bore fillet once slipped through."""
    bb = f.bounding_box()
    return abs(bb.max.Z - bb.min.Z) < 1e-6 and abs(bb.min.Z - z) < 1e-6


_top = max((f for f in part.faces() if _flat_at(f, SHEET_T)),
           key=lambda f: f.area)
PROFILE = Pos(0, 0, -SHEET_T) * _top
_pbb = PROFILE.bounding_box()
assert abs(_pbb.min.Z) < 1e-6 and abs(_pbb.max.Z) < 1e-6, (
    "the exported profile is not at z=0")


# ===== THE DXF FRAME: INSIDE FACE UP =======================================
# REV 5 ENGRAVED THE OUTER FACE. This revision engraves the INNER one, so the
# text is protected by 3 mm of acrylic and reads through it. Working out what
# that does to the drawing, because getting it wrong is a scrapped sheet:
#
#   * The panel's outer face points +Z. A drawing in plain XY is what you see
#     looking DOWN the -Z direction, i.e. standing OUTSIDE the case looking at
#     the outer face. That is the frame rev 5 drew in, and rev 5's DXF is
#     therefore an OUTSIDE-FACE-UP drawing. Its instruction said so.
#   * A laser marks whichever face is UP on the bed. To mark the inner face,
#     the sheet is laid inner-face-up -- which turns it over about a vertical
#     axis, so the whole part is now seen mirrored.
#   * Therefore the DXF must be drawn in the INSIDE view: EVERY layer mirrored
#     about the panel's own centreline, x = 37.00.
#
# BOTH LAYERS, NOT JUST THE TEXT. Mirroring the engrave alone would put
# readable text on the inside face of a part cut the right way round only if
# the sheet were outside-face-up, which is the case where the mark lands on
# the OUTSIDE. And the cut cannot simply be flipped either: this panel is NOT
# symmetric about that axis -- the vent field sits over U5 at x = 60.80, and a
# panel cut in the wrong handedness puts it at x = 13.20, over nothing. So the
# frame is mirrored and the cut goes with it.
#
# WHAT THE OPERATOR SEES: the vent field appears at TOP-LEFT in this DXF and
# the text appears MIRRORED. Both are correct, and both are the signature that
# the sheet is meant to go on the bed inner-face-up. Assembled, looking at the
# case from outside, the vent is top-right and the text reads normally.
MIRROR = Plane(origin=(PANEL_CX, 0, 0), z_dir=(1, 0, 0))
DXF_CUT = mirror(PROFILE, about=MIRROR)
DXF_ENGRAVE = [mirror(w, about=MIRROR) for w in ENGRAVE_WIRES]

SHEET = {
    "title": f"4square case REV 6 -- ACRYLIC BACK (engrave variant "
             f"{ENGRAVE_VARIANT}, vent {VENT_VARIANT})",
    "subtitle": f"{PLATE_SIZE:.2f} sq x {SHEET_T:.2f} mm sheet -- vents "
                f"{VENT_D:.2f} mm, notch inside distance {BRIDGE:.2f} mm, "
                f"engrave on the INNER face",
    "views": ["top", "iso-top", "iso-bottom"],
    "labels": [f"1  THE CUT PROFILE ({SHEET_T:.1f} mm acrylic)",
               "2  FROM THE BACK",
               "3  FROM INSIDE THE CASE"],
    "notes": [f"finger notch {FINGER_W:.1f} x {FINGER_L:.1f}, R{FINGER_R:.1f}",
              f"vent {len(VENT_GEOMS)} cuts, {VENT_STATS['open_pct']:.1f}% open",
              f"NEEDS 4 SPACERS {SPACER_H:.2f} mm long, Ø{SPACER_OD}/Ø{SPACER_ID}"],
}

# ===== verify ==============================================================
BTN_H = 4.38
REACH_INNER = P.Z_PLATE_SEAT - (P.Z_BOARD_BACK + BTN_H)
REACH_OUTER = REACH_INNER + SHEET_T

v = cadkit.Verifier(part, f"4square case REV 6 -- acrylic back "
                          f"(engrave {ENGRAVE_VARIANT}, vent {VENT_VARIANT})")
v.note("sheet", f"{PLATE_SIZE:.2f} x {PLATE_SIZE:.2f} x {SHEET_T:.2f} mm, flat")
v.note("CHANGE 3: finger notch", f"{FINGER_W_REV5:.2f} -> {FINGER_W:.2f} deep "
                                 f"x {FINGER_L:.2f} long (length unchanged)")
v.note("CHANGE 3: notch inside distance",
       f"{BRIDGE_REV5:.2f} -> {BRIDGE:.2f} mm  (the user's \"54.3\" is the "
       f"{BRIDGE_REV5:.2f}; each notch grew 10.00 mm inward)")
v.note("CHANGE 1: vent holes", f"{len(VENTS_FULL)} -> {len(VENT_CENTRES)}: "
                               f"dropped the column at x={DROP_COL_X:.2f} "
                               f"({N_COL_DROPPED}) and the row at "
                               f"y={DROP_ROW_Y:.2f} ({N_ROW_DROPPED}), "
                               f"{N_COL_DROPPED + N_ROW_DROPPED} total; "
                               f"{VENT_BLOCK_FULL[0]}x{VENT_BLOCK_FULL[1]} -> "
                               f"{VENT_BLOCK[0]}x{VENT_BLOCK[1]}")
v.note("CHANGE 2: vent diameter", f"Ø{VENT_D_REV5:.2f} -> Ø{VENT_D:.2f} "
                                  f"({VENT_SHRINK:.0%}), centres unchanged")
v.note("CHANGE 4: engrave face", "INNER -- the DXF is drawn INSIDE-FACE-UP, "
                                 "both layers mirrored about x=37.00")
v.note("CHANGE 5: engrave layout",
       f"variant {ENGRAVE_VARIANT} -- {ENGRAVE_INFO['name']}")
v.note("CHANGE 6: vent pattern",
       f"variant {VENT_VARIANT} -- {VENT_PATTERNS[VENT_VARIANT][0]}")
v.note("board parts under this panel (case coords)",
       f"U5 AHT20 ({U5_C[0]:.2f}, {U5_C[1]:.2f}); LEDs "
       + ", ".join(f"{n} ({x:.2f}, {y:.2f})" for n, x, y in LED_CASE)
       + f"; R7 ({R7_C[0]:.2f}, {R7_C[1]:.2f})")
v.note("SPACERS REQUIRED", f"4 x {SPACER_H:.2f} mm long, Ø{SPACER_OD} / "
                           f"Ø{SPACER_ID} bore")

v.standard()
v.no_enclosed_voids()
v.measurements(P.M)
v.bbox(x=PLATE_SIZE, y=PLATE_SIZE, z=SHEET_T)

# ---- still flat stock ----------------------------------------------------
_zs = set()
for f in part.faces():
    bb = f.bounding_box()
    if abs(bb.max.Z - bb.min.Z) < 1e-6:
        _zs.add(round(bb.min.Z, 6))
v.check("flat stock: every horizontal face is on one of the two skins",
        _zs == {0.0, SHEET_T}, f"horizontal faces at z={sorted(_zs)}")

from OCP.BRepClass3d import BRepClass3d_SolidClassifier          # noqa: E402
from OCP.gp import gp_Pnt                                        # noqa: E402
from OCP.TopAbs import TopAbs_State                              # noqa: E402

_cl = BRepClass3d_SolidClassifier(part.wrapped)


def _solid(x, y, z=SHEET_T / 2):
    _cl.Perform(gp_Pnt(float(x), float(y), float(z)), 1e-7)
    return _cl.State() == TopAbs_State.TopAbs_IN


# ---- CHANGE 1/2: the vent, measured out of the built solid ----------------
v.check(f"vent diameter is exactly {VENT_SHRINK:.0%} of rev 5's",
        abs(VENT_D - VENT_D_REV5 * VENT_SHRINK) < 1e-9,
        f"Ø{VENT_D} vs Ø{VENT_D_REV5} x {VENT_SHRINK}")
v.check("11 holes were removed: one column and one row",
        N_COL_DROPPED + N_ROW_DROPPED == 11,
        f"{N_COL_DROPPED} + {N_ROW_DROPPED}")
v.check("the surviving block is 4 rows x 6 columns",
        VENT_BLOCK == (4, 6), f"{VENT_BLOCK}")
v.check("every surviving vent centre is unchanged from rev 5",
        {(round(x, 4), round(y, 4)) for x, y in VENT_CENTRES} == _GOLDEN,
        "centres moved")
if VENT_VARIANT == 21:
    for _i, (_vx, _vy) in enumerate(VENT_CENTRES, 1):
        v.check(f"vent {_i} is open", not _solid(_vx, _vy), "not open")
    # the hole really is 1.76 across in the solid, not merely in the sketch
    _r = VENT_D / 2
    _vx, _vy = VENT_CENTRES[0]
    v.check("a vent hole measures Ø1.76 in the solid",
            (not _solid(_vx + _r - 0.02, _vy)) and _solid(_vx + _r + 0.05, _vy),
            "the bore is not where the diameter says it is")
else:
    for _i, _g in enumerate(VENT_GEOMS, 1):
        _p = _g.representative_point()
        v.check(f"vent cut {_i} is open", not _solid(_p.x, _p.y), "not open")
v.check(f"at least {VENT_MIN} vent openings", len(VENT_GEOMS) >= 1,
        f"{len(VENT_GEOMS)}")
v.note("vent open area", f"{VENT_STATS['open_area']:.1f} mm2 over a "
                         f"{VENT_STATS['bounds'][1] - VENT_STATS['bounds'][0]:.1f}"
                         f" x "
                         f"{VENT_STATS['bounds'][3] - VENT_STATS['bounds'][2]:.1f}"
                         f" footprint = {VENT_STATS['open_pct']:.1f}%")
v.check("the vent web is thick enough to survive being cut",
        math.isnan(VENT_STATS["web"]) or VENT_STATS["web"] >= VENT_WEB_MIN - 1e-9,
        f"narrowest web {VENT_STATS['web']:.2f} mm (min {VENT_WEB_MIN})")
_span_x = (VENT_STATS["bounds"][0], VENT_STATS["bounds"][1])
v.check("the vent field still spans U5 in X",
        _span_x[0] <= U5_C[0] <= _span_x[1],
        f"U5 at x={U5_C[0]:.2f}, field {_span_x[0]:.2f}..{_span_x[1]:.2f}")

# ---- THE LEDs. Reported for every pattern, ENFORCED for the sensor-local
# ones. The baseline grid overlaps D2/D4 and that is a KNOWN, REPORTED flag,
# not a silent pass: the user asked for the trimmed grid AND asked for
# sensor-local alternatives, so both states exist on purpose.
_led_union = unary_union([box(r[1], r[3], r[2], r[4]) for r in LED_RECTS])
_vent_union = unary_union(VENT_GEOMS)
LED_OVERLAP = _vent_union.intersection(_led_union).area
LED_GAP = _vent_union.distance(_led_union)
v.note("vent vs the LED courtyards",
       f"overlap {LED_OVERLAP:.2f} mm2, nearest approach {LED_GAP:.2f} mm "
       f"(D2/D4/D5 courtyards 6.90 x 5.50 at case x=55.00)")
# PER LED, NOT JUST THE TOTAL. One number over three parts hides which of them
# is actually exposed, and on a clear panel that is the interesting question.
_LED_EACH = []
for _n, _x0, _x1, _y0, _y1 in LED_RECTS:
    _a = _vent_union.intersection(box(_x0, _y0, _x1, _y1)).area
    _LED_EACH.append(f"{_n} {_a:.2f}")
v.note("...broken down per LED", ", ".join(_LED_EACH) + " mm2 of opening over "
                                                        "each courtyard")
if VENT_VARIANT in VENT_SENSOR_LOCAL:
    v.check("the vent keeps clear of every LED",
            LED_OVERLAP == 0.0 and LED_GAP >= LED_CLR - 1e-9,
            f"overlap {LED_OVERLAP:.2f} mm2, gap {LED_GAP:.2f} mm")
    v.check("the vent sits over the AHT20",
            _vent_union.distance(box(U5_RECT[0], U5_RECT[2],
                                     U5_RECT[1], U5_RECT[3])) < 1e-9
            or _vent_union.intersects(box(U5_RECT[0], U5_RECT[2],
                                          U5_RECT[1], U5_RECT[3])),
            "the sensor patch does not reach U5's courtyard")
else:
    v.note("KNOWN FLAG: this pattern crosses the LED line",
           f"{LED_OVERLAP:.2f} mm2 of hole over a LED courtyard -- the "
           f"sensor-local variants 24-34 exist to fix this")

# ---- CHANGE 3: the notches -----------------------------------------------
v.check("each notch grew exactly 10.00 mm inward",
        abs((FINGER_W - FINGER_W_REV5) - 10.0) < 1e-9,
        f"grew {FINGER_W - FINGER_W_REV5:.2f}")
v.check("the notch LENGTH along the edge is unchanged",
        abs(FINGER_L - 20.00) < 1e-9, f"FINGER_L is {FINGER_L}")
v.check("the notch outer ends still start at the panel edge",
        abs(_NOTCH_RECTS[0][0] - (P0 - 2.0)) < 1e-9
        and abs(_NOTCH_RECTS[1][1] - (P1 + 2.0)) < 1e-9,
        "a notch moved off the panel edge")
v.check("inside distance dropped by 20.00 mm",
        abs((BRIDGE_REV5 - BRIDGE) - 20.0) < 1e-9,
        f"{BRIDGE_REV5:.2f} -> {BRIDGE:.2f}")
v.clearance("web between the notches", BRIDGE, 20.0)

_press_worst = 999.0
for _i, (_x0, _x1, _y0, _y1) in enumerate(_BTN_CASE, 1):
    _bx, _by = (_x0 + _x1) / 2, (_y0 + _y1) / 2
    v.check(f"button {_i} centre is open", not _solid(_bx, _by),
            "acrylic covers the button centre")
    _reach = 0.0
    for _k in range(1, 121):
        _r = _k * 0.05
        if any(_solid(_bx + _r * math.cos(_a * 0.1),
                      _by + _r * math.sin(_a * 0.1)) for _a in range(63)):
            break
        _reach = _r
    _press_worst = min(_press_worst, _reach)
    v.check(f"button {_i} has >= {PRESS_CLR:.2f} mm of clear push area",
            _reach >= PRESS_CLR - 1e-9, f"only {_reach:.2f} mm clear")
    _cov = sum(1 for _p in [(_x0, _y0), (_x1, _y0), (_x0, _y1), (_x1, _y1)]
               if _solid(*_p))
    v.note(f"button {_i} body corners covered by acrylic", f"{_cov} of 4")
v.note("worst clear radius around a button centre",
       f"{_press_worst:.2f} mm (rev 5 measured 2.35 at FINGER_W 10.00)")

for _i, (_mx, _my) in enumerate(P.MOUNTS, 1):
    _cx, _cy = P.bxy(_mx, _my)
    v.check(f"screw hole {_i} is open", not _solid(_cx, _cy), "not open")
    v.check(f"screw hole {_i} still has material around it",
            _solid(_cx + SCREW_CLR_D / 2 + 1.5, _cy)
            or _solid(_cx - SCREW_CLR_D / 2 - 1.5, _cy),
            "the notch swallowed the screw boss")

# ---- no unexpected square corner -----------------------------------------
_lines = [e for e in PROFILE.edges() if e.geom_type == GeomType.LINE]
_arcs = [e for e in PROFILE.edges() if e.geom_type == GeomType.CIRCLE]

# THE VENT FIELD IS EXCLUDED BEFORE THE LOOP, NOT AFTER IT. A polygonal vent
# pattern turns one circle into ~130 straight edges, and this test is
# vertices x lines: filtering afterwards still costs 2000 x 2000 OCC point
# evaluations and the build simply stopped, with no error and no output. A
# non-circular vent is straight-sided ON PURPOSE, so its vertices were never
# in scope -- dropping them up front is both faster and more honest than
# computing them and then forgiving them.
_vx0, _vx1, _vy0, _vy1 = VENT_STATS["bounds"]


def _in_vent(px, py):
    return (_vx0 - 0.1 <= px <= _vx1 + 0.1) and (_vy0 - 0.1 <= py <= _vy1 + 0.1)


_outline_lines = [e for e in _lines
                  if not _in_vent((e @ 0.5).X, (e @ 0.5).Y)]
_sharp = []
for _v in PROFILE.vertices():
    if _in_vent(_v.X, _v.Y):
        continue
    _touch = [e for e in _outline_lines
              if min((Vector(_v.to_tuple()) - (e @ t)).length
                     for t in (0.0, 1.0)) < 1e-6]
    if len(_touch) >= 2:
        _sharp.append((round(_v.X, 3), round(_v.Y, 3)))
_ALLOWED = {(round(x, 3), round(y, 3))
            for x in (round(P0, 3), round(P1, 3))
            for y in (round(P0, 3), round(P1, 3))}
_unexpected = [c for c in _sharp if c not in _ALLOWED]
v.check("no unexpected square corner on the outline",
        not _unexpected, f"sharp corners at {_unexpected}")
v.note("profile", f"{len(_lines)} straight cuts, {len(_arcs)} arcs")

# ---- CHANGE 5: the engrave clears everything -----------------------------
_ks, _vu = keepouts(VENT_GEOMS)
_ink = ENGRAVE_INFO["ink"]
v.check("the engrave produced geometry", len(_ink) > 0, "no text wires")
_worst = _clearance_of(_ink, _ks, _vu)
# THE DILATION IS SUBTRACTED HERE TOO. _ink is centrelines for a stroke font,
# and the widest stroke on the panel is what the mark actually grows by, so
# the whole-layout figures are reported as MARKED clearance, not pen-path
# clearance. Zero for the outline fonts, so variants 1-20 are unaffected.
_DILATE = max([b["stroke_w"] for b in ENGRAVE_INFO["blocks"]] + [0.0]) / 2.0
for _name in sorted(_worst):
    _d = _worst[_name][0] - _DILATE
    v.check(f"engrave keeps >= {ENGRAVE_CLR:.2f} mm clear of the {_name}",
            _d >= ENGRAVE_CLR - 1e-9, f"only {_d:.3f} mm")
if _DILATE > 0:
    v.note("engrave clearances are MARKED clearances",
           f"every figure below already has half the {2 * _DILATE:.3f} mm "
           f"stroke width taken off the pen path")

_ex0, _ex1 = min(p[0] for p in _ink), max(p[0] for p in _ink)
_ey0, _ey1 = min(p[1] for p in _ink), max(p[1] for p in _ink)
v.check("the engrave is inside the panel outline",
        P0 < _ex0 and _ex1 < P1 and P0 < _ey0 and _ey1 < P1,
        f"ink spans {_ex0:.2f}..{_ex1:.2f} x {_ey0:.2f}..{_ey1:.2f}")
v.check("every block is at a readable size",
        all(b["size"] >= ENGRAVE_MIN_SIZE for b in ENGRAVE_INFO["blocks"]),
        f"{[b['size'] for b in ENGRAVE_INFO['blocks']]}")

# THE GLYPH CHECKS SPLIT BY FONT, and the stroke ones are not weaker for it.
# The outline checks (one contour per glyph, four counters) describe filled
# letters and are simply FALSE of a pen font -- left in place they would have
# gone on "passing" while testing nothing at all.
if not ENGRAVE_INFO["stroke_font"]:
    v.check("one outer contour per visible glyph",
            ENGRAVE_INFO["glyphs"] == sum(
                len(b["text"].replace(" ", ""))
                for b in ENGRAVE_LAYOUTS[ENGRAVE_VARIANT][2]),
            f"{ENGRAVE_INFO['glyphs']} faces")
else:
    _blocks_cfg = ENGRAVE_LAYOUTS[ENGRAVE_VARIANT][2]
    _want_seg = sum(NEWSTROKE["strings"][b["text"]]["segments"]
                    for b in _blocks_cfg)
    _got_seg = sum(len(pl) - 1 for pl in ENGRAVE_INFO["polylines"])
    v.check("every newstroke segment survived placement",
            _got_seg == _want_seg,
            f"{_got_seg} segments drawn, KiCad emitted {_want_seg}")
    v.check("the engrave carries no filled faces (it is a pen font)",
            ENGRAVE_INFO["glyphs"] == 0 and ENGRAVE_INFO["counters"] == 0,
            f"{ENGRAVE_INFO['glyphs']} faces appeared in a stroke font")
    # Every block's stroke width must be the board's own ratio, or the mark
    # will not look like the silkscreen it is supposed to match.
    for _b in ENGRAVE_INFO["blocks"]:
        v.check(f"{_b['text']!r} stroke width is the board's {NEWSTROKE_RATIO} "
                f"ratio",
                abs(_b["stroke_w"] - _b["size"] * NEWSTROKE_RATIO) < 1e-3,
                f"{_b['stroke_w']} for size {_b['size']}")
    # THE FLANKING PAIR IS ONE SIZE. This is the whole point of the group
    # lock and it is the thing a reader would notice instantly if it broke.
    _cred = [b for b in ENGRAVE_INFO["blocks"] if b["group"] == "credit"]
    v.check("SAWYER and SMITH are set at exactly the same size",
            len(_cred) == 2 and abs(_cred[0]["size"] - _cred[1]["size"]) < 1e-9,
            f"{[b['size'] for b in _cred]}")
    # ...and they really do flank the port rather than sitting both sides of
    # nothing: one ends left of the aperture, the other starts right of it.
    _sw = [b for b in ENGRAVE_INFO["blocks"] if b["text"] == "SAWYER"][0]
    _sm = [b for b in ENGRAVE_INFO["blocks"] if b["text"] == "SMITH"][0]
    v.check("SAWYER sits left of the J9 aperture and SMITH right of it",
            _sw["worst"].get("J9 aperture", 9) >= ENGRAVE_CLR
            and _sm["worst"].get("J9 aperture", 9) >= ENGRAVE_CLR,
            "the credit does not flank the port")
    # ...AND EACH WORD IS CENTRED IN ITS OWN CLEAR SPAN. Equal gap at the port
    # is NOT the same thing as centred, because the two words are different
    # lengths -- that is exactly the defect this gate exists to make
    # impossible. The span is aperture edge to corner-standoff keep-out, the
    # same rectangles the clearance checker measures against.
    for _b, _span, _port_side in ((_sw, CREDIT_SPAN_L, 1),
                                  (_sm, CREDIT_SPAN_R, 0)):
        _lo = _b["x0"] - _span[0]
        _hi = _span[1] - _b["x1"]
        _port, _post = (_hi, _lo) if _port_side else (_lo, _hi)
        v.check(f"{_b['text']!r} is centred in its span "
                f"{_span[0]:.2f}..{_span[1]:.2f}",
                abs(_lo - _hi) < 0.005,
                f"margins {_lo:.3f} / {_hi:.3f}")
        v.note(f"{_b['text']} margins",
               f"{_post:.3f} mm to the corner standoff, {_port:.3f} mm to the "
               f"J9 aperture (span {_span[1] - _span[0]:.2f} mm, ink "
               f"{_b['w']:.3f} mm)")
v.note("engrave blocks", "; ".join(
    f"{b['text']!r} at {b['size']:.2f} em (asked {b['asked']:.2f}), "
    f"caps {b['cap_h']:.2f}, {b['w']:.2f} wide" for b in ENGRAVE_INFO["blocks"]))
v.note("engrave worst clearance",
       ", ".join(f"{k} {_worst[k][0]:.2f}" for k in sorted(_worst)))

v.check("the engrave did not change the cut profile",
        abs(PROFILE.area - _top.area) < 1e-9,
        "PROFILE no longer matches the face it was taken from")

# ---- CHANGE 4: THE MIRROR, CHECKED, NOT ASSUMED --------------------------
# A mirror is exactly the kind of operation that looks right in a picture and
# is wrong in the file, so it is measured on both layers.
_cut_bb, _mcut_bb = PROFILE.bounding_box(), DXF_CUT.bounding_box()
v.check("the mirrored CUT is the same part, not a resized one",
        abs(DXF_CUT.area - PROFILE.area) < 1e-6
        and abs((_mcut_bb.max.X - _mcut_bb.min.X)
                - (_cut_bb.max.X - _cut_bb.min.X)) < 1e-9,
        "the mirror changed the profile's size")
v.check("the mirrored CUT is reflected about x=37.00",
        abs(_mcut_bb.min.X - (2 * PANEL_CX - _cut_bb.max.X)) < 1e-6,
        f"mirrored profile starts at x={_mcut_bb.min.X:.4f}")
if VENT_VARIANT == 21:
    _mv = 2 * PANEL_CX - max(x for x, _ in VENT_CENTRES)
    v.check("the vent field lands at the mirrored X in the DXF frame",
            abs(_mv - (2 * PANEL_CX - 68.0)) < 1e-9, f"{_mv:.2f}")

_mink = []
for _w in DXF_ENGRAVE:
    for _e in _w.edges():
        for _k in range(9):
            _p = _e @ (_k / 8)
            _mink.append((_p.X, _p.Y))
_mx0, _mx1 = min(p[0] for p in _mink), max(p[0] for p in _mink)
v.check("the ENGRAVE ink box is mirrored about x=37.00 too",
        abs(_mx0 - (2 * PANEL_CX - _ex1)) < 0.02
        and abs(_mx1 - (2 * PANEL_CX - _ex0)) < 0.02,
        f"engrave mirrors to {_mx0:.3f}..{_mx1:.3f}, expected "
        f"{2 * PANEL_CX - _ex1:.3f}..{2 * PANEL_CX - _ex0:.3f}")
v.check("CUT and ENGRAVE are in the SAME frame",
        abs(_mcut_bb.min.X - (2 * PANEL_CX - _cut_bb.max.X)) < 1e-6
        and abs(_mx0 - (2 * PANEL_CX - _ex1)) < 0.02,
        "one layer was mirrored and the other was not -- the classic way to "
        "scrap a sheet")
v.note("DXF frame", "INSIDE FACE UP: both layers mirrored about x=37.00. Lay "
                    "the sheet inner-face-up on the bed. In this drawing the "
                    "vent reads top-LEFT and the text reads BACKWARDS -- that "
                    "is the correct appearance.")

# ---- THE PARITY THAT THIS REVISION DELIBERATELY BREAKS -------------------
# Rev 5's two backs shared one PROFILE object so they could not drift. Rev 6
# changes the acrylic only, on request, so they HAVE drifted -- and the honest
# thing is to measure by how much and say so every run, not to quietly stop
# checking.
PARITY = {}
try:
    sys.path.insert(0, HERE)
    import importlib.util
    _spec = importlib.util.spec_from_file_location(
        "_rev5ab", os.path.join(HERE, "4square-case-rev5-acrylic-back.py"))
    _r5 = importlib.util.module_from_spec(_spec)
    _spec.loader.exec_module(_r5)
    PARITY = {
        "rev5_profile_area": round(_r5.PROFILE.area, 4),
        "rev6_profile_area": round(PROFILE.area, 4),
        "delta_area": round(PROFILE.area - _r5.PROFILE.area, 4),
        "rev5_vents": len(_r5.VENTS), "rev6_vents": len(VENT_GEOMS),
        "rev5_vent_d": _r5.VENT_D, "rev6_vent_d": VENT_D,
        "rev5_finger_w": _r5.FINGER_W, "rev6_finger_w": FINGER_W,
        "rev5_inside": round((_r5.P1 - _r5.FINGER_W)
                             - (_r5.P0 + _r5.FINGER_W), 4),
        "rev6_inside": round(BRIDGE, 4),
    }
except Exception as _e:                                          # noqa: BLE001
    PARITY = {"error": str(_e)}

v.note("PARITY WITH THE PRINTED BACK -- DELIBERATELY BROKEN",
       f"4square-case-rev5-printed-back.py imports rev 5's PROFILE and is "
       f"NOT changed by this file. Profile area {PARITY.get('rev5_profile_area')} "
       f"-> {PARITY.get('rev6_profile_area')} mm2 "
       f"(delta {PARITY.get('delta_area')}). The printed back keeps 35 x "
       f"Ø2.20 vents and 10.00 mm notches. Only the ACRYLIC was asked for.")

v.report()

if __name__ == "__main__":
    import json
    cadkit.export(part, OUT, verifier=v, sheet=SHEET)

    # ---- THE LAYER COLOURS ARE THE CUTTER'S INSTRUCTION, NOT DECORATION ----
    # RED = CUT THROUGH, BLUE = MARK. Every laser front-end keys its power and
    # speed table off the colour, so this is the one place in the drawing where
    # a wrong attribute quietly becomes a wrong operation on a real sheet.
    #
    # 2026-08-19: CUT was ACI 7 (black/white) here and in rev 5's exporter --
    # rev 6 did not diverge, it inherited it, and rev 5's 08-17 order went out
    # that way. The user's requirement is RED, so both layers are now stated
    # explicitly and the written file is read back and checked below rather
    # than trusted. ONE shape carries the whole cut -- outline, screw holes,
    # notches and vents are all faces of DXF_CUT -- so putting the colour on
    # the layer covers "border/outline included" by construction, and every
    # entity is left BYLAYER so nothing can override it locally.
    DXF_LAYER_COLORS = {"CUT": ColorIndex.RED, "ENGRAVE": ColorIndex.BLUE}

    exporter = ExportDXF(unit=Unit.MM)
    for _lname, _lcol in DXF_LAYER_COLORS.items():
        exporter.add_layer(_lname, color=_lcol)
    exporter.add_shape(DXF_CUT.faces()[0], layer="CUT")
    exporter.add_shape(DXF_ENGRAVE, layer="ENGRAVE")
    exporter.write(OUT + ".dxf")

    # ---- READ THE WRITTEN FILE BACK. A colour set on the exporter object is
    # not evidence about the file on disk, and the file is what goes to the
    # cutter. Fails loudly rather than shipping a black cut layer again.
    import ezdxf as _ezdxf
    _doc = _ezdxf.readfile(OUT + ".dxf")
    _tbl = {l.dxf.name: l.dxf.color for l in _doc.layers}
    for _lname, _lcol in DXF_LAYER_COLORS.items():
        assert _tbl.get(_lname) == _lcol.value, (
            f"DXF layer {_lname} came out ACI {_tbl.get(_lname)}, "
            f"expected {_lcol.value}")
    _stray = sorted({(e.dxf.layer, e.dxf.color) for e in _doc.modelspace()
                     if e.dxf.hasattr("color") and e.dxf.color != 256})
    assert not _stray, f"entities override their layer colour: {_stray}"
    _counts = {}
    for _e in _doc.modelspace():
        _counts[_e.dxf.layer] = _counts.get(_e.dxf.layer, 0) + 1

    print(f"wrote {OUT}.dxf   <- send THIS to the laser")
    print("     FRAME = INSIDE FACE UP. Both layers are mirrored about "
          "x=37.00.")
    print("     Lay the sheet INNER FACE UP. CUT = through, ENGRAVE = mark.")
    print("     In this DXF the vents read top-LEFT and the text reads "
          "BACKWARDS; that is correct.")
    print("     LAYER COLOURS, read back out of the written file: "
          + ", ".join(f"{k}=ACI {_tbl[k]} ({_counts.get(k, 0)} entities)"
                      for k in DXF_LAYER_COLORS)
          + "; every entity BYLAYER.")
    print("     RED = cut through (outline, screw holes, notches, vents). "
          "BLUE = engrave.")

    with open(OUT + "-flags.json", "w") as fh:
        json.dump({
            "engrave_variant": ENGRAVE_VARIANT,
            "engrave_layout": ENGRAVE_INFO["name"],
            "vent_variant": VENT_VARIANT,
            "vent_pattern": VENT_PATTERNS[VENT_VARIANT][0],
            "vent_open_pct": round(VENT_STATS["open_pct"], 2),
            "vent_web_min": (None if math.isnan(VENT_STATS["web"])
                             else round(VENT_STATS["web"], 3)),
            "led_overlap_mm2": round(LED_OVERLAP, 3),
            "led_gap_mm": round(LED_GAP, 3),
            "notch_inside_rev5": round(BRIDGE_REV5, 3),
            "notch_inside_rev6": round(BRIDGE, 3),
            "vent_d_rev5": VENT_D_REV5, "vent_d_rev6": VENT_D,
            "holes_removed": N_COL_DROPPED + N_ROW_DROPPED,
            "dxf_frame": "INSIDE FACE UP (both layers mirrored about x=37.00)",
            "parity_with_printed_back": PARITY,
            "known_flags": [
                "the printed back still uses rev 5's profile -- the two backs "
                "no longer share one outline, by request",
                (f"vent {VENT_VARIANT} ({VENT_PATTERNS[VENT_VARIANT][0]}) "
                 f"crosses the LED courtyards by "
                 f"{LED_OVERLAP:.1f} mm2 -- CHOSEN KNOWING THIS; the acrylic "
                 f"is clear, so an opening over a LED is a window, not a "
                 f"fault. Variants 24-34 are the sensor-local alternative.")
                if VENT_VARIANT not in VENT_SENSOR_LOCAL else
                "vent is sensor-local and clear of every LED",
            ],
        }, fh, indent=2)
    print(f"wrote {OUT}-flags.json")
