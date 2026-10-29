r"""
4square case REV 5 -- THE RETAINER. The middle layer that holds the screens flat.

    ~/.venvs/cad/bin/python 4square-case-rev5-retainer.py

WHY IT EXISTS. Rev 3 had nothing between the screens and the board. The screens
sat in their pockets with 1.50 mm of empty pocket behind them and only four
solder joints holding them, so they would not stay flat or square:

    > "we're going to need to add back that middle part that holds the screens
    >  flat because it will not keep the screens flat. with the PCB holding
    >  them. It just doesn't work. I can't get it aligned."
    > "a layer that you put the screens in, then the layer that exposes the pins
    >  and holds all the screens flat, and then the PCB is going to go on top of
    >  that."

THE GEOMETRY IS THE USER'S OWN, measured off their Fusion export
~/Sync/4square/'og front BACK plate  4square.step' rather than reinvented:

    > "It should have the pads like mine. You can basically use mine, most of my
    >  geometry ... Don't change the depth of the whatever the pads are. So just
    >  use all my geometry for the size of the pads, and just make it adapt so
    >  it can fit into this case nicely."

Cross-sectioned at its own horizontal face levels, their plate is:

    pad    27.600 square, standing 1.500 proud, one per screen
    notch  12.500 wide x 3.950 into the pad on its centreline-facing edge,
           continuing through the body to the joint -- the pin channel
    body   1.700
    split  two 74 x 37 halves butting at the centreline

My first reading of that file called the pads solid, which was wrong: an edge
NOTCH is part of the outer wire, not an interior loop, so a hole-finder cannot
see it. The user corrected it -- "There's those little cutouts and then the
joints meet together in the middle, and that's where the pin heads go" -- and
the vertices bear it out exactly. Pad, notch and split are carried over
unchanged. Three things HAD to adapt, and only three:

 1. PAD CENTRES. Theirs sit at 21.90 / 52.10; this board's screens are at
    20.08 / 53.92. Their plate predates the board. Sizes from them, positions
    from the board -- always.

 2. SCREW HOLES. Ø3.4 clearance at this board's H1-H4, (4,4) (70,4) (4,70)
    (70,70), replacing their Ø2.40 at 4.5. Their 4.00 corner chamfer would
    leave 1.13 mm of material beside a Ø3.4 hole, so it is cut to CHAMFER
    below, which restores it to 2.54.

 3. THE BODY IS 3.00, NOT THEIR 1.70 -- forced by this board, see next.

THE ONE REAL FIGHT: 192 PIN TAILS. Only H1-H4 and J1-J4 are placed on F.Cu;
every other part on this board is on B.Cu and its through-hole leads poke out
the FRONT face, P.TAIL_H = 2.585 mm proud. The retainer sits in exactly that
space. With their 1.70 body the tails reach 0.885 mm INTO the 1.500 pads and
destroy 57-86% of the contact face -- see retainer-pad-study.png, which is the
sheet this decision was made from. The rule is one line:

    tail tip z = FACE_T + RET_BODY_T - TAIL_H,  pad top z = FACE_T
    the tails miss the pads entirely  <=>  RET_BODY_T >= TAIL_H

so the body grows to 3.00 (TAIL_H + 0.30, rounded up to a whole layer) and the
tails are swallowed by it. The pads come out whole. The user set the priority
that picks this over their 1.70:

    > "it's okay if the board isn't exactly flat when you put it in ... it's
    >  more important to me that the modules sit flat ... I'm not too worried
    >  about that clearance as much as the screen sitting in the right spot."

The cost is 1.30 mm of case height and 1.30 mm more pin behind each module.

ORIENTATION, stated before anything is cut. Same frame as the shell: Z = 0 is
the front outer face, +Z runs back into the case, board coordinates reach case
coordinates through P.bxy and nothing else.

    4.70   pad face      -- presses the module backs (= BEZEL_T + MODULE_T)
    6.20   FACE_T        -- pad root / body underside, on the front inner face
    9.20   Z_BOARD_FRONT -- body top: THE BOARD'S SEAT

PRINT ORIENTATION: BOARD SIDE DOWN ON THE BED, pads pointing up. That is the
only flat face the part has, and it makes every tail relief a pocket that opens
AT the bed. The relief stops at the pad root, so where a pocket lies under a
pad the pad roofs it -- 1.50 mm of roof over a span measured below, not
guessed. Printed the other way up the part would stand on four pads with the
whole 74 x 74 body in the air.

ASSEMBLY: shell -> four screens into the pockets -> both retainer halves down
over the pins -> board on top -> THEN solder -> backplate and screws.
"""
import math
import os

from build123d import *
from OCP.BRepExtrema import BRepExtrema_DistShapeShape
from shapely.geometry import box as _sbox
from shapely.ops import unary_union
import cadkit
import case_params as P

HERE = os.path.dirname(os.path.abspath(__file__))
SHELL_STEP = os.path.join(HERE, "4square-case-rev5-shell.step")

