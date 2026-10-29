#!/usr/bin/env python3
"""Compose pageshot's four slot frames into the 2x2 the device actually shows.

Lit pixels are drawn white, as on the glass. Every panel is captioned with its
measured fill, and anything over the 15% always-on budget is called out — the
point of the picture is to see WHERE the ink is, not just that there is a lot
of it.
"""
import os, sys
from PIL import Image, ImageDraw, ImageFont

W, H, SCALE = 128, 64, 5
BUDGET = 15.0
PAD, CAP, GAP = 26, 34, 18

def font(sz, bold=False):
    for p in ("/usr/share/fonts/truetype/dejavu/DejaVuSans%s.ttf" % ("-Bold" if bold else ""),):
        if os.path.exists(p):
            return ImageFont.truetype(p, sz)
    return ImageFont.load_default()

def main():
    outdir = sys.argv[1] if len(sys.argv) > 1 else "out"
    dest = sys.argv[2] if len(sys.argv) > 2 else os.path.join(outdir, "clock-v5.png")
    title = sys.argv[3] if len(sys.argv) > 3 else "CLOCK page, variant 5"

    labels = [l.rstrip("\n") for l in open(os.path.join(outdir, "frames.txt"))]
    raw = open(os.path.join(outdir, "frames.bin"), "rb").read()
    stride = (W + 7) // 8 * H

    tw, th = W * SCALE, H * SCALE
    sheet_w = 2 * tw + GAP + 2 * PAD
    sheet_h = 2 * (th + CAP) + GAP + 2 * PAD + 54
    sheet = Image.new("RGB", (sheet_w, sheet_h), (20, 20, 24))
    d = ImageDraw.Draw(sheet)

    d.text((PAD, 16), title, font=font(21, True), fill=(240, 240, 245))
    d.text((PAD, 42), "the four panels as the firmware draws them, at 5x",
           font=font(13), fill=(150, 150, 158))

    for i in range(min(4, len(labels), len(raw) // stride)):
        frame = raw[i * stride:(i + 1) * stride]
        tile = Image.new("RGB", (W, H), (0, 0, 0))
        px = tile.load()
        for y in range(H):
            for x in range(W):
                if frame[y * ((W + 7) // 8) + (x >> 3)] & (0x80 >> (x & 7)):
                    px[x, y] = (255, 255, 255)
        tile = tile.resize((tw, th), Image.NEAREST)

        col, row = i % 2, i // 2
        ox = PAD + col * (tw + GAP)
        oy = PAD + 54 + row * (th + CAP + GAP)
        sheet.paste(tile, (ox, oy))

        # the safe area, so clipping at the shift extremes is visible
        d.rectangle([ox + 6 * SCALE, oy + 6 * SCALE,
                     ox + (W - 6) * SCALE - 1, oy + (H - 6) * SCALE - 1],
                    outline=(70, 90, 120))

        lab = labels[i]
        pct = float(next(t for t in lab.split() if t.endswith("%")).rstrip("%"))
        over = pct > BUDGET
        d.rectangle([ox - 1, oy - 1, ox + tw, oy + th],
                    outline=(220, 90, 70) if over else (70, 130, 90), width=2)
        d.text((ox, oy + th + 7), lab.split("  ")[0],
               font=font(14, True), fill=(225, 225, 232))
        note = ("%.1f%% lit — OVER the %g%% budget" % (pct, BUDGET)) if over \
               else ("%.1f%% lit — within budget" % pct)
        d.text((ox, oy + th + 24), note, font=font(12),
               fill=(235, 120, 100) if over else (130, 200, 150))

    sheet.save(dest)
    print("wrote", dest)

main()
