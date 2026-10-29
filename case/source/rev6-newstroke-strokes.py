r"""
Pull REAL newstroke stroke geometry out of KiCad, for the rev 6 engrave.

    ~/.venvs/cad/bin/python rev6-newstroke-strokes.py
    -> rev6-newstroke-strokes.json

WHY THIS EXISTS RATHER THAN A FONT FILE. The engrave has to be "the same font
as the PCB silkscreen". That font is **newstroke**, and newstroke is not
installed on this machine as a font file at all -- KiCad compiles it in
(`newstroke_font.cpp`), so there is nothing for a TTF loader to open. Rather
than approximate it with some other sans, this asks the authority directly:
it writes a throwaway board carrying our exact strings on F.Silkscreen, runs
`kicad-cli pcb export dxf`, and reads back the very same stroke segments the
plotter puts on a real fabrication file.

CONFIRMED THE BOARD REALLY USES IT: justclock-pcb.kicad_pcb contains no
`(face "...")` font override anywhere, so every silk string on the 4square
board is plotted with KiCad's default font, which is newstroke. The dominant
thickness/size ratio in that file is 0.15, which is what is used here.

WHAT COMES BACK IS CENTRELINES, NOT OUTLINES. A stroke font has no filled
contours: each glyph is a set of open paths that a pen follows. That is
strictly better for a laser engrave than the outline text rev 5 used -- the
head traces one pass per stroke instead of outlining and filling -- but it
means the ENGRAVE layer is open polylines, and that the marked width is the
stroke thickness rather than anything in the geometry. Both facts are carried
in the JSON and handled by the part script.

Geometry is emitted at SIZE = 10.0 mm and normalised so the ink bounding box
starts at (0, 0). A stroke font scales linearly, so the part script can solve
for any size by multiplying -- no further kicad-cli calls at build time, and
the build stays reproducible from the JSON alone.
"""
import json
import os
import shutil
import subprocess
import tempfile

import ezdxf

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "rev6-newstroke-strokes.json")

SIZE = 10.0                 # reference size; everything scales from here
THICK_RATIO = 0.15          # KiCad's default, and this board's dominant ratio
STRINGS = ["SAWYER", "SMITH", "4SQUARE V1"]

BOARD = """(kicad_pcb
  (version 20241229)
  (generator "pcbnew")
  (generator_version "9.0")
  (general (thickness 1.6) (legacy_teardrops no))
  (paper "A4")
  (layers
    (0 "F.Cu" signal)
    (31 "B.Cu" signal)
    (36 "B.SilkS" user "B.Silkscreen")
    (37 "F.SilkS" user "F.Silkscreen")
    (44 "Edge.Cuts" user)
  )
  (setup (pad_to_mask_clearance 0))
  (gr_text "%(text)s"
    (at 100 100 0)
    (layer "F.SilkS")
    (uuid "11111111-1111-1111-1111-111111111111")
    (effects (font (size %(size).4f %(size).4f) (thickness %(thick).4f))
             (justify left bottom))
  )
)
"""


def strokes_for(text):
    """One string -> its newstroke centreline polylines, at SIZE."""
    tmp = tempfile.mkdtemp(prefix="newstroke-")
    try:
        pcb = os.path.join(tmp, "t.kicad_pcb")
        dxf = os.path.join(tmp, "out.dxf")
        with open(pcb, "w") as fh:
            fh.write(BOARD % {"text": text, "size": SIZE,
                              "thick": SIZE * THICK_RATIO})
        r = subprocess.run(
            ["kicad-cli", "pcb", "export", "dxf", "--mode-single",
             "-o", dxf, "--layers", "F.SilkS", "--output-units", "mm", pcb],
            capture_output=True, text=True)
        if r.returncode != 0 or not os.path.exists(dxf):
            raise RuntimeError(f"kicad-cli failed for {text!r}: {r.stderr}")

        d = ezdxf.readfile(dxf)
        polys = []
        for e in d.modelspace():
            t = e.dxftype()
            if t == "LINE":
                polys.append([(e.dxf.start[0], e.dxf.start[1]),
                              (e.dxf.end[0], e.dxf.end[1])])
            elif t == "LWPOLYLINE":
                polys.append([(p[0], p[1]) for p in e.get_points("xy")])
            elif t == "ARC":
                ct = e.construction_tool()
                polys.append([(p.x, p.y) for p in ct.flattening(0.01)])
            else:
                raise RuntimeError(f"unexpected DXF entity {t} for {text!r}")
        if not polys:
            raise RuntimeError(f"no stroke geometry came back for {text!r}")

        # KiCad plots with the page's Y running down, so the DXF comes out
        # below the axis. Normalise to a (0,0) origin; the SHAPE is already
        # upright, which is checked by the part script's cap-height assertion
        # and by looking at the render.
        xs = [p[0] for pl in polys for p in pl]
        ys = [p[1] for pl in polys for p in pl]
        x0, y0 = min(xs), min(ys)
        polys = [[(round(x - x0, 6), round(y - y0, 6)) for x, y in pl]
                 for pl in polys]
        return {
            "polylines": polys,
            "w": round(max(xs) - x0, 6),
            "h": round(max(ys) - y0, 6),
            "segments": sum(len(pl) - 1 for pl in polys),
        }
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    if shutil.which("kicad-cli") is None:
        raise SystemExit("kicad-cli is not on PATH -- newstroke comes from it")
    v = subprocess.run(["kicad-cli", "version"], capture_output=True,
                       text=True).stdout.strip()

    data = {"font": "newstroke (KiCad built-in, the board's silkscreen font)",
            "source": "kicad-cli pcb export dxf, F.Silkscreen",
            "kicad_version": v, "size": SIZE, "thickness_ratio": THICK_RATIO,
            "strings": {}}
    # THE CAP HEIGHT IS MEASURED OFF A CONTROL GLYPH, NOT OFF THE STRINGS.
    # A stroke font's cap height IS its size -- but only for glyphs that sit
    # on the baseline and stop at the cap line. Measured here:
    #     "O"  10.000   exactly the size, so this is the datum
    #     "Q"  10.952   it has a descending tail
    #     "4"  10.476   the digit rises above the cap line
    # so "4SQUARE V1" honestly measures 11.429 tall and asserting 10.000 on it
    # (the first version of this check) failed a font that was perfectly fine.
    cap = strokes_for("O")["h"]
    assert abs(cap - SIZE) < 0.02, (
        f"newstroke cap height came back {cap}, expected {SIZE} -- the size "
        f"solver in the part script assumes cap height == size")
    data["cap_height"] = round(cap, 6)

    for s in STRINGS:
        g = strokes_for(s)
        data["strings"][s] = g
        print(f"  {s!r:14s} {len(g['polylines']):3d} paths, "
              f"{g['segments']:3d} segments, {g['w']:.3f} x {g['h']:.3f} mm "
              f"at size {SIZE} (cap {cap:.3f})")
        assert SIZE * 0.95 <= g["h"] <= SIZE * 1.25, (
            f"{s!r} ink box is {g['h']} tall at size {SIZE} -- too far from "
            f"the cap height to be ascenders and descenders")
        assert g["segments"] > 4 * len(s.replace(" ", "")), (
            f"{s!r} came back with only {g['segments']} segments -- that is "
            f"not a drawn glyph set")

    with open(OUT, "w") as fh:
        json.dump(data, fh, indent=1)
    print(f"\nwrote {OUT}   ({v})")