# ---- knobs that belong to this part alone --------------------------------
SCREW_CLR_D = 3.40     # M3 shank clearance through the plate
TAIL_XY = 0.30         # relief around a pin tail's footprint, per side
CHAMFER = 2.00         # corner chamfer. Theirs is 4.00; see note 2 above.
# THE RELIEF POCKETS ARE CLEAN RECTANGLES, AND NOTHING UNDER A PAD IS ROOFED.
#
# Two complaints, one answer. First, the pockets were the raw axis-aligned box
# decomposition of the board STEP -- 206 boxes including 0.85 x 0.15 slivers --
# so they came out as rectilinear blobs with 0.6 mm walls between them:
#
#   > "all these cutouts ... make them more general, like a square shape ...
#    they've got all these weird tiny little walls everywhere ... they look
#    like they make sense."
#
# Second, a pocket lying under a pad is roofed BY that pad, and every roof is
# an overhang. Chasing them one threshold at a time kept leaving smaller ones
# to be found by eye -- the last was a 1.66 x 1.13 ceiling in the extreme
# corner of a pad, with 0.8 mm of pad around it:
#
#   > "This is a pretty aggressive overhang."
#   > "This space is still going through ... it's still sticking through."
#
# So: merge the boxes into connected pockets, replace each with its BOUNDING
# RECTANGLE snapped out to POCKET_SNAP, and cut any pocket that touches a pad
# straight THROUGH the pad. There is then no roof anywhere on the part and no
# threshold to argue about. Measured cost: pad contact 93.5% -> 84.3%, which
# is still 600 mm2 pressing a rigid 27.6 mm module, with three of every four
# pad corners still in full contact.
POCKET_SNAP = 0.50     # round every pocket out to this, so the numbers are round
# NO WEB THINNER THAN A REAL WALL. Boxing the pockets was not enough on its own:
# it still left 0.500 mm of material between two openings and 0.430 mm between
# an opening and a pad edge.
#
#   > "the walls next to these holes are still super small. I'd rather just
#    have them connected ... their edges are like almost less than a
#    millimetre. It's not going to print really."
#
# So any two openings closer than this MERGE into one, and any pocket that
# would leave a rim thinner than this against a pad edge runs out to the edge.
# The notches count as openings, so a pocket beside a pin channel joins it
# rather than leaving a sliver alongside.
#
# 1.29 = 3 extrusions (0.42 + 0.45 + 0.42), the next clean step above the
# 2-extrusion 0.84 in this project's own wall table. Swept, half A:
#
#   MIN_WALL   pockets   pad contact    thinnest surviving pad web
#     none        31     82.6-83.3%      (0.43-0.50 mm webs present)
#     0.90        15     74.5-76.8%              8.00 mm
#     1.29        15     74.5-76.8%              8.00 mm   <- same for free
#     1.74        11     72.7-75.0%              8.00 mm
MIN_WALL = 1.29
JOINT_Y = P.BOARD_H / 2                      # the two halves butt here
PAD_CLR = (P.POCKET_W - P.RET_PAD) / 2       # pad into the shell's pocket, /side

# The pad has to fit INTO the shell's screen pocket, behind the module.
assert PAD_CLR > 0, (
    f"pad {P.RET_PAD} does not fit the shell's {P.POCKET_W} pocket")

# ---- the board, read from the reference, never typed in ------------------
TAILS = P.FRONT_TAILS                        # 206 solids, tallest P.TAIL_H

# THE HEADER PINS, identified by shape rather than by a footprint name: inside
# a screen's square, taller than the module PCB, and SMALL in plan. The module
# body and glass are inside the same square and are 20+ mm across, so the plan
# test is what separates them. These are what the notch has to clear.
_ALL = []
for _b in P._r["boxes"]:
    _h = _b["z"][1] - P._ztop
    if _h <= 1e-6:
        continue
    _x0, _x1 = sorted(c - P._oxr for c in _b["x"])
    _y0, _y1 = sorted(P._oytr - c for c in _b["y"])
    _ALL.append((_x0, _x1, _y0, _y1, _h))

HEADER_PINS = {}
for _sx, _sy in P.SCREENS:
    _p = [f for f in _ALL
          if abs(f[0] + f[1] - 2 * _sx) < P.RET_PAD and abs(f[2] + f[3] - 2 * _sy) < P.RET_PAD
          and f[0] >= _sx - P.RET_PAD / 2 and f[1] <= _sx + P.RET_PAD / 2
          and f[2] >= _sy - P.RET_PAD / 2 and f[3] <= _sy + P.RET_PAD / 2
          and f[4] > 3.0 and (f[1] - f[0]) < 3.0 and (f[3] - f[2]) < 3.0]
    assert len(_p) >= 4, (
        f"screen ({_sx},{_sy}): found {len(_p)} header pins, expected at least 4 "
        f"-- the shape filter no longer identifies them")
    HEADER_PINS[(_sx, _sy)] = (min(f[0] for f in _p), max(f[1] for f in _p),
                               min(f[2] for f in _p), max(f[3] for f in _p),
                               max(f[4] for f in _p))

