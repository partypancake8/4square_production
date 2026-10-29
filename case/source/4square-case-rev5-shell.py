r"""
4square case -- REV 3 SHELL. Printed front shell, front face down on the bed.

    ~/.venvs/cad/bin/python 4square-case-rev3-shell.py

THE FRONT FACE IS THE ALIGNMENT JIG. That is what this revision of the shell is
for, and it changes what the part IS. The assembly order is now: print the
shell, drop the four screen modules into their pockets, lay the board on top of
them, and only THEN solder. So the pocket -- not the board, not the solder
joint -- decides where a screen ends up permanently:

    > "they have to sit exactly in their pockets, and then when you put the
    >  board on top of them it's all perfectly aligned ... so that when they're
    >  soldered in, the screens sit square in their views."

Which makes the pocket wall a FUNCTIONAL DATUM, not a clearance. A 27.60 module
in a 27.90 pocket can skew at most atan(0.30 / 27.60) = 0.62 degrees. The
pocket is therefore measured back OUT of the built solid below, at three depths
on both axes, rather than trusted because the sketch said so.

WHAT WENT, AND WHY

    * THE FOUR CORNER BLOCKS ARE GONE. The face is 6.20 mm of solid material
      now (1.00 bezel + 5.20 pocket), so each screw threads straight into it.
      That also fixes the engagement that was marginal at 3.95 mm on the old
      4.50 mm boss: it is P.BOSS_ENGAGE = 4.60 inside the face.
    * THE BOARD NEEDED A SEAT. It sits at Z_BOARD_FRONT while the face's inner
      surface is at FACE_T, so it hovers BOARD_GAP = 0.686 mm and the screws
      would pull it down onto nothing. A perimeter rim that proud, plus a pad
      at each mount, gives it something to land on. The rim grows UP off the
      inner face, so it is supported for its whole height -- it is NOT rev 2's
      cantilevered ledge, and it is not rev 2's seat ring either, because it
      lives BELOW the board's plane and never narrows the path the board takes.

ORIENTATION. Z = 0 is the front outer face: cosmetic, and on the print bed.
+Z runs into the case. X, Y are CASE coordinates; P.bxy maps the board's.

    0.00   front outer face (on the bed)
    1.00   BEZEL_T -- pocket floor; the module's front face lands here
    6.20   FACE_T  -- front inner face; pocket walls end, rim starts
    6.89   Z_BOARD_FRONT -- board lands on the rim and the four pads
    8.49   Z_BOARD_BACK
   18.17   Z_PLATE_SEAT
   20.36   Z_TOP -- wall top

TRAPS ALREADY PAID FOR, KEPT PAID:
  * P.bxy flips BOTH axes. The board is turned over to go in, and a real board
    can only be rotated, never mirrored. The USB-C slot is the one feature on
    this part that is asymmetric in X, so it uses P.C3_CASE_X -- the derived
    case coordinate -- and never the raw board figure, which was 36 mm wrong.
  * Plane(face) picks its own x_dir and on a sidewall it comes out (0,0,1), so
    a cutout at x=+-14 lands at z=+-14 and every topological check still
    passes. Every plane here is pinned: Plane(origin=..., z_dir=..., x_dir=...).
  * Shell first, cut features second.
  * Faces are selected by POSITION, never by area.
  * is_valid / is_manifold are PROPERTIES in build123d 0.11.1.
  * Positional checks sample MID-MATERIAL. OCC classifies a point on a face as
    ON, not IN, so a sample taken on a face proves nothing.
"""
import json
import math
import os

from build123d import *
import cadkit
import case_params as P

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "4square-case-rev5-shell")

# ---- knobs that belong to this part alone (fits and depths, not measurements)
SLOT_CLR = 0.35       # receptacle to slot, per side -- DFM table, sliding fit
# PLUG_CLR and CB_DEPTH moved INTO the contract: the case height now derives
# from the USB-C aperture, so if they lived here the shell could change the
# aperture without the height that has to clear it ever knowing.
PLUG_CLR = P.PLUG_CLR
CB_DEPTH = P.CB_DEPTH

# REV 5: THE SEATING RIM AND THE MOUNT PADS ARE GONE.
# Rev 3 sat the board on a 3.20 mm rim standing 0.686 mm proud of the front
# inner face. Rev 4 puts the RETAINER in that space -- a full 74 x 74 plate
# that presses the four screens flat against the bezel and is itself the
# board's seat. Two seats at one height cannot both be exact; if the rim ended
# up even 0.05 mm proud of the retainer the retainer would never be clamped and
# the screens would be loose again, which is the whole defect being fixed:
#
#   > "we're going to need to add back that middle part that holds the screens
#    flat because it will not keep the screens flat. with the PCB holding them."
#
# So the rim is deleted outright and the retainer is the only seat.
RELIEF_XY = 0.30      # relief pocket around a front-side protrusion, per side

# Header-pin relief in the pocket floor. 0.40 deep leaves 0.60 of bezel -- three
# layers, the practical minimum for a lamina (DFM rule 3). It is cut ONLY in the
# floor, never into a wall -- the walls are the datum that squares the screen,
# and relieving the floor up to them leaves their locating face intact.
HDR_RELIEF_D = 0.40
# The strip runs to the pocket walls on three sides -- no lip for a pin to
# perch on. It stops this far short of the WINDOW only, so the bezel you can
# actually see stays a full 1.00 mm thick at its edge.
HDR_WIN_KEEP = 0.40

RELIEF_Z = 0.40       # and over its top

# ===== what the BOARD does to this part, read from the board ===============
# Rule 9: every physical input comes from an observation. These are read out of
# the same board reference the ledger's own entries were derived from, not
# copied from anybody's message -- including the brief that asked for them.
_ref = json.load(open(P.REF))
_OX, _OYT, _ZT = _ref["origin"]["x"], _ref["origin"]["y_top"], _ref["board"]["z_top"]

FRONT_SOLIDS = []                      # (x0, x1, y0, y1, height above board)
for _b in _ref["boxes"]:
    _x0, _x1 = sorted(c - _OX for c in _b["x"])
    _y0, _y1 = sorted(_OYT - c for c in _b["y"])
    if _b["z"][1] > _ZT + 1e-6:
        FRONT_SOLIDS.append((_x0, _x1, _y0, _y1, _b["z"][1] - _ZT))

