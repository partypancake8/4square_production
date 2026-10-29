#!/usr/bin/env python3
"""Assemble prover frames into the 2x2 the user actually looks at.

The contact sheet is one tile per frame in dump order, which is the right shape
for scanning 264 layouts for something ugly. It is the wrong shape for judging a
PAGE, because a page is four panels seen at once and the question is whether
they read as one thing.

So: pick the frames matching a filter, group them by their `slotN` field, and
lay each group out the way the panels physically sit  - 

    slot0 top-left      slot1 top-right
    slot2 bottom-left   slot3 bottom-right

which is Slot in config.h. Canvas space is what is drawn here, and that is
correct: display.cpp rotates the top row 180 degrees during the blit precisely
so both rows read upright to someone standing in front of the clock.

    ./pagemock.py out SETTINGS out/settings-mock.png

The gap between the tiles is the real one. The four modules do not butt up
against each other on the board, and a layout that only works when you imagine
them touching is a layout that will disappoint in the case.
"""
import sys, os, re
from PIL import Image, ImageDraw

W, H, SCALE = 128, 64, 4
SAFE = 6                       # must match SHIFT_MAX in display.h
GAP, PAD, LABEL_H = 14, 16, 24


def frame_image(raw, idx):
    """One 1-bit panel bitmap -> a scaled RGB tile with the safe area marked."""
    stride = (W + 7) // 8
    off = idx * stride * H
    img = Image.new("RGB", (W, H), (0, 0, 0))
    px = img.load()
    for y in range(H):
        row = off + y * stride
        for x in range(W):
            if raw[row + (x >> 3)] & (0x80 >> (x & 7)):
                px[x, y] = (150, 235, 255)     # the panels are white; this
                                               # reads as "lit OLED" on screen
    img = img.resize((W * SCALE, H * SCALE), Image.NEAREST)
    d = ImageDraw.Draw(img)
    d.rectangle([SAFE * SCALE, SAFE * SCALE,
                 (W - SAFE) * SCALE - 1, (H - SAFE) * SCALE - 1],
                outline=(60, 60, 70))
    return img


def main():
    outdir = sys.argv[1] if len(sys.argv) > 1 else "out"
    want = sys.argv[2] if len(sys.argv) > 2 else "SETTINGS"
    dest = sys.argv[3] if len(sys.argv) > 3 else os.path.join(outdir, "pagemock.png")

    binp = os.path.join(outdir, "frames.bin")
    txtp = os.path.join(outdir, "frames.txt")
    labels = [l.rstrip("\n") for l in open(txtp)]
    raw = open(binp, "rb").read()

    # Group by everything in the label EXCEPT the slot, so one group is one
    # screenful across the four panels.
    groups = {}
    for i, lab in enumerate(labels):
        if want not in lab:
            continue
        m = re.search(r"slot(\d)", lab)
        if not m:
            continue
        key = re.sub(r"\s*slot\d", "", lab)
        groups.setdefault(key, {})[int(m.group(1))] = i
    if not groups:
        print(f"no frames matching {want!r}")
        return

    tw, th = W * SCALE, H * SCALE
    gw, gh = tw * 2 + GAP, th * 2 + GAP + LABEL_H
    keys = sorted(groups)
    cols = min(3, len(keys))
    rows = (len(keys) + cols - 1) // cols

    sheet = Image.new("RGB", (cols * gw + PAD * (cols + 1),
                              rows * gh + PAD * (rows + 1)), (18, 18, 22))
    d = ImageDraw.Draw(sheet)
    for n, key in enumerate(keys):
        gx = PAD + (n % cols) * (gw + PAD)
        gy = PAD + (n // cols) * (gh + PAD)
        d.text((gx, gy + 6), key, fill=(190, 190, 200))
        for slot, idx in groups[key].items():
            ox = gx + (slot % 2) * (tw + GAP)
            oy = gy + LABEL_H + (slot // 2) * (th + GAP)
            sheet.paste(frame_image(raw, idx), (ox, oy))

    sheet.save(dest)
    print(f"{len(keys)} page(s) -> {dest}")


if __name__ == "__main__":
    main()