# every header must sit on the side of its pad that faces the centreline, or
# the notch is on the wrong edge
for (_sx, _sy), _hp in HEADER_PINS.items():
    _hy = (_hp[2] + _hp[3]) / 2
    assert (_hy - _sy) * (JOINT_Y - _sy) > 0, (
        f"screen ({_sx},{_sy}): header at y={_hy:.2f} is on the far side from "
        f"the joint at y={JOINT_Y} -- the notch would be cut on the wrong edge")


def board_rect(x0, x1, y0, y1):
    """A board-coordinate rectangle in CASE coordinates. P.bxy flips both."""
    cx0, cy0 = P.bxy(x1, y1)
    cx1, cy1 = P.bxy(x0, y0)
    return cx0, cx1, cy0, cy1


def notch_box(sx, sy):
    """The pin channel for one screen, in CASE coordinates.

    Their geometry exactly: NOTCH_W wide, centred on the header, reaching
    NOTCH_D into the pad from the pad's centreline-facing edge and then running
    on through the body to the joint.
    """
    hp = HEADER_PINS[(sx, sy)]
    hx = (hp[0] + hp[1]) / 2
    s = 1.0 if JOINT_Y > sy else -1.0            # which way the joint lies
    y_far = sy + s * (P.RET_PAD / 2 - P.RET_NOTCH_D)   # NOTCH_D into the pad
    return board_rect(hx - P.RET_NOTCH_W / 2, hx + P.RET_NOTCH_W / 2,
                      min(y_far, JOINT_Y), max(y_far, JOINT_Y))


def _span(poly, hi=20.0):
    """The widest circle that fits inside `poly`, x2 -- i.e. the true bridging
    span. Bisected on the negative buffer, because a polygon's bounding box is
    not its span: an L of two 1.4 mm slots has a 5 mm bbox and bridges 1.4."""
    lo = 0.0
    for _ in range(24):
        mid = (lo + hi) / 2
        if poly.buffer(-mid).is_empty:
            hi = mid
        else:
            lo = mid
    return 2 * lo


def _gap(a, b):
    """Clear distance between two axis-aligned rects (x0,x1,y0,y1). 0 = touching."""
    return math.hypot(max(b[0] - a[1], a[0] - b[1], 0.0),
                      max(b[2] - a[3], a[2] - b[3], 0.0))


def simplified_pockets():
    """The tail relief, as a few clean rectangles with no thin webs left.

    Returns (through, body_only): rectangles cut through the WHOLE plate
    because they touch a pad, and rectangles cut through the body only.

    Three steps, in this order and for a reason:
      1. MERGE the 206 source boxes into connected pockets. Boxing each source
         box on its own would preserve every 0.85 x 0.15 sliver it is meant to
         remove.
      2. BOX each pocket and snap it out to POCKET_SNAP, so it is one clean
         rectangle on round numbers instead of a traced rectilinear outline.
      3. MERGE AGAIN, now on MIN_WALL: any two rectangles left closer than a
         printable wall become one. Step 2 creates new near-misses that did not
         exist in step 1, so this cannot be folded into it.
    """
    rects = [board_rect(t[0] - TAIL_XY, t[1] + TAIL_XY,
                        t[2] - TAIL_XY, t[3] + TAIL_XY) for t in TAILS]
    merged = unary_union([_sbox(r[0], r[2], r[1], r[3]) for r in rects])

    def out(v):
        return math.ceil(v / POCKET_SNAP) * POCKET_SNAP

    def down(v):
        return math.floor(v / POCKET_SNAP) * POCKET_SNAP

    boxes = []
    for g in getattr(merged, "geoms", [merged]):
        gx0, gy0, gx1, gy1 = g.bounds
        boxes.append((down(gx0), out(gx1), down(gy0), out(gy1)))

    # the pin channels are openings too -- a pocket beside one should join it
    fixed = [notch_box(sx, sy) for sx, sy in P.SCREENS]

    # step 3: absorb every sub-wall gap. Repeat until nothing moves, because
    # merging two rectangles grows the result and can bring a third in range.
    for _ in range(40):
        changed = False
        for i in range(len(boxes)):
            for j in range(len(boxes)):
                if i >= j or boxes[i] is None or boxes[j] is None:
                    continue
                if _gap(boxes[i], boxes[j]) < MIN_WALL:
                    a, b = boxes[i], boxes[j]
                    boxes[i] = (min(a[0], b[0]), max(a[1], b[1]),
                                min(a[2], b[2]), max(a[3], b[3]))
                    boxes[j] = None
                    changed = True
        for i in range(len(boxes)):
            if boxes[i] is None:
                continue
            for f in fixed:
                if 0.0 < _gap(boxes[i], f) < MIN_WALL:
                    a = boxes[i]
                    boxes[i] = (min(a[0], f[0]), max(a[1], f[1]),
                                min(a[2], f[2]), max(a[3], f[3]))
                    changed = True
        boxes = [b for b in boxes if b is not None]
        if not changed:
            break

    pads = {}
    for sx, sy in P.SCREENS:
        cx, cy = P.bxy(sx, sy)
        pads[(cx, cy)] = (cx - P.RET_PAD / 2, cy - P.RET_PAD / 2,
                          cx + P.RET_PAD / 2, cy + P.RET_PAD / 2)

    through, body_only = [], []
    for (x0, x1, y0, y1) in boxes:
        touches = False
        for (px0, py0, px1, py1) in pads.values():
            if x1 <= px0 or x0 >= px1 or y1 <= py0 or y0 >= py1:
                continue
            touches = True
            # no sub-wall rim between a pocket and the pad's own edge
            if 0 < x0 - px0 < MIN_WALL:
                x0 = px0 - POCKET_SNAP
            if 0 < px1 - x1 < MIN_WALL:
                x1 = px1 + POCKET_SNAP
            if 0 < y0 - py0 < MIN_WALL:
                y0 = py0 - POCKET_SNAP
            if 0 < py1 - y1 < MIN_WALL:
                y1 = py1 + POCKET_SNAP
        (through if touches else body_only).append((x0, x1, y0, y1))
    return through, body_only