# THE BARE RING. How wide a band around the board's edge carries nothing on its
# front face -- i.e. how wide the seating rim may be before it lands on a
# component instead of on bare laminate. Computed, not quoted.
BARE_RING = min(min(f[0], P.BOARD - f[1], f[2], P.BOARD_H - f[3])
                for f in FRONT_SOLIDS)


def _in_square(f, cx, cy, w, h, tol=1e-6):
    return (f[0] >= cx - w / 2 - tol and f[1] <= cx + w / 2 + tol
            and f[2] >= cy - h / 2 - tol and f[3] <= cy + h / 2 + tol)


def _is_module(f):
    """A screen module: inside one of the four module squares AND tall. The
    height test matters -- the header pins UNDER a module are inside the same
    square and are not the module."""
    return f[4] > 3.0 and any(_in_square(f, sx, sy, P.SCREEN_W, P.SCREEN_H)
                              for sx, sy in P.SCREENS)


def _in_pocket(f):
    return any(_in_square(f, sx, sy, P.POCKET_W, P.POCKET_H)
               for sx, sy in P.SCREENS)


# An OBSTRUCTION is a front-side protrusion that is not a screen module, stands
# taller than the gap between the board and the face, and is not already inside
# a pocket (where there is 5.20 mm of void waiting for it). Everything else on
# the front of this board clears the face on its own.
OBSTRUCTIONS = [f for f in FRONT_SOLIDS
                if not _is_module(f) and f[4] > P.BOARD_GAP and not _in_pocket(f)]
WORST_RELIEF = max((f[4] for f in OBSTRUCTIONS), default=0.0)

# ===== geometry ============================================================
CX = (P.X0 + P.X1) / 2            # outer block centre, both axes
PILOT_Z0 = P.FACE_T - P.BOSS_ENGAGE      # the pilot's blind floor

Y_OUT = P.X0                      # outer face of the CAD-ymin wall
Y_IN = -P.FIT                     # its inner face
# The C3's receptacle rides on the module, which rides on the board's BACK.
# FROM THE MEASURED PORT HEIGHT, not from the two assumed numbers it replaces.
# C3_STANDOFF (2.50) and C3_MODULE_T (1.10) were both ASSUMED and waived, and
# together they put the port 2.10 mm too far forward -- the user measured 19.70
# from the front face to the top of the USB-C shell where this model said 17.60.
# The C3 is evidently not direct-soldered. The measurement is stored relative to
# the BOARD's back face, so it stays true if the case stack ever changes again.
SLOT_Z = P.Z_BOARD_BACK + P.C3_PORT_TOP_ABOVE_BOARD_BACK - P.C3_CONN_H / 2
SLOT_W = P.C3_CONN_W + 2 * SLOT_CLR
SLOT_Z0 = SLOT_Z - (P.C3_CONN_H / 2 + SLOT_CLR)
SLOT_Z1 = SLOT_Z + (P.C3_CONN_H / 2 + SLOT_CLR)
CB_W = P.PLUG_W + 2 * PLUG_CLR
CB_Z0 = SLOT_Z - (P.PLUG_H / 2 + PLUG_CLR)
CB_Z1 = SLOT_Z + (P.PLUG_H / 2 + PLUG_CLR)
CB_Y = Y_OUT + CB_DEPTH           # the counterbore floor


def board_rect(x0, x1, y0, y1):
    """A board-coordinate rectangle, in case coordinates. Both corners go
    through P.bxy, because bxy flips BOTH axes and doing that by hand is how the
    36 mm USB-C error happened."""
    ax, ay = P.bxy(x0, y0)
    bx, by = P.bxy(x1, y1)
    return min(ax, bx), max(ax, bx), min(ay, by), max(ay, by)


def yz_prism(pts, x0, width):
    """A prism: polygon in the (Y, Z) plane, extruded along +X. The plane is
    pinned explicitly -- Plane.YZ would work today and would still be the habit
    that lets a sidewall cutout land on the wrong axis tomorrow."""
    with BuildPart() as prism:
        with BuildSketch(Plane(origin=(x0, 0, 0), z_dir=(1, 0, 0),
                               x_dir=(0, 1, 0))):
            Polygon(*pts, align=None)
        extrude(amount=width)
    return prism.part


FIL = cadkit.Fillets()

