r"""Print-oriented copy of the REV 6 standoff frame.

A separate file on purpose. `make-print-stl.py` is a shipped rev 5 source and
the variants were asked for WITHOUT touching the shipped ones, so their print
poses live here instead of being appended to its JOBS table.

Same rule as that file: this only ever MOVES a part, so that is exactly what it
asserts -- volume unchanged, bbox the same SHAPE, sitting on the bed, still one
solid. Both variants are authored with z = 0 at the BOARD'S BACK FACE and the
web/border at the top against the acrylic, so both print WEB-DOWN: turned over,
the flat web lies on the bed and the four pegs grow upward off it. Left as
exported each would stand on four peg ends with the whole web in the air.

    ~/.venvs/cad/bin/python make-print-stl-rev6.py
"""
import os

from build123d import *
import cadkit
import case_params as P

SPACER_H = round(P.Z_PLATE_SEAT - P.Z_BOARD_BACK, 4)   # 11.15, the contract

HERE = os.path.dirname(os.path.abspath(__file__))

JOBS = {
    # Same pose as the rev 5 frame in make-print-stl.py -- this part differs
    # from it only in where the web is ROUTED, not in how it stands up.
    "4square-case-rev6-standoff-frame": (
        Rotation(180, 0, 0),
        "flipped: the web lies on the bed, the four 11.15 mm pegs grow upward"),
}

for base, (rot, why) in JOBS.items():
    src = os.path.join(HERE, base + ".step")
    if not os.path.exists(src):
        print(f"skip {base}: not built yet")
        continue
    part = import_step(src)
    src_vol = part.volume
    src_bb = part.bounding_box()
    if rot is not None:
        part = rot * part
    bb = part.bounding_box()
    part = Pos(0, 0, -bb.min.Z) * part
    bb = part.bounding_box()

    assert abs(part.volume - src_vol) < 1e-3, (
        f"{base}: the re-orientation changed the volume, "
        f"{src_vol:.3f} -> {part.volume:.3f} mm3 -- that is not a rigid move")
    assert sorted(round(v, 4) for v in (bb.size.X, bb.size.Y, bb.size.Z)) == \
        sorted(round(v, 4) for v in (src_bb.size.X, src_bb.size.Y,
                                     src_bb.size.Z)), (
        f"{base}: the bounding box is a different SHAPE after the move")
    assert abs(bb.min.Z) < 1e-6, (
        f"{base}: sits at z={bb.min.Z:.4f}, not on the bed")
    assert len(part.solids()) == 1, (
        f"{base}: {len(part.solids())} solids after the move, expected 1")
    # The height is the SPACER contract, read from the part that owns it, not
    # a literal. This line was inherited from the SHORT variants and still
    # said 7.95, which is the one number those variants changed -- it failed
    # the full-height rev 6 frame on its first run.
    assert abs(bb.size.Z - SPACER_H) < 1e-6, (
        f"{base}: {bb.size.Z:.4f} tall in print pose, expected {SPACER_H}")

    cadkit.check_basename(base)          # rule 14, same gate the parts use
    out = os.path.join(HERE, base + "-print.stl")
    export_stl(part, out)
    print(f"wrote {out}")
    print(f"    {why}")
    print(f"    {bb.size.X:.2f} x {bb.size.Y:.2f} x {bb.size.Z:.2f} mm "
          f"on the bed, {part.volume:.1f} mm3")