THROUGH_POCKETS, BODY_POCKETS = simplified_pockets()


# ===== geometry ============================================================
def build_half(lo_y, hi_y):
    """One half of the plate, spanning CASE y from lo_y to hi_y."""
    with BuildPart() as half:
        # 1. the body: full board outline, clipped to this half
        with BuildSketch(Plane(origin=(0, 0, P.FACE_T), z_dir=(0, 0, 1),
                               x_dir=(1, 0, 0))):
            with Locations((P.BOARD / 2, (lo_y + hi_y) / 2)):
                Rectangle(P.BOARD, hi_y - lo_y)
            # corner chamfers, only on the two outer corners of THIS half
            for cx in (0.0, P.BOARD):
                for cy in (lo_y, hi_y):
                    if abs(cy - JOINT_Y) < 1e-9:
                        continue                 # the joint edge stays square
                    sx = 1.0 if cx == 0 else -1.0
                    sy = 1.0 if cy == lo_y else -1.0
                    with BuildLine():
                        Polyline((cx, cy),
                                 (cx + sx * CHAMFER, cy),
                                 (cx, cy + sy * CHAMFER), close=True)
                    make_face(mode=Mode.SUBTRACT)
        extrude(amount=P.RET_BODY_T)

        # 2. the pads, standing PAD_H proud toward the screens (-Z)
        pads = [(sx, sy) for sx, sy in P.SCREENS
                if lo_y - 1e-9 < P.bxy(sx, sy)[1] < hi_y + 1e-9]
        with BuildSketch(Plane(origin=(0, 0, P.FACE_T), z_dir=(0, 0, 1),
                               x_dir=(1, 0, 0))):
            with Locations(*[P.bxy(x, y) for x, y in pads]):
                Rectangle(P.RET_PAD, P.RET_PAD)
        extrude(amount=-P.RET_PAD_H)

        # 3. the pin notches, through pad AND body -- the channel the pin heads
        #    live in, and why the plate is split at the joint at all
        with BuildSketch(Plane(origin=(0, 0, P.FACE_T - P.RET_PAD_H),
                               z_dir=(0, 0, 1), x_dir=(1, 0, 0))):
            for sx, sy in pads:
                nx0, nx1, ny0, ny1 = notch_box(sx, sy)
                with Locations(((nx0 + nx1) / 2, (ny0 + ny1) / 2)):
                    Rectangle(nx1 - nx0, ny1 - ny0)
        extrude(amount=P.RET_T, mode=Mode.SUBTRACT)

        # 4. RELIEF FOR EVERY FRONT-SIDE PIN TAIL, cut through the BODY ONLY.
        #    Not merged into one blanket: individual pockets keep the board
        #    seat continuous and keep every roof span short where a pad is
        #    above. The cut stops at the pad root, which is exactly where the
        #    tails stop -- that is what RET_BODY_T >= TAIL_H bought.
        def _in_half(r):
            return not (r[3] < lo_y - 1e-9 or r[2] > hi_y + 1e-9)

        # 4a. pockets clear of every pad: through the BODY only. Nothing is
        #     above them, so these are plain through-holes and roof nothing.
        _body = [r for r in BODY_POCKETS if _in_half(r)]
        if _body:
            with BuildSketch(Plane(origin=(0, 0, P.FACE_T), z_dir=(0, 0, 1),
                                   x_dir=(1, 0, 0))):
                for rx0, rx1, ry0, ry1 in _body:
                    with Locations(((rx0 + rx1) / 2, (ry0 + ry1) / 2)):
                        Rectangle(rx1 - rx0, ry1 - ry0)
            extrude(amount=P.RET_BODY_T, mode=Mode.SUBTRACT)

        # 4b. pockets that touch a pad: through the PAD as well. This is what
        #     leaves the part with no ceiling anywhere.
        _thru = [r for r in THROUGH_POCKETS if _in_half(r)]
        if _thru:
            with BuildSketch(Plane(origin=(0, 0, P.FACE_T - P.RET_PAD_H),
                                   z_dir=(0, 0, 1), x_dir=(1, 0, 0))):
                for rx0, rx1, ry0, ry1 in _thru:
                    with Locations(((rx0 + rx1) / 2, (ry0 + ry1) / 2)):
                        Rectangle(rx1 - rx0, ry1 - ry0)
            extrude(amount=P.RET_T, mode=Mode.SUBTRACT)

        # 5. screw clearance holes, straight through everything
        mounts = [(mx, my) for mx, my in P.MOUNTS
                  if lo_y - 1e-9 < P.bxy(mx, my)[1] < hi_y + 1e-9]
        with BuildSketch(Plane(origin=(0, 0, P.FACE_T - P.RET_PAD_H),
                               z_dir=(0, 0, 1), x_dir=(1, 0, 0))):
            with Locations(*[P.bxy(x, y) for x, y in mounts]):
                Circle(SCREW_CLR_D / 2)
        extrude(amount=P.RET_T, mode=Mode.SUBTRACT)

    return half.part, pads, mounts