with BuildPart() as shell:
    # ---- 1. the outer block ----------------------------------------------
    with BuildSketch(Plane.XY):
        with Locations((CX, CX)):
            Rectangle(P.OUTER, P.OUTER)
    extrude(amount=P.Z_TOP)

    # ---- 2. hollow it, FULL WIDTH, all the way to the top ----------------
    # Nothing narrows this opening at any height above the front inner face, so
    # the board's straight path in from the back is clear by construction.
    with BuildSketch(Plane.XY.offset(P.FACE_T)):
        with Locations((P.BOARD / 2, P.BOARD_H / 2)):
            Rectangle(P.CAVITY, P.CAVITY)
    extrude(amount=P.Z_TOP - P.FACE_T, mode=Mode.SUBTRACT)

    # ---- 3. THE SCREEN POCKETS -- the datum -------------------------------
    # Cut from the inner face (FACE_T) forward to the bezel (BEZEL_T), so the
    # wall is exactly POCKET_DEPTH tall and its floor is exactly the bezel. The
    # module drops in from behind and is squared by the four walls; the board
    # lands on the rim, not the module on the pocket floor (POCKET_DEPTH <
    # FRONT_STACK is asserted in the contract).
    # The module pocket now starts BELOW the glass pocket. Cutting it from the
    # bezel, as every earlier rev did, would remove the glass pocket's locating
    # walls in the same operation and leave nothing holding the glass square.
    with BuildSketch(Plane.XY.offset(P.MODULE_POCKET_Z0)):
        with Locations(*[P.bxy(x, y) for x, y in P.SCREENS]):
            Rectangle(P.POCKET_W, P.POCKET_H)
    extrude(amount=P.MODULE_POCKET_D, mode=Mode.SUBTRACT)

    # ---- 3b. THE GLASS POCKET -- the new datum ----------------------------
    # The glass is what the eye sees and what must sit flat, so the glass is
    # what gets located. Centred on the MODULE (the glass is very nearly
    # centred on it); the window centres on the LIT AREA instead. Collapsing
    # those two datums into one is what let the window run past the glass.
    with BuildSketch(Plane.XY.offset(P.BEZEL_T)):
        with Locations(*[P.bxy(x, y) for x, y in P.GLASS_POCKETS]):
            Rectangle(P.GLASS_POCKET_W, P.GLASS_POCKET_H)
    extrude(amount=P.GLASS_POCKET_D, mode=Mode.SUBTRACT)

    # ---- 3c. THE RIBBON CHANNEL -------------------------------------------
    # The flex leaves the bottom edge of the glass. Pinched between the glass
    # and the pocket floor it holds the glass off its seat -- the same failure
    # mode as the header pins already relieved below, one layer up. The channel
    # runs from the glass pocket out to the module pocket wall on the header
    # side, at full glass-pocket depth, and its direction is DERIVED from which
    # way the lit area is offset rather than hardcoded.
    # TWO CENTRES, AND THEY ARE NOT THE SAME ONE. The glass pocket sits
    # GLASS_CENTRE_OFFSET off the screen centre, so the module pocket's wall
    # must be measured from the SCREEN centre. Measuring it from the glass
    # pocket centre -- which this loop did -- ran the channel
    # GLASS_CENTRE_OFFSET past the wall and undercut it. At the old 0.10 that
    # was 0.10 of overshoot and slipped under the ceiling threshold; at the
    # measured 0.60 it became 36.5 mm2 of roofed wall, and unsupported_ceilings
    # caught it. A latent bug that only a bigger number made visible.
    for (mx, my), (sx, sy), (wx, wy) in zip(P.SCREENS, P.GLASS_POCKETS,
                                            P.WINDOWS):
        s = 1.0 if wy > sy else -1.0             # the header/ribbon edge
        y_in = sy + s * (P.GLASS_POCKET_H / 2 - 0.01)   # inside the glass pocket
        y_out = my + s * P.POCKET_H / 2          # out to the module pocket wall
        cx, cy = P.bxy(sx, (y_in + y_out) / 2)
        with BuildSketch(Plane.XY.offset(P.BEZEL_T)):
            with Locations((cx, cy)):
                Rectangle(P.RIBBON_CH_W, abs(y_out - y_in))
        extrude(amount=P.GLASS_POCKET_D, mode=Mode.SUBTRACT)

    # ---- 3d. THE RIBBON RELIEF -- depth, not just width -------------------
    # The channel above only opens the ring BEHIND the glass plane. The bezel
    # in FRONT of that plane is still solid, so a flex that folds back under
    # the glass is pinched between the glass and the bezel and holds the glass
    # off the seat -- the same failure as the header pins, one layer forward.
    #
    #   > "add like a relief cutout for the ribbon cable just so it can sit as
    #    flat as possible ... pretty generous in the area, but just so the
    #    ribbon cable isn't pressing on anything."
    #
    # IT STARTS AT THE GLASS POCKET WALL, NOT INSIDE IT. The header edge is
    # where the window's guaranteed 0.84 mm of glass-on-bezel seat lives, and
    # that seat is the whole reason cells 5-8 of the depth jig existed.
    # Relieving into it would give the ribbon room by taking away the very
    # ledge the glass sits on. Outboard of the wall the region is already open
    # above, so nothing here is roofed.
    for (mx, my), (sx, sy), (wx, wy) in zip(P.SCREENS, P.GLASS_POCKETS,
                                            P.WINDOWS):
        s = 1.0 if wy > sy else -1.0
        # glass pocket edge off the GLASS centre, module wall off the SCREEN
        # centre -- see the note on the channel above.
        y_from = sy + s * P.GLASS_POCKET_H / 2   # AT the wall, not inside it
        y_to = my + s * P.POCKET_H / 2           # out to the module pocket wall
        cx, cy = P.bxy(sx, (y_from + y_to) / 2)
        with BuildSketch(Plane.XY.offset(P.BEZEL_T - P.RIBBON_RELIEF_D)):
            with Locations((cx, cy)):
                Rectangle(P.RIBBON_CH_W, abs(y_to - y_from))
        extrude(amount=P.RIBBON_RELIEF_D, mode=Mode.SUBTRACT)

    # ---- 4. windows, through the remaining bezel --------------------------
    # Sized to the LIT AREA plus one margin -- "it should just be pixels" --
    # not to the old front-plate aperture, which was 0.25 mm wider than the
    # glass itself on every long side and is why you could see into the case.
    with BuildSketch(Plane.XY):
        with Locations(*[P.bxy(x, y) for x, y in P.WINDOWS]):
            Rectangle(P.WINDOW_W, P.WINDOW_H)
    extrude(amount=P.BEZEL_T, mode=Mode.SUBTRACT)

    # ---- 4b. HEADER-PIN RELIEF in each pocket floor -----------------------
    # THE DEFECT THIS EXISTS FOR, and no check we own could ever have seen it.
    # Each OLED module's own header pins are soldered from its BACK, so their
    # cut ends stand proud on its GLASS side. Those ends touch our pocket floor
    # before the module's PCB does, so the module sits proud -- and the board
    # laid on top of it sits proud too. That is the tilt the user has been
    # fighting by clipping pins:
    #
    #   > "the headers are touching that inside pocket floor before the rest of
    #    the PCB is ... that is where the area needs a little bit of relief."
    #
    # It is invisible to every gate in this project because the module's own
    # pin ends are not in the board STEP at all -- the STEP has the module as a
    # block. Only someone holding one finds it.
    #
    # The strip goes on the HEADER side, derived from which way the glass is
    # offset rather than hardcoded, so it follows if P.WINDOWS ever moves.
    # It touches ONLY the floor: the four pocket walls are the datum that
    # squares the screen and they stay full height and full length.
    for (sx, sy), (wx, wy) in zip(P.SCREENS, P.WINDOWS):
        s = 1.0 if wy > sy else -1.0            # which way the header edge lies
        y_win = wy + s * P.GLASS_H / 2          # far edge of the window
        y_wall = sy + s * P.POCKET_H / 2        # the pocket wall on that side
        # RUN IT ALL THE WAY TO THE WALLS. The first version inset the strip
        # 0.80 from every side to "protect" the walls, which left a 0.40 mm lip
        # of full-thickness bezel against the header-side wall -- and a lip is
        # precisely what a pin end perches on, which is the tilt we are trying
        # to remove. The user caught it off a phone render:
        #
        #   > "it looks like the relief doesn't connect to the rest, like
        #    there's still a little tiny wall."
        #
        # Relieving up to the wall costs the wall nothing: the wall is the
        # material OUTBOARD of the pocket, and its locating face simply runs
        # HDR_RELIEF_D deeper on that side. Only the window edge keeps a band
        # of full-thickness bezel, so the visible bezel stays 1.00 mm.
        y_from = y_win + s * HDR_WIN_KEEP
        cy = (y_from + y_wall) / 2
        band = abs(y_wall - y_from)
        # REFERENCED TO THE MODULE POCKET FLOOR, not the bezel. The module now
        # seats at MODULE_POCKET_Z0 because the glass pocket occupies the
        # 1.79 mm in front of it, so the pin ends stand proud of THAT plane.
        # Cutting from the bezel as before also left the recess roofed by the
        # new solid ring between the two pockets -- 295 mm2 of ceiling, which
        # unsupported_ceilings caught. Cut from the module floor and the module
        # pocket itself is the opening above it, so nothing is roofed.
        with BuildSketch(Plane.XY.offset(P.MODULE_POCKET_Z0 - HDR_RELIEF_D)):
            with Locations(P.bxy(sx, cy)):
                Rectangle(P.POCKET_W, band)
        extrude(amount=HDR_RELIEF_D, mode=Mode.SUBTRACT)

    # ---- 6. relief for the front-side protrusions -------------------------
    # Cut DOWN into the inner face, so every relief pocket has an upward-facing
    # floor and vertical walls: no ceiling, nothing to support. Each is only as
    # deep as its own obstruction needs.
    for _f in OBSTRUCTIONS:
        _rx0, _rx1, _ry0, _ry1 = board_rect(_f[0] - RELIEF_XY, _f[1] + RELIEF_XY,
                                            _f[2] - RELIEF_XY, _f[3] + RELIEF_XY)
        _depth = (_f[4] - P.BOARD_GAP) + RELIEF_Z
        with BuildSketch(Plane.XY.offset(P.FACE_T)):
            with Locations((((_rx0 + _rx1) / 2), (_ry0 + _ry1) / 2)):
                Rectangle(_rx1 - _rx0, _ry1 - _ry0)
        extrude(amount=-_depth, mode=Mode.SUBTRACT)

    # ---- 7. blind pilot holes, entered from the front INNER FACE ----------
    # Rev 3 bored these from the seating pad's top face because there was a pad
    # of material over them. There is no pad now: the retainer spans that gap,
    # carries its own Ø3.4 clearance holes, and the screw meets solid material
    # for the first time at the inner face. Thread engagement is unchanged --
    # it was always inside the face, never in the pad.
    with BuildSketch(Plane.XY.offset(P.FACE_T)):
        with Locations(*[P.bxy(x, y) for x, y in P.MOUNTS]):
            Circle(P.BOSS_PILOT_D / 2)
    extrude(amount=-(P.FACE_T - PILOT_Z0), mode=Mode.SUBTRACT)

    # ---- 8. the C3's USB-C, through the CAD-ymin wall ---------------------
    # P.C3_CASE_X, never the raw board X. Both cuts get a 45 degree roof that
    # falls as it runs inward: a plain rectangular slot leaves a horizontal
    # ceiling the width of the port with nothing under it (DFM rule 2), and it
    # is the only place in this part where a ceiling could exist at all. The
    # counterbore's roof is measured from the counterbore FLOOR, so the full
    # aperture survives to the depth a real overmould has to reach -- sloping
    # from the outer face instead would pinch the plug off after 0.6 mm.
    add(yz_prism([(Y_OUT - 1.0, SLOT_Z0),
                  (Y_IN + 1.0, SLOT_Z0),
                  (Y_IN + 1.0, SLOT_Z1),
                  (Y_IN, SLOT_Z1),
                  (Y_OUT - 1.0, SLOT_Z1 + (Y_IN - (Y_OUT - 1.0)))],
                 P.C3_CASE_X - SLOT_W / 2, SLOT_W),
        mode=Mode.SUBTRACT)
    add(yz_prism([(Y_OUT - 1.0, CB_Z0),
                  (CB_Y, CB_Z0),
                  (CB_Y, CB_Z1),
                  (Y_OUT - 1.0, CB_Z1 + (CB_Y - (Y_OUT - 1.0)))],
                 P.C3_CASE_X - CB_W / 2, CB_W),
        mode=Mode.SUBTRACT)

    # ---- 9. the two declared fillets, and nothing else --------------------
    # Selected by POSITION. A length-ranked pick grabs interior cavity corners,
    # and if it does not fail outright it rounds the wrong edges quietly.
    def _outer_vertical(edges, tol=0.05):
        return [e for e in edges
                if abs(abs(e.center().X - CX) - P.OUTER / 2) < tol
                and abs(abs(e.center().Y - CX) - P.OUTER / 2) < tol]

    def _front_outline(edges, tol=0.05):
        out = []
        for e in edges:
            c = e.center()
            if abs(c.Z) > tol:
                continue
            # the whole outer loop, arcs at the filleted corners included --
            # filleting only the four straights leaves the corner arcs sharp
            # and the front edge discontinuous
            if max(abs(c.X - CX), abs(c.Y - CX)) > P.OUTER / 2 - P.FILLET_CORNER - tol:
                out.append(e)
        return out

    FIL.add("outer vertical corners",
            _outer_vertical(shell.edges().filter_by(Axis.Z)), P.FILLET_CORNER)
    FIL.add("front face perimeter",
            _front_outline(shell.edges()), P.FILLET_FACE)

