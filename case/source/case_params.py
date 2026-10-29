r"""
4square case REV 3 -- the shared parameter contract.

BOTH part scripts import this and NOTHING re-derives a shared number locally.
Rev 2's shell and backplate each computed their own Z stack, which is how a
0.4 mm clearance could be right in one file and wrong in the other.

    import case_params as P

WHAT REV 3 IS, AND WHY (chosen by the user from the architecture sheet,
option 2, "pins through the board")

    * The board is LOADED FROM THE BACK and NOTHING above its plane overlaps
      its 74 x 74 footprint. That is the rule rev 2 broke -- a seat ring left
      the board 2.30 mm too wide to pass the opening it had to pass through,
      and hung 539 mm2 of itself in mid-air at the same time.
    * It rests on FOUR CORNER BOSSES, not a perimeter ledge:
          > "the board will sit on probably like little corner supports"
      Each boss is Ø6, merged into the two walls it touches, and grows UP off
      the front face -- so it carries the board AND prints without support.
    * The screws come from the BACK, pass through the plate, down a standoff
      tube that belongs to the PLATE, through the board's own Ø3.2 mounting
      hole, and thread into the boss BELOW the board. Everything above the
      board's plane is therefore part of a piece that lifts off.
    * THE SCREENS SIT FLUSH IN THE FRONT FACE:
          > "the screens line up, only the screens are flush with the front of
          >  the case when they are put in"
      The face carries a pocket the module drops into from behind, leaving a
      BEZEL of 1.00 mm over the glass -- five layers, and the figure measured
      off the user's own original front plate. The module front therefore lands
      1.00 mm below the outer surface instead of 2.39 mm down a hole, and that
      is what sets the whole Z stack.

ORIENTATION. Z = 0 is the front outer face: the cosmetic face, and the face
that goes on the print bed. +Z runs into the case, toward the back.

    0.00            front outer face  (on the bed)
    BEZEL_T         front of the screen modules -- flush, in the pocket
    FACE_T          front inner face
    Z_BOARD_FRONT   board rests here, on four corner bosses
    Z_BOARD_BACK    board back face; plate standoff tubes land here
    Z_PLATE_SEAT    backplate inner face
    Z_TOP           wall top, flush with the backplate
"""
import json
import os

import cadkit

HERE = os.path.dirname(os.path.abspath(__file__))
REF = os.path.join(HERE, "justclock-pcb-ref.json")
LEDGER = os.path.join(HERE, "measurements.json")

M = cadkit.Measurements(LEDGER)
_r = json.load(open(REF))

# ---- from the physical world, through the ledger -------------------------
BOARD = M["BOARD_W"]
BOARD_H = M["BOARD_H"]
PCB_T = M["PCB_T"]
FRONT_STACK = M["FRONT_STACK"]
BACK_STACK = M["BACK_STACK"]
MOUNT_HOLE_D = M["MOUNT_HOLE_D"]
FRONT_PROTRUSION_INSET = M["FRONT_PROTRUSION_INSET"]

SCREEN_W = M["SCREEN_MODULE_W"]
SCREEN_H = M["SCREEN_MODULE_H"]
GLASS_W = M["GLASS_W"]
GLASS_H = M["GLASS_H"]
GLASS_OFFSET = M["GLASS_OFFSET"]

# ===== REV 5: THE GLASS IS THE DATUM ======================================
# The screens did not sit flat and you could see past the glass into the
# housing. Root cause: the module PCB was the datum and the window was cut to
# GLASS_W x GLASS_H = 25.20 x 13.00 -- but those numbers were read off the
# WINDOW in the user's old front plate, not off the glass. An aperture is
# always cut larger than the thing behind it, so we sized our window to an
# oversized opening and then looked through it:
#
#     glass long axis   24.70   window as built 25.20   -> 0.25 PAST the glass
#                                                          on each side
#     window short axis 13.00 offset 2.40                -> 0.60 past the glass
#                                                          on the header side
#
# Both confirmed against the board STEP's own solids, which agree with the
# user's calipers to 0.000 on the long axis:
#
#     module PCB   25.400 x 26.200   z 2.585..4.095
#     glass        24.700 x 17.000   z 5.385..5.885   <- outermost
#     lit area     20.000 x 10.000   z 4.085..4.585
#
# TWO DATUMS WERE COLLAPSED INTO ONE. GLASS_OFFSET=2.40 describes where the LIT
# AREA sits (centre y 17.98 vs module 20.08), NOT where the glass sits -- the
# glass is very nearly centred on the module (20.18). So:
#     the GLASS POCKET centres on the MODULE   (GLASS_CENTRE_OFFSET, 0.10)
#     the WINDOW       centres on the LIT AREA (ACTIVE_OFFSET, 2.10)
GLASS_PANEL_W = M["GLASS_PANEL_W"]
GLASS_PANEL_H = M["GLASS_PANEL_H"]
GLASS_PROUD = M["GLASS_PROUD"]
ACTIVE_W = M["ACTIVE_W"]
ACTIVE_H = M["ACTIVE_H"]
ACTIVE_OFFSET = M["ACTIVE_OFFSET"]
GLASS_CENTRE_OFFSET = M["GLASS_CENTRE_OFFSET"]
RIBBON_W = M["RIBBON_W"]
C3_PORT_TOP_ABOVE_BOARD_BACK = M["C3_PORT_TOP_ABOVE_BOARD_BACK"]

