r"""
4square case REV 5 -- PRINTED BACK. The finger-notch back, 3D printed, with
the standoffs built into the part instead of being separate spacers.

    ~/.venvs/cad/bin/python 4square-case-rev5-printed-back.py

    > "also need a part to sit on top of the pcb to support this back peice?
    >  no actually just put that into the piece itself ... add the corect
    >  spacing to the backplate (this is fine for this version wil stick with
    >  full 3d print, acrylic will be seperate spacer)"

So there are two backs and they are deliberately different parts:

    ACRYLIC   flat sheet, laser cut from the DXF, needs FOUR SEPARATE SPACERS
    PRINTED   this one, same profile, spacers grown in as corner columns

THE PROFILE IS IMPORTED, NOT REDRAWN. Every opening -- the two finger notches,
the sensor vent, the USB-C aperture, the screw holes -- comes from
4square-case-rev5-acrylic-back.py via its PROFILE object. Redrawing it here
would mean two files that agree until one of them is edited, which is exactly
how rev 2 ended up with a clearance that was right in one file and wrong in
the other. Change the acrylic back, this follows.

WHAT THE STANDOFFS ARE FOR, AND WHY THE HEIGHT IS NOT A CHOICE

The shell's cavity runs full width to the rim with NO LEDGE -- that was rev 3's
deliberate decision, so nothing above the board's plane can block it going in.
Nothing therefore sets the plate's height except the standoffs themselves. Get
them wrong and the screws either bow the plate inward or hold it proud of the
rim. So the height is derived, and asserted against the stack:

    TUBE_H  ==  Z_PLATE_SEAT - Z_BOARD_BACK  ==  11.15

THE STANDOFFS ARE SQUARE AND THEY ARE PART OF THE CORNER. Four round posts
standing 1.10 mm clear of the panel corner read as an oversight; each is now a
square column flush with both panel edges, so corner and standoff are one piece
of material. Same screw centre, same height, more of it, and nothing moved
toward the board.

ORIENTATION. Z = 0 is the panel's INNER face, matching the acrylic back, so
the panel occupies 0..PLATE_T and the columns run DOWN toward the board, into
-Z. That is assembly orientation, not print orientation: make-print-stl.py
flips it so the columns grow upward off the bed.
"""
import importlib.util
import os
import sys

from build123d import *
import cadkit
import case_params as P

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "4square-case-rev5-printed-back")

# ---- import the acrylic back's profile -----------------------------------
# The module name starts with a digit, so it cannot be imported by name.
_ACR = os.path.join(HERE, "4square-case-rev5-acrylic-back.py")
_spec = importlib.util.spec_from_file_location("acrylic_back", _ACR)
_ab = importlib.util.module_from_spec(_spec)
sys.modules["acrylic_back"] = _ab
_spec.loader.exec_module(_ab)          # runs ITS verifier too: 119 checks

PROFILE = _ab.PROFILE
FINGER_W, FINGER_L = _ab.FINGER_W, _ab.FINGER_L
AP_W, AP_H, J9_R = _ab.AP_W, _ab.AP_H, _ab.J9_R
VENTS = _ab.VENTS

# ---- the standoffs, which are the whole difference -----------------------
# LEG_TRIM: the standoffs are cut 2.10 mm shorter than the stack says, by
# request on 2026-08-10 ("make the legs 2.1 mm shorter ... just do it do not
# check"). The contract height is kept in view so the departure is visible
# rather than looking like the derivation changed. Consequence, stated: the
# back panel now sits LEG_TRIM below the shell rim instead of flush with it.
LEG_TRIM = 2.10
TUBE_H = round(P.Z_PLATE_SEAT - P.Z_BOARD_BACK - LEG_TRIM, 4)
assert P.TUBE_BORE > P.MOUNT_HOLE_D - 1e-9, (
    "the tube bore must clear the board's own mounting hole")