part = shell.part

SHEET = {
    "title": (f"4square case REV 5, printed shell   "
              f"{P.OUTER:.1f} x {P.OUTER:.1f} x {P.Z_TOP:.1f} mm   "
              f"the front face is the alignment jig"),
    # iso-top looks down +Z, which is INTO the case -- the open back. The
    # cosmetic face is at Z=0, so it is iso-BOTTOM. Getting these the wrong way
    # round labelled the open back "FRONT" on the rev 2 sheet.
    "views": ["iso-bottom", "iso-top", "section:y"],
    "labels": ["1  FRONT  (cosmetic face, prints on the bed)",
               "2  BACK  (four locating pockets, screw pilots, no rim: the retainer seats the board)",
               "3  SECTION  (through a pocket: 1.00 bezel, 5.20 of pocket wall)"],
    "subs": [f"four {P.GLASS_W:.2f} x {P.GLASS_H:.2f} windows, {P.BEZEL_T:.2f} mm "
             f"of bezel over the glass",
             f"{P.POCKET_W:.2f} square x {P.POCKET_DEPTH:.2f} deep, a "
             f"{P.SCREEN_W:.2f} module can skew "
             f"{math.degrees(math.atan(2 * P.POCKET_FIT / P.SCREEN_W)):.2f}°",
             f"board seats on the RETAINER, {P.BOARD_GAP:.3f} mm above this face"],
}