C3_STANDOFF = M["C3_STANDOFF"]
C3_OVERHANG = M["C3_OVERHANG"]
C3_CONN_W = M["C3_CONN_W"]
C3_CONN_H = M["C3_CONN_H"]
C3_MODULE_T = M["C3_MODULE_T"]
PLUG_W = M["USB_C_PLUG_OVERMOULD_W"]
PLUG_H = M["USB_C_PLUG_OVERMOULD_H"]

WALL = M["WALL"]
EXT_OUTER = M["EXTRUSION_OUTER"]
EXT_INNER = M["EXTRUSION_INNER"]

# ---- the knobs -----------------------------------------------------------
PLATE_T = WALL
# The bezel is a LAMINA, not a wall: its thickness runs along the build
# direction, so it is governed by LAYER HEIGHT, not extrusion width. The first
# version of this line read `2 * EXT_OUTER + 0.03` = 0.87, which is 4.35 layers
# and would have been sliced to 0.80 regardless -- the model would have been
# describing something the printer cannot make. Four layers, stated honestly.
LAYER = 0.20
# 1.00 mm, five layers -- MEASURED off the user's own front plate, whose
# window runs z -4.00 to -3.00 before stepping out to the 28.20 module pocket.
# Was 0.80 (my own choice of "four layers, the practical minimum"); their number
# is the one that has been printed and looked at.
BEZEL_T = 5 * LAYER                 # 1.00 over the glass: five layers

# THE FRONT FACE IS NO LONGER JUST A WALL -- it is the alignment jig.
# The four screens drop into deep pockets in it, the board goes on top of them,
# and only then are they soldered. So the pocket sets where a screen ends up
# permanently, and it has to be exact:
#     > "they have to sit exactly in their pockets, and then when you put the
#     >  board on top of them it's all perfectly aligned ... so that when
#     >  they're soldered in, the screens sit square in their views."
# 5.20 deep, not 5.00: the user's own plate used 5.00 and reported it "always a
# little too shallow".
POCKET_DEPTH = 5.20
FACE_T = BEZEL_T + POCKET_DEPTH     # 6.20
FIT = 0.35                          # board to wall, per side
PLATE_FIT = 0.25                    # plate to cavity, per side
# 0.15 per side, NOT the old 0.30. The pocket is now what squares the screen,
# so it is a LOCATING fit. At 27.60 module in a 27.90 pocket the worst skew a
# screen can take is atan(0.30/27.60) = 0.62 degrees. 0.30/side would have
# allowed 1.25 degrees, which is visible across a 25 mm window.
POCKET_FIT = 0.15                   # screen module into its pocket, per side
Z_BACK_CLR = 0.60                   # over the tallest back part

BOSS_D = 6.00                       # corner boss: 2 x M3, the DFM minimum
BOSS_PILOT_D = 2.50                 # M3 self-tapper into PLA: 0.8 x D
SPIGOT_CLR = 0.30                   # screw shank to the board's own hole
TUBE_OD = 6.00                      # plate standoff tube
# 3.40 left a 1.30 mm tube wall against a 1.29 minimum -- true, but with no
# margin at all. 3.30 still clears an M3 shank (3.00 nominal, 0.15 per side)
# and puts the wall at 1.35.
TUBE_BORE = 3.30

FILLET_CORNER = 3.00
FILLET_FACE = 1.00