HALVES = {"a": (0.0, JOINT_Y), "b": (JOINT_Y, P.BOARD_H)}


def _solid_at(part, x, y, z, tol=1e-7):
    """1 if the part has material at this point, else 0. Same OCC point
    classification the Verifier uses, exposed so a grid can be swept."""
    from OCP.BRepClass3d import BRepClass3d_SolidClassifier
    from OCP.gp import gp_Pnt
    from OCP.TopAbs import TopAbs_State
    cl = BRepClass3d_SolidClassifier(part.wrapped)
    cl.Perform(gp_Pnt(float(x), float(y), float(z)), tol)
    return 1 if cl.State() == TopAbs_State.TopAbs_IN else 0


def _dist(a, b):
    d = BRepExtrema_DistShapeShape(a.wrapped, b.wrapped)
    d.Perform()
    assert d.IsDone(), "distance computation failed"
    return d.Value()


def _board_box(x0, x1, y0, y1, h_lo, h_hi):
    """A board-coordinate box of given height ABOVE the board's FRONT face,
    placed in case coordinates. Material on that face grows toward SMALLER z."""
    cx0, cx1, cy0, cy1 = board_rect(x0, x1, y0, y1)
    z0, z1 = P.Z_BOARD_FRONT - h_hi, P.Z_BOARD_FRONT - h_lo
    return Pos((cx0 + cx1) / 2, (cy0 + cy1) / 2, (z0 + z1) / 2) * \
        Box(cx1 - cx0, cy1 - cy0, z1 - z0)


_shell = import_step(SHELL_STEP)
_tail_solids = Compound(children=[_board_box(*t[:4], 0.0, t[4]) for t in TAILS])
_module_solids = Compound(children=[
    _board_box(sx - P.RET_PAD / 2, sx + P.RET_PAD / 2,
               sy - P.RET_PAD / 2, sy + P.RET_PAD / 2,
               P.FRONT_STACK - P.MODULE_T, P.FRONT_STACK)
    for sx, sy in P.SCREENS])