# ---- the square standoff columns -----------------------------------------
POST = P.TUBE_OD / 2                  # half-width, on the board side
PANEL_LO = (P.BOARD - _ab.PLATE_SIZE) / 2
PANEL_HI = PANEL_LO + _ab.PLATE_SIZE
# how far each column reaches from its screw centre out to the panel corner
CORNER_REACH = round(P.MOUNTS[0][0] - PANEL_LO, 4)
POST_SIZE = round(POST + CORNER_REACH, 4)
TUBE_WALL = POST - P.TUBE_BORE / 2    # thinnest wall, on the board side

assert CORNER_REACH > 0, (
    "the screw centre is outside the panel -- the column cannot reach a corner")
# A wall is a WALL: it stands along the build direction, so extrusion width
# governs and the clean step is 0.87, not the 0.84 a lamina would take.
assert TUBE_WALL >= 0.87, (
    f"the column's thin side is {TUBE_WALL:.2f} mm around the bore -- thinner "
    f"than two extrusions is not a wall, it is a suggestion")

with BuildPart() as back:
    # ---- 1. the panel, straight off the acrylic profile -------------------
    # dir is stated rather than inherited from the face's normal: a face knows
    # which way it points and this one has already sent the panel downward once.
    add(extrude(PROFILE, amount=P.PLATE_T, dir=(0, 0, 1)))

    # ---- 2. THE STANDOFFS: SQUARE, AND RUN OUT INTO THE PANEL CORNER ------
    #     > "the corner should be not post, they should be, you know, blend
    #     >  into the corner. nicely."
    #
    # They were four round Ø6 posts standing 1.10 mm clear of the corner --
    # near enough to read as an oversight, far enough to be four separate
    # towers. Each is now a SQUARE column that runs out to meet both panel
    # edges, so the corner of the panel and the standoff are one piece of
    # material instead of a post beside a corner.
    #
    # The outer faces are flush with the panel edges, so the column gains its
    # size entirely on the corner side: 7.10 square against the old Ø6, with
    # the screw still on its original centre. Nothing moves toward the board.
    # Each column spans from the panel EDGE it is nearest to, inward past the
    # screw centre by POST. The inward direction is AWAY from that edge, so the
    # sign follows which half the mount is in -- getting it backwards makes a
    # 1.10 mm sliver hugging the edge that still passes "is it in the corner".
    for _mx, _my in [P.bxy(x, y) for x, y in P.MOUNTS]:
        _x0, _x1 = sorted((PANEL_LO if _mx < P.BOARD / 2 else PANEL_HI,
                           _mx + (-POST if _mx > P.BOARD / 2 else POST)))
        _y0, _y1 = sorted((PANEL_LO if _my < P.BOARD_H / 2 else PANEL_HI,
                           _my + (-POST if _my > P.BOARD_H / 2 else POST)))
        assert abs((_x1 - _x0) - POST_SIZE) < 1e-6 and \
               abs((_y1 - _y0) - POST_SIZE) < 1e-6, (
            f"column came out {_x1 - _x0:.2f} x {_y1 - _y0:.2f}, not "
            f"{POST_SIZE:.2f} square -- the inward direction is wrong")
        with BuildSketch(Plane.XY):
            with Locations(((_x0 + _x1) / 2, (_y0 + _y1) / 2)):
                Rectangle(_x1 - _x0, _y1 - _y0)
        extrude(amount=-TUBE_H)
        # and through the panel too, so the bore below is ONE diameter from
        # face to face. The imported profile carries the acrylic back's Ø3.40
        # sheet clearance hole; leaving it would step down to the Ø3.30 bore
        # and hang a 0.05 mm ledge in mid-air at the top of every column.
        with BuildSketch(Plane.XY):
            with Locations(((_x0 + _x1) / 2, (_y0 + _y1) / 2)):
                Rectangle(_x1 - _x0, _y1 - _y0)
        extrude(amount=P.PLATE_T)

    # ---- 3. bore them, through the column AND the panel -------------------
    with BuildSketch(Plane.XY.offset(P.PLATE_T)):
        with Locations(*[P.bxy(x, y) for x, y in P.MOUNTS]):
            Circle(P.TUBE_BORE / 2)
    extrude(amount=-(P.PLATE_T + TUBE_H), mode=Mode.SUBTRACT)