# ===== REV 4: THE RETAINER ================================================
# Rev 3 had no part between the screens and the board, and the screens would
# not stay flat:
#     > "we're going to need to add back that middle part that holds the
#     >  screens flat because it will not keep the screens flat. with the PCB
#     >  holding them. It just doesn't work. I can't get it aligned."
#
# The geometry below is MEASURED off the user's own Fusion back plate,
# ~/Sync/4square/'og front BACK plate  4square.step', cross-sectioned at its
# own horizontal face levels. Their instruction was to keep it:
#     > "It should have the pads like mine. You can basically use mine, most of
#     >  my geometry ... Don't change the depth of the whatever the pads are."
#
#     pad          27.600 square, standing 1.500 proud       <- kept exactly
#     notch        12.500 wide x 3.950 into the pad, and
#                  continuing through the body to the joint  <- kept exactly
#     body         1.700                                     <- CHANGED, see below
#     split        two halves meeting at the centreline      <- kept
RET_PAD = 27.600
RET_NOTCH_W = 12.500
RET_NOTCH_D = 3.950

# ===== SETTLED 2026-08-09: THE MODULE IS 3.72, AND THE PAD NOW FOLLOWS IT ===
# This was the last open number in the front stack, and the derivation used to
# run the wrong way round. RET_PAD_H was pinned at their plate's 1.500 and the
# MODULE thickness was inferred from it -- so a wrong pad could never be
# detected, because the module was defined to fit whatever pad was written
# here. The measurement inverts it: the module is the physical fact, the pad is
# what has to reach it.
#
#     > "yeah it's 3.72"     calipers, glass front face to bare PCB back
#
#     their plate implied     3.700   (pocket 5.20 - pad 1.50)  -- 0.02 out
#     the board STEP said     3.301                             -- REFUTED
#
# So their printed plate was right all along and the STEP's module block was
# wrong by 0.42, which is consistent with everything else that solid has been
# caught on. The 0.399 "the pad may leave the module loose" risk that has been
# carried since 2026-08-07 is closed: the real shortfall is 0.02.
MODULE_T = M["MODULE_T"]                        # 3.72, measured
# The pad face and the module's back face are the same plane, so this is not a
# choice -- it is the only value that touches the module without crushing it.
# Asserted against Z_MODULE_BACK further down, which is the check that would
# have caught the old arrangement had the numbers ever disagreed.
RET_PAD_H = round(POCKET_DEPTH - MODULE_T, 6)   # 1.48, was their 1.500
MODULE_T_FROM_BOARD_STEP = 3.301

# THE BODY IS 2.90, NOT THEIR 1.70, AND THE BOARD DECIDES IT -- NOT A PREFERENCE.
# Only H1-H4 and J1-J4 are placed on F.Cu; every other part on this board is on
# B.Cu, and its through-hole leads poke out the FRONT face. 192 of those tails
# stand TAIL_H proud, and the retainer occupies exactly that space.
#
#     tail tip z = FACE_T + RET_BODY_T - TAIL_H          pad top z = FACE_T
#     the tails miss the pads entirely  <=>  RET_BODY_T >= TAIL_H
#
# At their 1.70 the tails eat 0.885 mm into the 1.500 pads and destroy 57-86% of
# the contact face (retainer-pad-study.png). The user's priority decides it:
#     > "it's more important to me that the modules sit flat ... I'm not too
#     >  worried about that clearance as much as the screen sitting in the right
#     >  spot."
# So the body grows until the tails are swallowed by it and the pads are whole.
_ztop = _r["board"]["z_top"]
_oxr, _oytr = _r["origin"]["x"], _r["origin"]["y_top"]
_SCR = [(20.08, 20.08), (53.92, 20.08), (20.08, 53.92), (53.92, 53.92)]


def _in_module_sq(x0, x1, y0, y1, tol=1e-9):
    return any(x0 >= cx - RET_PAD / 2 - tol and x1 <= cx + RET_PAD / 2 + tol
               and y0 >= cy - RET_PAD / 2 - tol and y1 <= cy + RET_PAD / 2 + tol
               for cx, cy in _SCR)


# (x0, x1, y0, y1, height above the board's FRONT face), board coordinates.
# Same classifier the shell and the old orienter use: a solid is part of a
# MODULE only if it is inside a 27.60 square AND taller than 3.0 mm. Footprint
# alone would swallow the ~200 header pins that this part has to clear.
FRONT_TAILS = []
for _b in _r["boxes"]:
    _h = _b["z"][1] - _ztop
    if _h <= 1e-6:
        continue
    _x0, _x1 = sorted(c - _oxr for c in _b["x"])
    _y0, _y1 = sorted(_oytr - c for c in _b["y"])
    if _in_module_sq(_x0, _x1, _y0, _y1) and _h > 3.0:
        continue                                  # the screen module itself
    FRONT_TAILS.append((_x0, _x1, _y0, _y1, _h))

