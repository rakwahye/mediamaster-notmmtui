# SPDX-License-Identifier: LGPL-2.1-or-later
"""Rasterise sketch dumps (glyph, fg, bg per cell) into images.
Block, sextant, octant and braille glyphs are drawn geometrically, so the
picture is what a terminal with a complete font would show."""
import json, os
from PIL import Image, ImageDraw, ImageFont

CW, CH = 10, 20
DEF_FG, DEF_BG = (0xE6, 0xF0, 0xFF), (0x05, 0x06, 0x0E)
here = os.path.dirname(os.path.abspath(__file__))
MASKS = json.load(open(os.path.join(here, "glyphs.json"), encoding="utf-8"))
FONT = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", 16)
FONTB = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf", 16)


def hexrgb(h):
    v = int(h, 16)
    return (v >> 16 & 255, v >> 8 & 255, v & 255)


def load(path):
    frames, cur = [], []
    with open(path, encoding="utf-8") as f:
        rows, cols, octok, name = f.readline().split()
        for line in f:
            line = line.rstrip("\n")
            if line == "--":
                frames.append(cur)
                cur = []
                continue
            g, fg, bg, fd, bd, st = line.split("\t")
            cur.append((g, DEF_FG if fd == "1" else hexrgb(fg), DEF_BG if bd == "1" else hexrgb(bg), int(st)))
    return int(rows), int(cols), name, frames


def eighth(cp):
    """(x0, y0, x1, y1) in cell fractions for the U+2580 block family."""
    if 0x2581 <= cp <= 0x2588:
        k = cp - 0x2580
        return (0, 1 - k / 8, 1, 1)
    if 0x2589 <= cp <= 0x258F:
        k = 0x2590 - cp
        return (0, 0, k / 8, 1)
    return {0x2580: (0, 0, 1, .5), 0x2590: (.5, 0, 1, 1), 0x2594: (0, 0, 1, 1 / 8), 0x2595: (7 / 8, 0, 1, 1),
            0x1FB82: (0, 0, 1, .25)}.get(cp)


def cell(d, x0, y0, g, fg, bg, st):
    d.rectangle([x0, y0, x0 + CW - 1, y0 + CH - 1], fill=bg)
    if g == " " or g == "":
        return
    cp = ord(g[0]) if len(g) == 1 else 0
    m = MASKS.get(g)
    if m:
        gw, gh = m["g"]
        for r in range(gh):
            for c in range(gw):
                if m["ink"][r][c]:
                    xa, xb = x0 + c * CW // gw, x0 + (c + 1) * CW // gw - 1
                    ya, yb = y0 + r * CH // gh, y0 + (r + 1) * CH // gh - 1
                    d.rectangle([xa, ya, xb, yb], fill=fg)
        return
    e = eighth(cp)
    if e:
        d.rectangle([x0 + round(e[0] * CW), y0 + round(e[1] * CH), x0 + round(e[2] * CW) - 1,
                     y0 + round(e[3] * CH) - 1], fill=fg)
        return
    if 0x2800 <= cp <= 0x28FF:
        bits = cp - 0x2800
        dots = [(0, 0, 0), (0, 1, 1), (0, 2, 2), (1, 0, 3), (1, 1, 4), (1, 2, 5), (0, 3, 6), (1, 3, 7)]
        for c, r, b in dots:
            if bits >> b & 1:
                cx, cy = x0 + 2.5 + c * 5, y0 + 2.5 + r * 5
                d.ellipse([cx - 1.7, cy - 1.7, cx + 1.7, cy + 1.7], fill=fg)
        return
    if cp in (0x2500, 0x2501):
        t = 1 if cp == 0x2500 else 3
        d.rectangle([x0, y0 + CH // 2 - t // 2, x0 + CW - 1, y0 + CH // 2 + (t - 1) // 2], fill=fg)
        return
    d.text((x0 + CW / 2, y0 + CH / 2 + 1), g, font=FONTB if st & 2 else FONT, fill=fg, anchor="mm")


def frame_image(rows, cols, cells, pad=0):
    im = Image.new("RGB", (cols * CW + 2 * pad, rows * CH + 2 * pad), DEF_BG)
    d = ImageDraw.Draw(im)
    for i, (g, fg, bg, st) in enumerate(cells):
        y, x = divmod(i, cols)
        cell(d, pad + x * CW, pad + y * CH, g, fg, bg, st)
    return im