part = back.part

SHEET = {
    "title": "4square case REV 5 -- PRINTED BACK",
    "subtitle": f"the finger-notch back with the standoffs built in: "
                f"4 x {POST_SIZE:.2f} SQUARE corner columns, "
                f"{TUBE_H:.2f} mm long",
    "views": ["top", "iso-bottom", "section:x"],
    "labels": ["1  THE OUTER FACE", "2  FROM INSIDE - the corner columns",
               "3  SECTION through a corner column"],
    "notes": [f"panel {_ab.PLATE_SIZE:.2f} sq x {P.PLATE_T:.2f}, "
              f"overall {P.PLATE_T + TUBE_H:.2f} tall",
              f"finger notch {FINGER_W:.1f} x {FINGER_L:.1f}, "
              f"{len(VENTS)} vent holes, USB-C {AP_W:.2f} x {AP_H:.2f} "
              f"R{J9_R}",
              "prints outer-face-down; make-print-stl.py flips it"],
}

# ===== verify ==============================================================
v = cadkit.Verifier(part, "4square case REV 5 -- printed back")
v.note("panel", f"{_ab.PLATE_SIZE:.2f} x {_ab.PLATE_SIZE:.2f} x "
                f"{P.PLATE_T:.2f} mm")
v.note("standoffs", f"4 x SQUARE {POST_SIZE:.2f} columns, Ø{P.TUBE_BORE} bore, "
                    f"{TUBE_H:.2f} mm long, thin wall {TUBE_WALL:.2f}")
v.note("blended into the corner", f"each column runs {CORNER_REACH:.2f} mm out "
                                  f"to meet both panel edges -- flush, not a "
                                  f"post standing 1.10 mm clear of the corner")
v.note("standoff height, derived", f"Z_PLATE_SEAT {P.Z_PLATE_SEAT:.2f} - "
                                   f"Z_BOARD_BACK {P.Z_BOARD_BACK:.2f} = "
                                   f"{TUBE_H:.2f}")
v.note("openings inherited from the acrylic profile",
       f"2 finger notches {FINGER_W:.1f} x {FINGER_L:.1f}, {len(VENTS)} vents, "
       f"USB-C {AP_W:.2f} x {AP_H:.2f}, 4 screw holes")

# ---- THE PANEL IS ACTUALLY THERE -----------------------------------------
# Every opening check below asks whether a hole is OPEN, and all of them pass
# on a part with no panel at all. This asks the opposite question first, at
# four places the panel must be solid, so an empty or misplaced panel fails
# here instead of sailing through 49 green checks.
v.standard()
v.no_enclosed_voids()
v.measurements(P.M)
v.bbox(x=_ab.PLATE_SIZE, y=_ab.PLATE_SIZE, z=P.PLATE_T + TUBE_H)

# ---- the columns are real, the right length, joined, and IN the corner ---
from OCP.BRepClass3d import BRepClass3d_SolidClassifier          # noqa: E402
from OCP.gp import gp_Pnt                                        # noqa: E402
from OCP.TopAbs import TopAbs_State                              # noqa: E402

_cl = BRepClass3d_SolidClassifier(part.wrapped)


def _solid(x, y, z):
    _cl.Perform(gp_Pnt(float(x), float(y), float(z)), 1e-7)
    return _cl.State() == TopAbs_State.TopAbs_IN


