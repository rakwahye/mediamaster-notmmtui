# SPDX-License-Identifier: LGPL-2.1-or-later
"""Run every concept and lay the strips out on one sheet: a still PNG
(frame `still`) and an animated GIF of all of them at once."""
import os, subprocess, sys
from PIL import Image, ImageDraw, ImageFont
import raster

COLS, FRAMES = 96, 48
CONCEPTS = [  # name, rows, caption
    ("powerline", 1, "1 · powerline — 1 row. The row is the bar, playhead to ⅛ cell; title decodes in on track change"),
    ("plasma", 1, "2 · plasma — 1 row, 2 px tall in half blocks; text on an NCALPHA_BLEND veil, no box"),
    ("scope", 2, "3 · scope — 2 rows. Octant oscilloscope (4×2 dots a cell) with a phosphor ghost; L/R VU in eighths"),
    ("road", 3, "4 · road — 3 rows. Eighth-block spectrum over its wet-road reflection; playhead on the horizon"),
    ("wires", 3, "5 · wires — 3 rows. Braille pylons and power lines; a comet rides the line to the playhead"),
    ("horizon", 4, "6 · horizon — 4 rows. The picture's road: octant grid rushing in on the kick; vanishing point = playhead"),
]
here = os.path.dirname(os.path.abspath(__file__))
out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "out")
os.makedirs(out, exist_ok=True)
LABEL = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 14)
PAD, GAP, LH, MARGIN = 6, 18, 22, 24

strips = []
for name, rows, cap in CONCEPTS:
    dump = os.path.join(out, name + ".dump")
    subprocess.run([sys.executable, os.path.join(here, "run.py"), str(rows), str(COLS), name, str(FRAMES), dump],
                   check=True)
    r, c, _, frames = raster.load(dump)
    imgs = [raster.frame_image(r, c, f, pad=PAD) for f in frames]
    imgs[FRAMES // 2].save(os.path.join(out, name + ".png"))
    strips.append((cap, imgs))

W = COLS * raster.CW + 2 * PAD + 2 * MARGIN
H = MARGIN + sum(LH + s[1][0].height + GAP for s in strips) + MARGIN - GAP


def compose(i):
    sheet = Image.new("RGB", (W, H), (0x0B, 0x0C, 0x16))
    d = ImageDraw.Draw(sheet)
    y = MARGIN
    for cap, imgs in strips:
        d.text((MARGIN, y + 2), cap, font=LABEL, fill=(0x9A, 0xA4, 0xC8))
        y += LH
        im = imgs[i]
        d.rectangle([MARGIN - 1, y - 1, MARGIN + im.width, y + im.height], outline=(0x2A, 0x2E, 0x48))
        sheet.paste(im, (MARGIN, y))
        y += im.height + GAP
    return sheet


compose(FRAMES // 2).save(os.path.join(out, "notmmtui-sketches.png"))
frames = [compose(i) for i in range(FRAMES)]
frames[0].save(os.path.join(out, "notmmtui-sketches.gif"), save_all=True, append_images=frames[1:],
               duration=80, loop=0, optimize=False)
print(os.path.join(out, "notmmtui-sketches.png"))