# ===== verify ==============================================================
v = cadkit.Verifier(part, "4square case REV 5 -- printed shell")
v.note("board (measured)", f"{P.BOARD:.2f} x {P.BOARD_H:.2f} x {P.PCB_T:.2f} mm")
v.note("case outer", f"{P.OUTER:.2f} x {P.OUTER:.2f} x {P.Z_TOP:.2f} mm")
v.note("Z stack", f"bezel {P.BEZEL_T:.2f} / face {P.FACE_T:.2f} / board front "
                  f"{P.Z_BOARD_FRONT:.3f} / plate seat {P.Z_PLATE_SEAT:.2f}")
v.note("screen pocket (nominal)",
       f"{P.POCKET_W:.2f} x {P.POCKET_H:.2f} x {P.POCKET_DEPTH:.2f} deep, "
       f"{P.POCKET_FIT:.2f}/side on a {P.SCREEN_W:.2f} module")
v.note("worst screen skew the pocket allows",
       f"{math.degrees(math.atan(2 * P.POCKET_FIT / P.SCREEN_W)):.3f} deg")
v.note("thread engagement in the face",
       f"{P.BOSS_ENGAGE:.2f} mm ({P.BOSS_ENGAGE / 3.0:.1f}x M3 diameter), "
       f"bore {P.FACE_T - PILOT_Z0:.3f} deep from the inner face")
v.note("board seat", f"the RETAINER -- this face carries nothing proud; the "
                     f"board sits {P.BOARD_GAP:.3f} mm above it")
v.note("bare ring on the board's front", f"{BARE_RING:.3f} mm (computed from "
                                         f"{len(FRONT_SOLIDS)} front solids)")
v.note("front-side obstructions to relieve",
       f"{len(OBSTRUCTIONS)}, tallest {WORST_RELIEF:.3f} mm above the board")
v.note("C3 port centre", f"case x {P.C3_CASE_X:.2f} (board x {P.C3_X:.2f}), "
                         f"case z {SLOT_Z:.2f}")

# ---- the standard battery -------------------------------------------------
# allow_tori: the two DECLARED fillets meet at the four front corners, and a
# 1 mm round swept along a 3 mm corner arc IS a torus. Not a loosened threshold
# -- v.fillets(FIL) below asserts the part carries exactly the declared radii.
v.standard(allow_tori=4)
v.no_enclosed_voids()
v.measurements(P.M)
v.bbox(x=P.OUTER, y=P.OUTER, z=P.Z_TOP)
v.connected_throughout(0.30, P.Z_TOP - 0.30, steps=13)

# ---- the board can actually GET there -------------------------------------
_travel = P.Z_TOP - P.Z_BOARD_FRONT + P.PCB_T + 6.0
_board = cadkit.Pos(P.BOARD / 2, P.BOARD_H / 2,
                    P.Z_BOARD_FRONT + P.PCB_T / 2 + _travel) * cadkit.Box(
                        P.BOARD, P.BOARD_H, P.PCB_T)
v.insertion_clear(_board, (0, 0, -1), _travel,
                  label="board fitted from the back", step=0.25)

# ---- nothing prints in mid-air --------------------------------------------
v.unsupported_ceilings(max_area=10.0)

# ===== THE POCKET, MEASURED OUT OF THE BUILT SOLID =========================
# The datum this revision exists to deliver. Not "the sketch said 27.90": walk
# out from the middle of each pocket until the classifier changes state, and
# read the wall's position off the geometry. Every relief cut, every fillet and
# every later boolean has already happened by this point, so this measures what
# will be printed rather than what was drawn.
from OCP.BRepClass3d import BRepClass3d_SolidClassifier          # noqa: E402
from OCP.gp import gp_Pnt                                        # noqa: E402
from OCP.TopAbs import TopAbs_State                              # noqa: E402

_cl = BRepClass3d_SolidClassifier(part.wrapped)


def _solid(x, y, z):
    _cl.Perform(gp_Pnt(float(x), float(y), float(z)), 1e-7)
    return _cl.State() == TopAbs_State.TopAbs_IN


def _wall(p, d, reach, iters=34):
    """Distance from p along unit direction d to the first material. p must be
    in open space and p + reach*d in material, else None."""
    if _solid(*p) or not _solid(*[p[i] + d[i] * reach for i in range(3)]):
        return None
    a, b = 0.0, reach
    for _ in range(iters):
        m = (a + b) / 2
        if _solid(*[p[i] + d[i] * m for i in range(3)]):
            b = m
        else:
            a = m
    return (a + b) / 2