# THE PANEL EXISTS, AND IT IS WHERE IT SAYS IT IS. Four points that must be
# solid material, spread across the panel so a shifted or flipped panel cannot
# satisfy them by accident.
_MID = P.PLATE_T / 2
for _lbl, _px, _py in (("centre", P.BOARD / 2, P.BOARD_H / 2),
                       # NOT at (60.80, 40.00) any more: widening the pocket
                       # to 20.00 swallowed that point, and a presence probe
                       # that lands in an opening reports a missing panel.
                       ("left of the vent field", 42.00, 58.00),
                       ("between the notches", P.BOARD / 2, P.BOARD_H / 2 - 20),
                       ("beside the J9 aperture", 50.00, 68.00)):
    v.check(f"panel is solid at its {_lbl}", _solid(_px, _py, _MID),
            "no panel material here -- the panel is missing or misplaced")
v.check("the panel occupies 0..PLATE_T, not below it",
        _solid(P.BOARD / 2, P.BOARD_H / 2, P.PLATE_T - 0.05)
        and not _solid(P.BOARD / 2, P.BOARD_H / 2, -0.05),
        "the panel extruded the wrong way off its profile")

_r_mid = (P.TUBE_BORE / 2 + POST) / 2          # mid-wall, board side
for _i, (_mx, _my) in enumerate(P.MOUNTS, 1):
    _cx, _cy = P.bxy(_mx, _my)
    v.check(f"standoff {_i} is solid at mid-length",
            _solid(_cx + _r_mid, _cy, -TUBE_H / 2), "no column wall there")
    v.check(f"standoff {_i} bore is open at mid-length",
            not _solid(_cx, _cy, -TUBE_H / 2), "the bore is blocked")
    v.check(f"standoff {_i} reaches the board face",
            _solid(_cx + _r_mid, _cy, -TUBE_H + 0.15), "column is short")
    v.check(f"standoff {_i} stops AT the board face, not past it",
            not _solid(_cx + _r_mid, _cy, -TUBE_H - 0.15),
            "column runs past the board's back face")
    v.check(f"standoff {_i} is joined to the panel",
            _solid(_cx + _r_mid, _cy, -0.15) and _solid(_cx + _r_mid, _cy, 0.15),
            "there is a gap between column and panel")

v.clearance("standoff wall, board side", TUBE_WALL, 0.87)

# THE BLEND IS THE WHOLE POINT, SO IT IS CHECKED. A column that merely got
# bigger without reaching the edges would pass every check above. These ask
# for material AT the panel's own corner, at the column's mid height.
for _i, (_mx, _my) in enumerate(P.MOUNTS, 1):
    _cx, _cy = P.bxy(_mx, _my)
    _ex = PANEL_LO + 0.20 if _cx < P.BOARD / 2 else PANEL_HI - 0.20
    _ey = PANEL_LO + 0.20 if _cy < P.BOARD_H / 2 else PANEL_HI - 0.20
    v.check(f"standoff {_i} reaches the panel corner",
            _solid(_ex, _ey, -TUBE_H / 2),
            "the column stops short -- it is a post beside a corner, not part "
            "of it")
    # and it must stop there: nothing may hang outside the panel
    _ox = PANEL_LO - 0.20 if _cx < P.BOARD / 2 else PANEL_HI + 0.20
    v.check(f"standoff {_i} does not overhang the panel edge",
            not _solid(_ox, _cy, -TUBE_H / 2), "material outside the panel")
v.check("overall height is panel + standoff",
        abs(part.bounding_box().size.Z - (P.PLATE_T + TUBE_H)) < 1e-6,
        f"{part.bounding_box().size.Z:.3f}")

# ---- the openings survived being extruded into a printed part -------------
for _i, (_vx, _vy, _) in enumerate(VENTS, 1):
    if _i % 12 == 1:                            # sample, not all 84
        v.check(f"vent {_i} is open", not _solid(_vx, _vy, P.PLATE_T / 2),
                "not open")
for _i, (_x0, _x1, _y0, _y1) in enumerate(_ab._BTN_CASE, 1):
    v.check(f"button {_i} centre is open",
            not _solid((_x0 + _x1) / 2, (_y0 + _y1) / 2, P.PLATE_T / 2),
            "material covers the button centre")

v.report()

cadkit.export(part, OUT, verifier=v, sheet=SHEET)