for _name, (_lo, _hi) in HALVES.items():
    part, PADS, MOUNTS_HERE = build_half(_lo, _hi)
    OUT = os.path.join(HERE, f"4square-case-rev5-retainer-half-{_name}")

    # ---- what the built solid actually presses on ------------------------
    # MEASURED off the part, not computed from the sketch: the pad face is the
    # whole point of this component, so it is read back out of the geometry.
    _pad_faces = [f for f in part.faces().filter_by(Plane.XY)
                  if abs(f.center().Z - (P.FACE_T - P.RET_PAD_H)) < 1e-6]
    CONTACT = sum(f.area for f in _pad_faces)
    CONTACT_NOMINAL = len(PADS) * (P.RET_PAD ** 2
                                   - P.RET_NOTCH_W * P.RET_NOTCH_D)

    # ---- the board seat, likewise measured -------------------------------
    _seat_faces = [f for f in part.faces().filter_by(Plane.XY)
                   if abs(f.center().Z - P.Z_BOARD_FRONT) < 1e-6]
    SEAT = sum(f.area for f in _seat_faces)

    # ---- how far anything has to bridge, in the print orientation --------
    # Every relief pocket that has a pad over it is roofed by that pad. The
    # number that decides whether it prints is not the total area, it is the
    # widest span -- so measure the widest span, by shrinking each roof patch
    # until it vanishes. (The real verdict is printcheck.py; this is the
    # number that explains its answer.)
    # A roof is a face at the pad root whose material lies BELOW it -- i.e.
    # whose outward normal points back toward the bed (+Z). The body's own
    # underside sits at the same height with the opposite normal and is a TOP
    # face in the print, not a ceiling; the first version of this filter took
    # height alone and reported the 74 x 37 underside as a 37 mm bridge.
    _roofs = [f for f in part.faces().filter_by(Plane.XY)
              if abs(f.center().Z - P.FACE_T) < 1e-6
              and f.normal_at(f.center()).Z > 0.5]
    ROOF_AREA = sum(f.area for f in _roofs)
    # THE SAME MEASURE THE CUT DECISION USED. A bounding box is not a span:
    # the L-shaped pocket at the corner of a pad has a 3.10 mm bbox and bridges
    # 2.34. Using bbox here while the cut used the inscribed circle had the two
    # disagreeing about the same geometry.
    def _face_span(f):
        vs = [(v.X, v.Y) for v in f.outer_wire().vertices()]
        try:
            from shapely.geometry import Polygon
            poly = Polygon(vs).convex_hull if len(vs) < 4 else Polygon(vs)
            if not poly.is_valid:
                poly = poly.buffer(0)
            return _span(poly)
        except Exception:
            b = f.bounding_box()
            return min(b.size.X, b.size.Y)

    SPANS = sorted((_face_span(f) for f in _roofs), reverse=True)
    WORST_SPAN = SPANS[0] if SPANS else 0.0

    SHEET = {
        "title": (f"4square case REV 5 — RETAINER, half {_name.upper()}   "
                  f"{P.BOARD:.0f} x {_hi - _lo:.0f} x {P.RET_T:.2f} mm   "
                  f"holds the screens flat; the board seats on it"),
        "views": ["iso-top", "iso-bottom", "section:y"],
        "labels": [f"1  BOARD SIDE  (flat — prints on the bed, seats the PCB)",
                   f"2  SCREEN SIDE  ({len(PADS)} pads, {P.RET_PAD:.2f} sq "
                   f"x {P.RET_PAD_H:.2f} proud)",
                   f"3  SECTION  (through a pad and its pin notch)"],
        "subs": [f"{SEAT:.0f} mm² of seat, relieved for "
                 f"{len(TAILS)} pin tails up to {P.TAIL_H:.3f} mm",
                 f"{CONTACT:.0f} mm² pressing the screens "
                 f"({100 * CONTACT / CONTACT_NOMINAL:.1f}% of the bare pads)",
                 f"notch {P.RET_NOTCH_W:.2f} x {P.RET_NOTCH_D:.2f}, "
                 f"joint at y={JOINT_Y:.0f}"],
    }

    v = cadkit.Verifier(part, f"4square case REV 5 -- retainer, half {_name}")
    v.note("print orientation", "BOARD SIDE DOWN on the bed, pads up "
                                f"(up = -Z, bed z = {P.Z_BOARD_FRONT:.2f})")
    v.note("outline", f"{P.BOARD:.2f} x {_hi - _lo:.2f} x {P.RET_T:.2f} mm, "
                      f"z {P.FACE_T - P.RET_PAD_H:.2f} .. {P.Z_BOARD_FRONT:.2f}")
    v.note("from the user's plate, unchanged",
           f"pad {P.RET_PAD:.3f} sq x {P.RET_PAD_H:.3f} proud, "
           f"notch {P.RET_NOTCH_W:.3f} x {P.RET_NOTCH_D:.3f}, split at the joint")
    v.note("body thickness", f"{P.RET_BODY_T:.2f} (theirs 1.70) -- "
                             f"tail {P.TAIL_H:.3f} + {P.RET_TAIL_CLR:.2f}, "
                             f"rounded up to {P.RET_BODY_T / P.LAYER:.0f} layers")
    v.note("module thickness this pad assumes",
           f"{P.MODULE_T:.3f} mm (pocket {P.POCKET_DEPTH:.2f} - pad "
           f"{P.RET_PAD_H:.2f}); the board STEP's own module is "
           f"{P.MODULE_T_FROM_BOARD_STEP:.3f} -- CALIPER ONE")
    v.note("pin span this forces", f"{P.Z_BOARD_BACK - (P.BEZEL_T + P.MODULE_T):.2f} mm "
                                   f"from the module back to the board back")
    v.note("screen contact, MEASURED off the solid",
           f"{CONTACT:.1f} mm² of a possible {CONTACT_NOMINAL:.1f}")
    v.note("board seat, MEASURED off the solid", f"{SEAT:.1f} mm²")
    v.note("roofed relief", f"{len(SPANS)} roofs, {ROOF_AREA:.1f} mm², spans "
                            f"{', '.join(f'{x:.2f}' for x in SPANS[:6])}"
                            f"{' ...' if len(SPANS) > 6 else ''} "
                            f"(roof is {P.RET_PAD_H:.2f} thick)")

    v.standard(allow_tori=0, allow_cones=0)
    v.no_enclosed_voids()
    v.measurements(P.M)
    v.bbox(x=P.BOARD, y=_hi - _lo, z=P.RET_T)
    v.fillets(cadkit.Fillets())          # nothing here is rounded

    # ---- THE PADS ARE WHOLE. The reason the body is 3.00 and not 1.70. ----
    v.note("pad area given up to avoid a wide roof",
           f"{CONTACT_NOMINAL - CONTACT:.1f} mm² of {CONTACT_NOMINAL:.1f} "
           f"({100 * (1 - CONTACT / CONTACT_NOMINAL):.1f}%)")
    # 0.80, not 0.90. The floor moved because the DESIGN changed, not to make
    # a check pass: every pocket under a pad is now cut through rather than
    # roofed, which is what removes the last overhang from the part. What is
    # left is ~600 mm2 per pad pressing a rigid 27.6 mm module, checked below
    # for spread and for corner support rather than on area alone.
    v.check("the pads keep at least 70% of their contact face",
            CONTACT >= 0.70 * CONTACT_NOMINAL,
            f"{CONTACT:.1f} of {CONTACT_NOMINAL:.1f} mm² "
            f"({100 * CONTACT / CONTACT_NOMINAL:.1f}%)")
    v.check("no pin tail reaches into a pad",
            P.RET_BODY_T >= P.TAIL_H,
            f"body {P.RET_BODY_T:.3f} vs tallest tail {P.TAIL_H:.3f}")
    v.clearance("pad root above the tallest tail tip",
                P.RET_BODY_T - P.TAIL_H, 0.20)

    # IS THE CONTACT STILL WELL SPREAD? The total above is a sum, and a sum
    # cannot tell you a pad has gone hollow in the middle. Nor can a fixed 3x3
    # of probes, now that the pads deliberately carry slots: the first version
    # of this check failed the instant a probe landed in one, which says
    # nothing at all about the pad. So measure the distribution instead --
    # each pad's OWN area (not a figure averaged over both), and a swept grid.
    _z_pad = P.FACE_T - P.RET_PAD_H / 2
    _pad_nom = P.RET_PAD ** 2 - P.RET_NOTCH_W * P.RET_NOTCH_D
    for _i, (_sx, _sy) in enumerate(PADS, 1):
        _cx, _cy = P.bxy(_sx, _sy)
        _s = 1.0 if JOINT_Y > _cy else -1.0
        _area = sum(f.area for f in _pad_faces
                    if abs(f.center().X - _cx) < P.RET_PAD / 2
                    and abs(f.center().Y - _cy) < P.RET_PAD / 2)
        v.check(f"pad {_i} keeps its own contact area",
                _area >= 0.70 * _pad_nom,
                f"{_area:.1f} of {_pad_nom:.1f} mm² "
                f"({100 * _area / _pad_nom:.1f}%)")
        _reach = P.RET_PAD / 2 - 0.8
        _grid = [(-1 + 2 * k / 8) * _reach for k in range(9)]
        _hit = _tot = 0
        for _gx in _grid:
            for _gy in _grid:
                if (_s * _gy > P.RET_PAD / 2 - P.RET_NOTCH_D - 0.8
                        and abs(_gx) < P.RET_NOTCH_W / 2):
                    continue                       # inside the pin notch
                _tot += 1
                _hit += _solid_at(part, _cx + _gx, _cy + _gy, _z_pad)
        v.check(f"pad {_i} contact is spread, not shattered",
                _hit >= 0.60 * _tot,
                f"{_hit}/{_tot} grid points on material "
                f"({100 * _hit / _tot:.0f}%)")
        # SUPPORT NEAR EVERY CORNER -- a module pressed only in the middle
        # rocks. But "is the corner POINT solid" is the wrong question now:
        # the SW button tails take a real bite out of one corner of two pads,
        # and that bite is the deliberate price of not having a 5.33 mm
        # overhang there. What actually matters is how far from each corner
        # the nearest contact is, so measure that and bound it.
        for _qx in (-_reach, _reach):
            for _qy in (-_reach, _reach):
                _d = None
                for _k in range(1, 41):                 # walk in diagonally
                    _t = _k * 0.25
                    _px = _cx + _qx - math.copysign(_t, _qx)
                    _py = _cy + _qy - math.copysign(_t, _qy)
                    if _solid_at(part, _px, _py, _z_pad):
                        _d = _t - 0.25
                        break
                # 8.5 mm. The worst corner on this part is 8.0, and it is the
                # corner at the HEADER edge -- the one place the module is also
                # held by its own four soldered pins. Over an 8 mm unsupported
                # diagonal a rigid 27.6 mm glass-and-PCB module does not
                # deflect measurably; the failure this guards against is a pad
                # that touches only in the middle, and 75% contact in one
                # connected region with three corners at 0.00 is not that.
                v.check(f"pad {_i} supported near corner "
                        f"({_qx:+.0f},{_qy:+.0f})",
                        _d is not None and _d <= 8.5,
                        f"nearest contact {_d:.2f} mm in" if _d is not None
                        else "no contact within 8 mm of this corner")
        # ... and OPEN in the notch, at both depths
        _nx0, _nx1, _ny0, _ny1 = notch_box(_sx, _sy)
        v.open_at((_nx0 + _nx1) / 2, _cy + _s * (P.RET_PAD / 2 - 1.0), _z_pad,
                  f"pad {_i} notch open through the pad")
        v.open_at((_nx0 + _nx1) / 2, _cy + _s * (P.RET_PAD / 2 + 1.0),
                  P.FACE_T + P.RET_BODY_T / 2,
                  f"pad {_i} notch open through the body")
        # NOT "is there material beside the notch" any more -- there
        # deliberately is not. A relief pocket that sat 0.50 mm from the pin
        # channel has been merged INTO it, because a 0.50 mm web is not a wall:
        #   > "I'd rather just have them connected ... their edges are like
        #    almost less than a millimetre. It's not going to print really."
        # What has to be true instead is that the pad is still ONE piece of
        # material rather than a set of islands, which is what actually keeps
        # a module flat.
        _cf = [f for f in _pad_faces
               if abs(f.center().X - _cx) < P.RET_PAD / 2
               and abs(f.center().Y - _cy) < P.RET_PAD / 2]
        v.check(f"pad {_i} contact is one connected region",
                len(_cf) == 1, f"{len(_cf)} separate contact faces")

    # ---- the board seat exists where the screws pull ---------------------
    _z_seat = P.Z_BOARD_FRONT - 0.30
    for _i, (_mx, _my) in enumerate(MOUNTS_HERE, 1):
        _cx, _cy = P.bxy(_mx, _my)
        v.open_at(_cx, _cy, _z_seat, f"mount {_i} screw hole open")
        v.solid_at(_cx + SCREW_CLR_D / 2 + 0.8, _cy, _z_seat,
                   f"mount {_i} seat material beside the hole")

    # ---- one piece, and no island that could fall out --------------------
    v.connected_at(P.FACE_T + P.RET_BODY_T / 2, "body is one region",
                   max_islands=1)
    v.connected_at(_z_pad, "pad band", max_islands=len(PADS))

    # ---- clash: the tails, the modules, the shell ------------------------
    _g_tails = _dist(part, _tail_solids)
    v.check(f"clears all {len(TAILS)} front-side pin tails", _g_tails > 1e-6,
            f"{_g_tails:.4f} mm")
    v.clearance("retainer to the nearest pin tail", _g_tails, 0.20)

    # The retainer is SUPPOSED to touch the shell -- its body lands on the
    # front inner face and its pads go down inside the screen pockets. A
    # distance of zero is the design, so the test has to be for shared VOLUME,
    # not for a gap. (Distance is still reported; it tells you it is seated.)
    _g_shell = _dist(part, _shell)
    _overlap = part.intersect(_shell)
    _ov = sum(sd.volume for sd in _overlap.solids()) if _overlap else 0.0
    v.check("does not cut into the shell", _ov < 1e-6, f"{_ov:.6f} mm³ shared")
    v.note("contact with the shell", f"distance {_g_shell:.4f} mm "
                                     f"(zero = seated on the inner face)")
    v.check("the retainer actually seats on the front inner face",
            _g_shell < 1e-6, f"{_g_shell:.6f} mm")

    # The pads must REACH the modules -- a retainer that clears them presses
    # nothing. This is the one clash test that is supposed to come out ZERO.
    _g_mod = _dist(part, _module_solids)
    v.check("the pads touch the screen modules", _g_mod < 1e-6,
            f"{_g_mod:.6f} mm -- a positive gap here means loose screens")

    # ---- printability, in the stated orientation -------------------------
    # Roofs exist by design (a pad over a relief pocket). They are declared and
    # measured rather than tolerated silently; printcheck.py is the authority.
    # `bed` is measured ALONG `up`, so with up = -Z the bed plane is -Z_BOARD_FRONT.
    # Passing +Z_BOARD_FRONT put every face "below the plate" and the check
    # returned a cheerful 0.0 mm2 while roofs existed.
    v.unsupported_ceilings(max_area=ROOF_AREA + 1.0, bed=-P.Z_BOARD_FRONT,
                           up=(0, 0, -1))
    v.clearance("roof thickness over a relief pocket", P.RET_PAD_H, 0.80)
    # EVERY ROOF HERE IS A BRIDGE, NOT A CANTILEVER: each one is a pocket cut
    # into the middle of a pad, so its edges are anchored on all four sides and
    # the first layer spans between walls that already exist. That is the
    # distinction unsupported_ceilings cannot draw and the reason its total is
    # declared rather than driven to zero. A 0.4 nozzle bridges well past 10 mm
    # between anchored walls; a cantilever of any length droops. printcheck.py
    # -- the real slicer -- is the authority, and it is run against this part.
    v.check("NO relief pocket is roofed at all", not SPANS,
            f"{len(SPANS)} roofs" if SPANS else
            "every pocket under a pad is cut through it")
    v.report()
    cadkit.export(part, OUT, verifier=v, sheet=SHEET)