# TWO pockets now, stacked, so they are probed separately against their own
# nominal size. Probing the glass pocket against the module pocket's 27.90
# is what the old single list did, and it is why every z=1.50 check failed.
_GLASS_DEPTHS = [P.BEZEL_T + 0.50, P.MODULE_POCKET_Z0 - 0.50]
_DEPTHS = [P.MODULE_POCKET_Z0 + 0.50, P.FACE_T - 0.50]
_worst_w = 0.0
_worst_sq = 0.0
for _i, (_sx, _sy) in enumerate(P.SCREENS, 1):
    _cx, _cy = P.bxy(_sx, _sy)
    for _z in _GLASS_DEPTHS:
        _rw, _rh = P.GLASS_POCKET_W / 2 + 1.0, P.GLASS_POCKET_H / 2 + 1.0
        _gcx, _gcy = P.bxy(*P.GLASS_POCKETS[_i - 1])
        _gxm = _wall((_gcx, _gcy, _z), (-1, 0, 0), _rw)
        _gxp = _wall((_gcx, _gcy, _z), (1, 0, 0), _rw)
        _gym = _wall((_gcx, _gcy, _z), (0, -1, 0), _rh)
        _gyp = _wall((_gcx, _gcy, _z), (0, 1, 0), _rh)
        # The ribbon channel deliberately removes the header-side wall, so that
        # probe is EXPECTED to find nothing. Requiring all four is what made
        # this check fail on a pocket that is perfectly present.
        if None in (_gxm, _gxp):
            v.check(f"glass pocket {_i} measurable at z={_z:.2f}", False,
                    "an X probe never reached material")
            continue
        v.check(f"glass pocket {_i} X at z={_z:.2f} is "
                f"{P.GLASS_POCKET_W:.2f}",
                abs(_gxm + _gxp - P.GLASS_POCKET_W) <= 0.02,
                f"measured {_gxm + _gxp:.4f} mm")
        # the ribbon channel deliberately breaks the header-side wall,
        # so Y is measured on the intact side only
        # ORIENTATION-AGNOSTIC ON PURPOSE. Exactly one Y wall is opened by the
        # ribbon channel; which one depends on P.bxy, which flips BOTH axes,
        # and hand-deriving that sign is how this check failed on a pocket that
        # was correct. So: require exactly one intact Y wall, and require it to
        # sit at the nominal half-height.
        _ys = [g for g in (_gym, _gyp) if g is not None]
        _good = [g for g in _ys if abs(g - P.GLASS_POCKET_H / 2) <= 0.02]
        v.check(f"glass pocket {_i} Y wall at z={_z:.2f}",
                len(_good) >= 1,
                f"probes {[None if g is None else round(g, 4) for g in (_gym, _gyp)]} "
                f"vs nominal {P.GLASS_POCKET_H / 2:.4f}")
        v.check(f"glass pocket {_i} ribbon channel opens ONE Y wall at "
                f"z={_z:.2f}", len(_good) == 1,
                f"{len(_good)} of 2 Y walls at nominal -- the channel must "
                f"break exactly one")
    for _z in _DEPTHS:
        # reach just past the nominal wall, NOT a whole pocket width: the
        # pockets are 33.84 apart, so a probe sent POCKET_W from the centre
        # lands inside the NEXT pocket, finds open space at the far end and
        # reports "no wall" on a pocket that is perfectly present.
        _rw, _rh = P.POCKET_W / 2 + 1.0, P.POCKET_H / 2 + 1.0
        _xm = _wall((_cx, _cy, _z), (-1, 0, 0), _rw)
        _xp = _wall((_cx, _cy, _z), (1, 0, 0), _rw)
        _ym = _wall((_cx, _cy, _z), (0, -1, 0), _rh)
        _yp = _wall((_cx, _cy, _z), (0, 1, 0), _rh)
        if None in (_xm, _xp, _ym, _yp):
            v.check(f"pocket {_i} measurable at z={_z:.2f}", False,
                    "a probe never reached material -- the pocket is not there")
            continue
        _w, _h = _xm + _xp, _ym + _yp
        v.check(f"pocket {_i} X at z={_z:.2f} is {P.POCKET_W:.2f}",
                abs(_w - P.POCKET_W) <= 0.02, f"measured {_w:.4f} mm")
        v.check(f"pocket {_i} Y at z={_z:.2f} is {P.POCKET_H:.2f}",
                abs(_h - P.POCKET_H) <= 0.02, f"measured {_h:.4f} mm")
        # square, not merely the right size: each wall must sit at half-width
        # from the nominal centre, so the pocket is neither offset nor skewed,
        # and the two axes must agree with each other
        _off = max(abs(_xm - P.POCKET_W / 2), abs(_xp - P.POCKET_W / 2),
                   abs(_ym - P.POCKET_H / 2), abs(_yp - P.POCKET_H / 2))
        v.check(f"pocket {_i} walls square at z={_z:.2f}", _off <= 0.02,
                f"worst wall offset {_off:.4f} mm")
        _worst_w = max(_worst_w, abs(_w - P.POCKET_W), abs(_h - P.POCKET_H))
        _worst_sq = max(_worst_sq, _off)
    # depth: probe down where the bezel survives, i.e. beside the window
    # The MODULE pocket floor is now MODULE_POCKET_Z0, not the bezel: the glass
    # pocket occupies the 1.79 mm in front of it. Probe outboard of the glass
    # pocket, or the probe falls straight through into the glass pocket and
    # reports the bezel as the module floor.
    _px = _cx + P.GLASS_POCKET_W / 2 + 1.0
    _top = P.FACE_T - 0.5
    _drop = _wall((_px, _cy, _top), (0, 0, -1), P.MODULE_POCKET_D + 0.3)
    _floor = None if _drop is None else _top - _drop
    v.check(f"module pocket {_i} floor at z={P.MODULE_POCKET_Z0:.2f}",
            _floor is not None and abs(_floor - P.MODULE_POCKET_Z0) <= 0.02,
            f"floor at z={_floor:.4f}" if _floor is not None else "no floor found")
    v.check(f"module pocket {_i} depth is {P.MODULE_POCKET_D:.2f}",
            _floor is not None
            and abs((P.FACE_T - _floor) - P.MODULE_POCKET_D) <= 0.02,
            f"measured {(P.FACE_T - _floor):.4f} mm" if _floor is not None
            else "no floor found")
v.note("pocket size, worst error over 24 measurements", f"{_worst_w:.4f} mm")
v.note("pocket squareness, worst wall offset", f"{_worst_sq:.4f} mm")

# ---- windows, pockets, rim, pads, pilots, port: sampled MID-MATERIAL ------
for _i, ((_sx, _sy), (_wx, _wy)) in enumerate(zip(P.SCREENS, P.WINDOWS), 1):
    _px, _py = P.bxy(_sx, _sy)
    _gx, _gy = P.bxy(_wx, _wy)
    v.open_at(_gx, _gy, P.BEZEL_T / 2, f"screen {_i} window open")
    v.open_at(_px, _py, P.BEZEL_T + P.POCKET_DEPTH / 2, f"screen {_i} pocket open")
    _bx = _gx - (P.GLASS_W / 2 + 1.0) if _gx > _px else _gx + (P.GLASS_W / 2 + 1.0)
    v.solid_at(_bx, _gy, P.BEZEL_T / 2, f"screen {_i} bezel")
    v.solid_at(_px + P.POCKET_W / 2 + 0.6, _py, P.FACE_T - 0.5,
               f"screen {_i} face beside the pocket")

