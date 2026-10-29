r"""Assembly coordinates are NOT print coordinates.

Every part here is exported in the shared case frame so the assembly check can
place shell, board and plate in one system. The slicer takes whatever it is
given and drops the lowest point onto the bed -- so the backplate, whose
standoff tubes run toward -Z, would be stood on its tube ends with the whole
plate as a 5000 mm2 ceiling in the air.

This writes a PRINT-ORIENTED copy of each part, and states the orientation out
loud, which is DFM rule 7. Nothing else transforms the geometry.
"""
import sys, os
from build123d import *
import cadkit
import case_params as P

HERE = os.path.dirname(os.path.abspath(__file__))

JOBS = {
    # part            : (rotation to put the printed-down face on the bed, why)
    "4square-case-rev5-shell":     (None,
        "front outer face is already at z=0 and prints face-down on the bed"),
    "4square-case-rev5-backplate": (Rotation(180, 0, 0),
        "flipped: outer face down, standoff tubes and plungers grow upward"),
    # THE RETAINER. Its only flat face is the BOARD side, at the TOP of the
    # part in case coordinates, so it has to be turned over: board side down,
    # pads growing up, which is the orientation every check in the part script
    # was written against. Left as exported it would stand on four pads with
    # the whole body in the air.
    # THE PRINTED BACK. Same reason as the backplate: its standoff tubes run
    # toward -Z, so as exported it would stand on four tube ends with the whole
    # panel as a ceiling in the air.
    "4square-case-rev5-printed-back": (Rotation(180, 0, 0),
        "flipped: outer face down, standoff tubes grow upward"),
    # THE STANDOFF FRAME. Authored with z=0 at the BOARD'S BACK FACE and the
    # web at the top, against the acrylic. Turned over it prints WEB-DOWN: the
    # web lies flat on the bed and all four pegs grow upward off it, so there
    # is no down-facing face anywhere above the plate. Left as exported it
    # would stand on four peg ends with the whole web in the air.
    "4square-case-rev5-standoff-frame": (Rotation(180, 0, 0),
        "flipped: the web lies on the bed, the four pegs grow upward"),
    "4square-case-rev5-retainer-half-a": (Rotation(180, 0, 0),
        "flipped: board side (the seat) down on the bed, pads growing upward"),
    "4square-case-rev5-retainer-half-b": (Rotation(180, 0, 0),
        "flipped: board side (the seat) down on the bed, pads growing upward"),
}

for base, (rot, why) in JOBS.items():
    src = os.path.join(HERE, base + ".step")
    if not os.path.exists(src):
        print(f"skip {base}: not built yet"); continue
    part = import_step(src)
    src_vol = part.volume
    src_bb = part.bounding_box()
    if rot is not None:
        part = rot * part
    bb = part.bounding_box()
    part = Pos(0, 0, -bb.min.Z) * part          # sit it on the bed
    bb = part.bounding_box()

    # ---- THIS FILE ONLY EVER MOVES A PART, SO THAT IS WHAT IT ASSERTS -----
    # It exports geometry, so cad-practices rule 8 applies: the build must FAIL
    # rather than write a wrong file. The part itself was already verified by
    # its own script -- cadkit.export refuses to write a .step otherwise -- so
    # what has to be proved HERE is that the rigid move changed nothing but the
    # pose, and that the result really is standing on the bed. A silently
    # mangled transform would otherwise reach the slicer looking fine.
    assert abs(part.volume - src_vol) < 1e-3, (
        f"{base}: the re-orientation changed the volume, "
        f"{src_vol:.3f} -> {part.volume:.3f} mm3 -- that is not a rigid move")
    assert sorted(round(v, 4) for v in (bb.size.X, bb.size.Y, bb.size.Z)) == \
        sorted(round(v, 4) for v in (src_bb.size.X, src_bb.size.Y,
                                     src_bb.size.Z)), (
        f"{base}: the bounding box is a different SHAPE after the move, "
        f"{src_bb.size} -> {bb.size}")
    assert abs(bb.min.Z) < 1e-6, (
        f"{base}: sits at z={bb.min.Z:.4f}, not on the bed -- the slicer would "
        f"drop it and every height in this report would be a fiction")
    assert len(part.solids()) == 1, (
        f"{base}: {len(part.solids())} solids after the move, expected 1")

    out = os.path.join(HERE, base + "-print.stl")
    export_stl(part, out)
    print(f"{base}\n    {why}\n    on the bed: {bb.size.X:.2f} x {bb.size.Y:.2f} x "
          f"{bb.size.Z:.2f} mm, z {bb.min.Z:.2f}..{bb.max.Z:.2f}\n    wrote {out}")