TAIL_H = max(t[4] for t in FRONT_TAILS)           # 2.585
RET_TAIL_CLR = 0.30                               # over a tail tip, in Z
RET_BODY_T = TAIL_H + RET_TAIL_CLR                # 2.885
RET_BODY_T = round(-(-RET_BODY_T // LAYER) * LAYER, 6)   # up to a whole layer: 3.00
RET_T = RET_BODY_T + RET_PAD_H                    # total plate thickness

assert RET_BODY_T >= TAIL_H, (
    f"body {RET_BODY_T} is thinner than the tallest pin tail {TAIL_H} -- the "
    f"tails would eat into the pads, which is the defect this part cures")

# ---- derived: the Z stack, computed once, for both parts -----------------
# The board no longer sits just behind the front face; the retainer's body is
# between them, and the board seats on the retainer rather than on a rim.
Z_BOARD_FRONT = FACE_T + RET_BODY_T
Z_BOARD_BACK = Z_BOARD_FRONT + PCB_T
# THE CASE HEIGHT IS NOW SET BY THE USB-C APERTURE, NOT BY J9.
# With the port measured 2.10 mm further back than the assumed stack put it, the
# plug counterbore and its 45 degree roof reach higher than the back-side parts
# do, and the aperture broke through the top of the wall by 0.62 mm. The height
# therefore derives from whichever of the two actually governs, instead of being
# fixed by J9 and then checked afterwards.
PLUG_CLR = 0.35                     # overmould to counterbore, per side
CB_DEPTH = 1.60                     # how far the counterbore lets a plug enter
WALL_ABOVE_PORT = 2 * EXT_OUTER     # material left over the aperture

_SLOT_Z = Z_BOARD_BACK + C3_PORT_TOP_ABOVE_BOARD_BACK - C3_CONN_H / 2
# the roof rises 45 degrees as it runs inward, so it peaks CB_DEPTH higher
C3_APERTURE_TOP = _SLOT_Z + PLUG_H / 2 + PLUG_CLR + CB_DEPTH

_seat_from_board = Z_BOARD_BACK + BACK_STACK + Z_BACK_CLR
_seat_from_port = C3_APERTURE_TOP + WALL_ABOVE_PORT - PLATE_T
Z_PLATE_SEAT = max(_seat_from_board, _seat_from_port)
CASE_HEIGHT_DRIVER = ("the C3's USB-C aperture" if _seat_from_port > _seat_from_board
                      else "the back-side parts (J9)")
Z_TOP = Z_PLATE_SEAT + PLATE_T

CAVITY = BOARD + 2 * FIT
OUTER = CAVITY + 2 * WALL
X0 = -(FIT + WALL)
X1 = BOARD + FIT + WALL

# The board now sits almost against the front face: the module is 5.886 tall,
# the pocket takes 5.20 of it, so only this much sticks out behind the face.
BOARD_GAP = Z_BOARD_FRONT - FACE_T              # 0.686
# There are no corner bosses any more. The face is 6.20 mm of solid material,
# so the screws thread straight into it -- which also fixes the engagement that
# was marginal at 3.95 mm on the old 4.5 mm boss.
BOSS_ENGAGE = FACE_T - BEZEL_T - 0.60           # 4.60 of thread, all inside the face
TUBE_H = Z_PLATE_SEAT - Z_BOARD_BACK            # plate tube reaches the board

# ---- board features ------------------------------------------------------
MOUNTS = [(4.0, 4.0), (70.0, 4.0), (4.0, 70.0), (70.0, 70.0)]
SCREENS = [(20.08, 20.08), (53.92, 20.08), (20.08, 53.92), (53.92, 53.92)]

# The glass is NOT centred on the module: it sits GLASS_OFFSET toward the
# module's HEADER edge, because the COG driver and FPC tail occupy the far edge.
#
# THE SHIFT IS IN Y ONLY. All four modules are rotated 90 or 270 (gen_pcb.py),
# so every header faces the board's horizontal centreline at y=37 -- the glass
# moves along Y and is centred in X. The first version of this list applied the
# offset on BOTH axes, which left the window 0.175 mm from the pocket wall on
# two sides of every screen: a bezel rim under one extrusion width, i.e. no
# material at all, and you would have seen the module's PCB edge through the
# gap. Caught by the subagent building the shell, which measured the rim
# instead of assuming the contract was right.
# THE WINDOW NOW TRACKS THE LIT AREA, not the old front-plate aperture.
#     > "The screen should basically be right up ... no gap between the screen
#     >  and the outside border of the window. And it should just be pixels."
# So the window is the lit rectangle plus one margin, and nothing more. The
# margin has to swallow the module's freedom in its pocket (GLASS_FIT per side)
# and still leave a dark band rather than bare glass, so at worst case the
# window edge stands WINDOW_MARGIN - GLASS_FIT = 0.25 mm outside the pixels.
# 0.30, not 0.40. The binding constraint is the HEADER side, where the lit area
# already sits 2.00 mm off the glass centre: at 0.40 the glass overlapped the
# window by only 0.750 and the contract check below refused the build. This is
# the tightest the window can be held while still guaranteeing a light seal,
# and it lands closer to "just pixels" anyway -- worst case the window edge
# stands 0.15 mm outside the lit rectangle.
# THE WINDOW IS NOW SIZED BY THE SEAT, NOT BY THE PIXELS.
# It used to be the lit rectangle plus a 0.30 margin. A bigger one was asked
# for, and the size that was actually printed and chosen is cell 8's:
#     > "make the view window 24.66 by 13.66 mm same location just smidge
#     >  bigger"                                      -> printed as cells 1-4
#     > "option eight we're going to go with"         -> cells 5-8 window
# 24.66 x 13.66 was built and rejected on the plate for a reason worth keeping:
# the glass is only 24.70 x 16.60, so at that size the glass overlapped the
# bezel by -0.13 mm on the long axis and -0.18 on the header edge. The window
# edge stood OUTSIDE the glass and the ledge it seats on fell to 0.17 mm, under
# half an extrusion -- the glass would have had nothing to sit on.
#
# So the window is derived from what the glass can actually cover: as large as
# possible while still guaranteeing SEAT_MIN of glass on bezel all the way
# round, with the module shoved hard against one wall of its pocket. Derived,
# not typed, so it tracks any future change to the glass or the offsets.
# GLASS_FIT is defined HERE, above the window, because the window is now
# derived from it -- the seat the window leaves is measured against a glass
# that may sit anywhere within its fit.
GLASS_FIT = 0.15                                # per side, a locating fit
SEAT_MIN = 2 * EXT_OUTER                        # 0.84, two outer extrusions
_win_off = abs(ACTIVE_OFFSET - GLASS_CENTRE_OFFSET)
WINDOW_W = round(GLASS_PANEL_W - 2 * (SEAT_MIN + GLASS_FIT), 4)        # 22.72
WINDOW_H = round(2 * (GLASS_PANEL_H / 2 - _win_off
                      - SEAT_MIN - GLASS_FIT), 4)                      # 11.62
# and it must still show every pixel with the module at either extreme
WINDOW_MARGIN = min((WINDOW_W - ACTIVE_W) / 2, (WINDOW_H - ACTIVE_H) / 2)

WINDOWS = [(20.08, 20.08 + ACTIVE_OFFSET),
           (53.92, 20.08 + ACTIVE_OFFSET),
           (20.08, 53.92 - ACTIVE_OFFSET),
           (53.92, 53.92 - ACTIVE_OFFSET)]

# THE GLASS POCKET -- the new locating feature, and the point of this revision.
# The glass, not the module PCB, is what the eye sees and what must end up
# square, so the glass is what gets located. It centres on the MODULE.
GLASS_POCKET_W = GLASS_PANEL_W + 2 * GLASS_FIT  # 25.00
GLASS_POCKET_H = GLASS_PANEL_H + 2 * GLASS_FIT  # 16.90
# THE POCKET DEPTH IS NOW CHOSEN BY TEST, AND ROUNDING UP WAS THE BUG.
#
# It used to be GLASS_PROUD rounded UP to a whole layer, on the reasoning that
# a deeper pocket "never holds the glass off its seat". That is exactly
# backwards, and it is what put the two seats at the same height:
#
#     glass front lands on the bezel        z = BEZEL_T
#     module PCB front lands on the module pocket floor
#                                           z = BEZEL_T + GLASS_POCKET_D
#     the module goes as far forward as allowed, so
#         z_glass = max(BEZEL_T, BEZEL_T + GLASS_POCKET_D - GLASS_PROUD)
#     and the glass touches the bezel  <=>  GLASS_POCKET_D <= GLASS_PROUD
#
# Rounding UP guarantees the wrong side of that inequality. With GLASS_PROUD
# itself derived from the board STEP's glass solid -- the same solid already
# refuted on GLASS_PANEL_H -- the module was bottoming on the floor first and
# the glass never reached the bezel at all:
#
#     > "still having the issue of the screen not sitting as close to the
#     >  window as possible ... something about the geometry is preventing it
#     >  from seating further in."
#
# So the module pocket floor is no longer a seat at all. Settled by printing
# 4square-glass-depth-fit-jig.py -- four depths, twice, with a real module:
#
#     > "option eight we're going to go with"          cell 8 = 1.00 deep
#
# 1.00 is an OBSERVATION, not a derivation, which is why it is written plainly
# rather than computed. It also brackets GLASS_PROUD from below: the glass
# reaches its seat at 1.00, so GLASS_PROUD >= 1.00.
GLASS_POCKET_D = 1.00
assert abs(GLASS_POCKET_D / LAYER - round(GLASS_POCKET_D / LAYER)) < 1e-9, (
    "the glass pocket depth is not a whole number of layers")
assert GLASS_POCKET_D <= GLASS_PROUD, (
    f"glass pocket {GLASS_POCKET_D} is deeper than the glass stands proud "
    f"({GLASS_PROUD}) -- the module PCB would bottom on the module pocket "
    f"floor and hold the glass off the bezel, which is the rev 5 defect")

GLASS_POCKETS = [(20.08, 20.08 + GLASS_CENTRE_OFFSET),
                 (53.92, 20.08 + GLASS_CENTRE_OFFSET),
                 (20.08, 53.92 - GLASS_CENTRE_OFFSET),
                 (53.92, 53.92 - GLASS_CENTRE_OFFSET)]

# THE RIBBON GETS ITS OWN CHANNEL. It leaves the bottom edge of the glass, and
# if it is pinched between the glass and the pocket floor it holds the glass off
# its seat -- which is the "will not sit flat" failure, one layer up from the
# header-pin version already fixed in the pocket floor.
# GENEROUS, BY REQUEST -- the flex must not touch anything, anywhere:
#     > "add like a relief cutout for the ribbon cable just so it can sit as
#     >  flat as possible ... it can be pretty generous in the area, but just
#     >  so the ribbon cable isn't pressing on anything."
# 1.50 per side instead of 0.40. It is capped rather than opened all the way
# across, because the glass pocket's +Y wall is a LOCATING wall and the channel
# breaks it: at 15.20 there is still 4.90 mm of intact wall on either side of
# the channel, which is what holds the glass square in Y.
RIBBON_FIT = 1.50                               # per side; a flex, not a datum
RIBBON_CH_W = RIBBON_W + 2 * RIBBON_FIT         # 15.20

# AND THE FLEX GETS DEPTH AS WELL AS WIDTH. The channel only opens the ring
# BEHIND the glass plane; the bezel in front of it is still solid, so a flex
# that folds back under the glass is pinched against it. This relief is cut
# forward INTO the bezel, outboard of the glass pocket so it never touches the
# 0.84 mm seat on the header edge, leaving 3 layers of bezel:
RIBBON_RELIEF_D = 0.40
RIBBON_RELIEF_LEFT = BEZEL_T - RIBBON_RELIEF_D  # 0.60, three layers
assert abs(RIBBON_RELIEF_LEFT / LAYER
           - round(RIBBON_RELIEF_LEFT / LAYER)) < 1e-9, (
    "the bezel left under the ribbon relief is not a whole number of layers")
# 1e-9 of slack, and it is not a fudge: 3 * 0.20 evaluates to
# 0.6000000000000001 in binary floating point, so an exact >= rejects a bezel
# that is exactly three layers thick.
assert RIBBON_RELIEF_LEFT >= 3 * LAYER - 1e-9, (
    f"only {RIBBON_RELIEF_LEFT:.2f} of bezel left under the ribbon relief -- "
    f"a lamina needs at least three layers")

# The module pocket now starts BELOW the glass pocket instead of at the bezel,
# or it would cut the glass pocket's locating walls clean away.
MODULE_POCKET_Z0 = BEZEL_T + GLASS_POCKET_D     # 2.00
MODULE_POCKET_D = FACE_T - MODULE_POCKET_Z0     # 4.20

# ===== THE GLASS HANGS THE WHOLE FRONT STACK, AND THE RETAINER MUST REACH ==
# With the glass seated on the bezel, every Z in front of the board follows
# from BEZEL_T. The retainer's pad is what pushes the module onto that seat, so
# its face and the module's back face are the SAME plane -- and nothing in this
# file said so until the pocket depth changed and the question became live.
Z_GLASS_FRONT = BEZEL_T                          # the seat, and the datum
Z_MODULE_PCB_FRONT = BEZEL_T + GLASS_PROUD       # floats clear of the floor
Z_MODULE_BACK = BEZEL_T + MODULE_T               # what the retainer presses
Z_RET_PAD_FACE = FACE_T - RET_PAD_H              # what presses it

assert abs(Z_MODULE_BACK - Z_RET_PAD_FACE) < 1e-9, (
    f"the retainer's pad face is at z={Z_RET_PAD_FACE:.3f} but a module seated "
    f"on the bezel puts its back face at z={Z_MODULE_BACK:.3f} -- "
    f"{abs(Z_MODULE_BACK - Z_RET_PAD_FACE):.3f} mm apart, so the retainer "
    f"either misses the module or crushes it against the glass seat")

# THE MODULE POCKET FLOOR IS NOT A SEAT AND MUST NOT BECOME ONE.
MODULE_FLOOR_CLR = Z_MODULE_PCB_FRONT - MODULE_POCKET_Z0
assert MODULE_FLOOR_CLR >= 0, (
    f"the module pocket floor stands {-MODULE_FLOOR_CLR:.3f} mm proud of the "
    f"module's PCB face -- two seats at one height, which is the exact defect "
    f"rev 5 exists to remove")

BACK_PARTS = {
    "coin clip BT1": (50.24, 66.65, 48.58, 71.82),
    "J9 USB-C": (32.53, 41.47, 66.89, 70.06),
    "button SW3": (3.71, 11.39, 29.15, 35.15),
    "button SW4": (3.71, 11.39, 39.15, 45.15),
    "button SW1": (62.61, 70.29, 29.15, 35.15),
    "button SW2": (62.61, 70.29, 39.15, 45.15),
}
BUTTONS = [(7.55, 32.15), (7.55, 42.15), (66.45, 32.15), (66.45, 42.15)]


C3_X = (26.62 + 11.38) / 2          # 19.00, from J5/J6 being exactly 15.24 apart

SCREEN_MIN_INSET = min(20.08 - SCREEN_W / 2, 20.08 - SCREEN_H / 2)


def bxy(x, y):
    """Board coords (KiCad: X right, Y DOWN, seen from the FRONT) -> case CAD.

    BOTH axes flip. This was wrong until 2026-08-05 and it flipped only Y.

    WHY BOTH. The board goes into the case front-face-first, so it is turned
    over -- and a real board can only be ROTATED, never mirrored. Turning it
    over is 180 degrees about an in-plane axis, which reverses ONE in-plane
    axis and the normal. Reversing Y alone (the old version) reverses one
    in-plane axis and NOT the normal, which is a reflection: a board that
    cannot exist. With Z fixed by the case, both X and Y must flip.

    HOW IT WAS CAUGHT, and why no check saw it for two revisions: every clash
    test used the components' axis-aligned BOUNDING BOXES, and a box is
    symmetric -- it lands in the same place whether the board is placed
    correctly or mirrored. Loading the real board solid and measuring one
    asymmetric part settled it in seconds:

        coin-cell holder, measured in the placed assembly : X  7.35 .. 23.76
        where the old convention said it was              : X 50.24 .. 66.65

    and the user, holding the board, back facing them:

        > "Coin cell's on the left, USB-C is on the right."

    which is the same answer.

    WHAT IT ACTUALLY BROKE: almost nothing, because nearly every feature on
    this board is symmetric in X -- the four screens, four mounting holes,
    four buttons and J9 are all centred or symmetric pairs, so they land
    identically either way. The single exception was the C3's USB-C slot,
    which was cut 36 mm from the port, on the far side of the same wall.
    """
    return (BOARD - x, BOARD_H - y)


# The C3's USB-C in CASE coordinates. Kept as a derived value rather than
# letting a part script use the raw board figure: the raw one is the number
# that was 36 mm wrong.
C3_CASE_X = bxy(C3_X, 0)[0]


# ---- the contract, checked here so BOTH parts inherit it -----------------
# A lamina is measured in LAYERS, not extrusions. Testing it against the wall
# rule is the same category error the bezel thickness itself made.
assert abs(BEZEL_T / LAYER - round(BEZEL_T / LAYER)) < 1e-9, (
    f"bezel {BEZEL_T} is not a whole number of {LAYER} mm layers")
assert BEZEL_T >= 4 * LAYER, "a bezel under four layers is translucent in PLA"
assert POCKET_DEPTH > 0, "the screen pocket has no depth"
assert BOSS_ENGAGE > 0, "no thread engagement left in the corner boss"
assert BOSS_D >= 2 * 3.0, "M3 boss must be at least 2 x screw diameter"
assert TUBE_BORE > 3.2, "the plate tube must clear an M3 shank"
# The rule the whole revision exists for. A boss reaches BOSS_D/2 from the
# mounting-hole centre; the mounting holes sit 4.0 mm in from the board edge,
# so a boss stays inside the board -- which is FINE, because it lives BELOW
# the board's plane. Nothing above that plane may overlap the footprint.
# The board must still clear the front face, and the screws must still find
# enough material. Both are now properties of the FACE, not of a boss.
assert BOARD_GAP > 0, (
    f"the pocket is deeper than the module is tall: face {FACE_T} vs board at "
    f"{Z_BOARD_FRONT}")
assert BOSS_ENGAGE >= 2 * 2.0, (
    f"only {BOSS_ENGAGE:.2f} mm of thread in the face -- under 2x diameter")
assert POCKET_DEPTH < FRONT_STACK, (
    "a pocket deeper than the module would bottom the module out before the "
    "board lands on the face")

# THE BEZEL RIM. The window is cut through BEZEL_T inside the module pocket, so
# the frame left around the glass must be real material on ALL FOUR sides of
# every screen. A rim under one extrusion width is not a rim; the window simply
# opens into the pocket and the module's PCB edge shows. This is checked here,
# in the contract, because the bad version passed every check the PART could
# run -- it was manifold, correctly positioned and dimensionally exact.
POCKET_W = SCREEN_W + 2 * POCKET_FIT
POCKET_H = SCREEN_H + 2 * POCKET_FIT

# THE CHECK THAT SHOULD HAVE EXISTED. The old one measured the window against
# the POCKET, which is the wrong pair entirely: it asked "does the window stay
# inside the module footprint" and answered yes while the window ran 0.25 mm
# past the GLASS on every long side. What you see through a window is whatever
# is directly behind it, so the pair that matters is WINDOW vs GLASS.
#
# Worst case: the glass sits hard against one wall of its pocket (GLASS_FIT).
_ov_x = (GLASS_PANEL_W - WINDOW_W) / 2 - GLASS_FIT
_ov_y = (GLASS_PANEL_H - WINDOW_H) / 2 - GLASS_FIT - abs(
    ACTIVE_OFFSET - GLASS_CENTRE_OFFSET)
GLASS_OVERLAP_MIN = min(_ov_x, _ov_y)
assert GLASS_OVERLAP_MIN >= 2 * EXT_OUTER, (
    f"the window overlaps the glass by only {GLASS_OVERLAP_MIN:.3f} mm at "
    f"worst case, under two extrusions ({2 * EXT_OUTER:.2f}) -- you would see "
    f"past the glass edge into the housing, which is the defect this revision "
    f"exists to cure")

# And the window must still show every pixel, with the module at either extreme
# of its pocket. A window sized to the pixels is only right if it cannot crop.
assert WINDOW_MARGIN > GLASS_FIT, (
    f"window margin {WINDOW_MARGIN} does not exceed the glass fit {GLASS_FIT} "
    f"-- a module pushed to one wall would crop the lit area")

# The glass pocket must fit inside the module pocket it is cut into, or its
# locating walls do not exist.
assert GLASS_POCKET_W < POCKET_W and GLASS_POCKET_H < POCKET_H, (
    f"glass pocket {GLASS_POCKET_W:.2f} x {GLASS_POCKET_H:.2f} is not inside "
    f"the module pocket {POCKET_W:.2f} x {POCKET_H:.2f}")
assert MODULE_POCKET_D > 0, "no module pocket left below the glass pocket"
assert GLASS_POCKET_D < POCKET_DEPTH, (
    "the glass pocket is deeper than the whole screen pocket")