# REV 5: the band between the inner face and the board is now EMPTY -- it
# belongs to the retainer. Probe it as void rather than as rim and pads.
_gap_z = P.FACE_T + P.BOARD_GAP / 2
for _i, (_mx, _my) in enumerate(P.MOUNTS, 1):
    _cx, _cy = P.bxy(_mx, _my)
    v.open_at(_cx, _cy, P.FACE_T - P.BOSS_ENGAGE / 2, f"mount {_i} pilot open")
    v.open_at(_cx, _cy, _gap_z, f"mount {_i} clear above the face for the retainer")
    v.solid_at(_cx, _cy, PILOT_Z0 - 0.30, f"mount {_i} material in front of the pilot")
    v.solid_at(_cx, _cy, P.BEZEL_T / 2, f"mount {_i} bezel unbroken")
v.bore_blind(P.BOSS_PILOT_D / 2, "Z", 0.0, "pilot holes stay out of the face")

# THE RETAINER'S BAND MUST BE COMPLETELY EMPTY, edge to edge. This is the
# check that rev 3 could not have: anything left standing proud of the inner
# face -- a rim, a pad, a stray boss -- would hold the retainer off the screens
# and the screens would stay loose. Probe the perimeter, the old rim line, and
# the centre, all at the same height.
for _qx, _qy, _what in ((P.BOARD / 2, -P.FIT / 2, "at the cavity wall"),
                        (P.BOARD / 2, 1.0, "on the old rim line"),
                        (P.BOARD / 2, 4.0, "just inboard of it"),
                        (P.BOARD / 2, P.BOARD_H / 2, "at the board centre"),
                        (4.0, 4.0, "over a mounting corner")):
    v.open_at(_qx, _qy, _gap_z, f"retainer band clear {_what}")

v.open_at(P.C3_CASE_X, (Y_OUT + Y_IN) / 2, SLOT_Z, "C3 through-slot")
v.open_to_outside(P.C3_CASE_X, (Y_OUT + Y_IN) / 2, SLOT_Z, "C3 slot reaches outside")
v.open_at(P.C3_CASE_X, Y_OUT + CB_DEPTH / 2, SLOT_Z1 + 0.5, "C3 counterbore")
v.solid_at(P.C3_CASE_X, (Y_OUT + Y_IN) / 2, CB_Z1 + CB_DEPTH + 0.60,
           "wall above the C3 port")
v.solid_at(P.C3_CASE_X, (Y_OUT + Y_IN) / 2, CB_Z0 - 1.0, "wall below the C3 port")
v.solid_at(P.C3_CASE_X + CB_W / 2 + 1.0, (Y_OUT + Y_IN) / 2, SLOT_Z,
           "wall beside the C3 port")

# ---- clearances -----------------------------------------------------------
v.clearance("C3 slot around the receptacle", SLOT_CLR, 0.30)
v.clearance("counterbore vs USB-IF max overmould", PLUG_CLR, 0.20)
v.clearance("wall left above the C3 aperture", P.Z_TOP - (CB_Z1 + CB_DEPTH), 0.84)

# pocket to the nearest mount pilot. The pocket corner is the near point, and
# this is the wall the self-tapping screw actually splits if it is too thin.
_pk = min(math.hypot(abs(mx - sx) - P.POCKET_W / 2, abs(my - sy) - P.POCKET_H / 2)
          for (mx, my) in P.MOUNTS for (sx, sy) in P.SCREENS)
v.note("mounting hole to pocket corner", f"{_pk:.3f} mm")
v.clearance("pocket wall beside the screw pilot", _pk - P.BOSS_PILOT_D / 2,
            (P.BOSS_D - P.BOSS_PILOT_D) / 2)


def _gap(a, b):
    return math.hypot(max(b[0] - a[1], a[0] - b[1], 0.0),
                      max(b[2] - a[3], a[2] - b[3], 0.0))


# REV 5: there is no rim and there are no pads, so the sweeps that used to
# measure front-side solids against them are gone with them. The equivalent
# question -- does anything on the board's front face reach the case? -- is now
# answered by BOARD_GAP alone, and answered with margin to spare:
v.note("front-side solids vs the front inner face",
       f"tallest tail {max(_f[4] for _f in FRONT_SOLIDS if _f[4] < 3.0):.3f} mm "
       f"vs a {P.BOARD_GAP:.3f} mm gap -- the retainer fills it, not the case")

# ---- relief: every obstruction actually cleared ---------------------------
# Not "a relief cut exists" -- for each one, sample the case at the height the
# protrusion's TOP will occupy once the board is seated, and require open space.
for _i, _f in enumerate(OBSTRUCTIONS, 1):
    _rx0, _rx1, _ry0, _ry1 = board_rect(_f[0], _f[1], _f[2], _f[3])
    _tip = P.Z_BOARD_FRONT - _f[4]                  # case Z of the protrusion tip
    for _qx, _qy in (((_rx0 + _rx1) / 2, (_ry0 + _ry1) / 2),
                     (_rx0 + 0.15, _ry0 + 0.15), (_rx1 - 0.15, _ry1 - 0.15)):
        v.open_at(_qx, _qy, _tip + 0.10,
                  f"relief {_i} clear at ({_qx:.1f},{_qy:.1f})")
# WHERE THE DATUM IS NOT FULL HEIGHT, stated rather than left to be found.
# The four button tails sit 3.71 mm in from the board edge, which is right
# beside a screen pocket, so their relief pockets cut a NOTCH in the top of the
# pocket wall. The probes above run through the pocket's centre and would never
# see it. Measured here instead: how much of each wall's length is notched, and
# how much wall height survives there. The module is still located over the full
# 5.20 mm everywhere else, and over this much at the notch.
_notch_len, _notch_left = 0.0, P.POCKET_DEPTH
for _f in OBSTRUCTIONS:
    _e = (_f[0] - RELIEF_XY, _f[1] + RELIEF_XY, _f[2] - RELIEF_XY, _f[3] + RELIEF_XY)
    _d = (_f[4] - P.BOARD_GAP) + RELIEF_Z
    for _sx, _sy in P.SCREENS:
        _px0, _px1 = _sx - P.POCKET_W / 2, _sx + P.POCKET_W / 2
        _py0, _py1 = _sy - P.POCKET_H / 2, _sy + P.POCKET_H / 2
        _ox = min(_e[1], _px1) - max(_e[0], _px0)
        _oy = min(_e[3], _py1) - max(_e[2], _py0)
        if _ox <= 0 or _oy <= 0:
            continue                      # not near this pocket at all
        for _crosses, _run in (((_e[0] < _px0 < _e[1]) or (_e[0] < _px1 < _e[1]), _oy),
                               ((_e[2] < _py0 < _e[3]) or (_e[2] < _py1 < _e[3]), _ox)):
            if _crosses:
                _notch_len = max(_notch_len, _run)
                _notch_left = min(_notch_left, P.POCKET_DEPTH - _d)
v.note("pocket wall notched by a relief cut",
       f"{_notch_len:.2f} mm of a {P.POCKET_W:.2f} mm wall, "
       f"{_notch_left:.2f} of {P.POCKET_DEPTH:.2f} mm of wall height left there")
v.clearance("pocket wall height left at the deepest notch", _notch_left, 3.00)
v.clearance("pocket wall length left unnotched", P.POCKET_W - 2 * _notch_len, 15.0)

# REV 5: THERE ARE NO RELIEF CUTS, and that is a result, not an omission.
# An OBSTRUCTION is a front-side solid taller than BOARD_GAP. BOARD_GAP was
# 0.686 in rev 3, so 206 tails qualified and the inner face was covered in
# pockets. It is now RET_BODY_T = 3.000, which is taller than every tail on the
# board (worst 2.585), so the set is empty and the inner face is plain.
v.check("no relief cuts are needed in the front face", len(OBSTRUCTIONS) == 0,
        f"{len(OBSTRUCTIONS)} obstructions taller than the "
        f"{P.BOARD_GAP:.3f} mm gap")
v.clearance("gap over the tallest front-side tail",
            P.BOARD_GAP - max(_f[4] for _f in FRONT_SOLIDS if _f[4] < 3.0), 0.30)

# ---- HEADER-PIN RELIEF, verified from the built solid ---------------------
# The strip must be OPEN where the module's pin ends land, the bezel under it
# must survive at a whole number of layers, and -- the part that matters -- the
# four pocket WALLS must be untouched, because they are the datum.
_relief_floor = P.MODULE_POCKET_Z0 - HDR_RELIEF_D
assert abs(_relief_floor / P.LAYER - round(_relief_floor / P.LAYER)) < 1e-9, (
    "the material left under the header relief is not a whole number of layers")
v.note("header-pin relief",
       f"{HDR_RELIEF_D:.2f} deep off the module floor at "
       f"{P.MODULE_POCKET_Z0:.2f}, {_relief_floor:.2f} of solid left "
       f"({_relief_floor / P.LAYER:.0f} layers)")

for _i, ((_sx, _sy), (_wx, _wy)) in enumerate(zip(P.SCREENS, P.WINDOWS), 1):
    _s = 1.0 if _wy > _sy else -1.0
    _ywin = _wy + _s * P.WINDOW_H / 2
    _ywall = _sy + _s * P.POCKET_H / 2
    _cy = (_ywin + _ywall) / 2
    _qx, _qy = P.bxy(_sx, _cy)
    # open inside the strip, solid in the material that remains beneath it.
    # Sample OUTBOARD of the glass pocket, or the glass pocket's own void is
    # what answers "is it open" and the check proves nothing about the relief.
    # Offset in X clear of the RIBBON CHANNEL, which is 13.00 wide on this very
    # edge -- sampling on the screen centreline lands inside the channel and
    # reports the relief as open / the material as missing when neither is true.
    _ox, _oy = P.bxy(_sx + P.RIBBON_CH_W / 2 + 1.0,
                     _sy + _s * (P.GLASS_POCKET_H / 2 + 1.0))
    v.open_at(_ox, _oy, P.MODULE_POCKET_Z0 - HDR_RELIEF_D / 2,
              f"screen {_i} header relief is OPEN")
    v.solid_at(_ox, _oy, (P.MODULE_POCKET_Z0 - HDR_RELIEF_D) / 2,
               f"screen {_i} material survives under the relief")
    # and the wall it runs alongside is still full height: sample the wall
    # material just outboard of the strip, at the top of the pocket
    _wallx, _wally = P.bxy(_sx, _sy + _s * (P.POCKET_H / 2 + 0.5))
    v.solid_at(_wallx, _wally, P.FACE_T - 0.30,
               f"screen {_i} pocket wall intact beside the relief")

# ---- WINDOW vs GLASS, measured from the geometry this file actually built --
# The old check here measured the window against the POCKET, which is the wrong
# pair: it passed happily while the window ran 0.25 mm past the glass on every
# long side. What you see through a window is what is directly behind it.
# Worst case has the glass shoved against one wall of its own pocket.
_ov = min(min(P.GLASS_PANEL_W / 2 - abs(wx - gx) - P.WINDOW_W / 2,
              P.GLASS_PANEL_H / 2 - abs(wy - gy) - P.WINDOW_H / 2) - P.GLASS_FIT
          for (gx, gy), (wx, wy) in zip(P.GLASS_POCKETS, P.WINDOWS))
v.note("window sits on glass by (worst case)",
       f"{_ov:.3f} mm (contract says {P.GLASS_OVERLAP_MIN:.3f})")
v.clearance("glass covers every window edge", _ov, 2 * P.EXT_OUTER)
v.check("window-on-glass overlap agrees with the contract",
        abs(_ov - P.GLASS_OVERLAP_MIN) < 1e-6,
        f"{_ov:.4f} vs {P.GLASS_OVERLAP_MIN:.4f}")

# and the window must still show every pixel with the module at either extreme
_pix = P.WINDOW_MARGIN - P.GLASS_FIT
v.clearance("window clears the lit area even with the module hard over",
            _pix, 0.10)

v.fillets(FIL)
v.report()
cadkit.export(part, OUT, verifier=v, sheet=SHEET)
