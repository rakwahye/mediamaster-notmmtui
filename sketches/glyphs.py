# SPDX-License-Identifier: LGPL-2.1-or-later
"""Write glyphs.json: which dots of a cell each sextant and octant glyph
inks, read from Notcurses' own tables (src/lib/blit.c), so raster.py draws
exactly what the blitter meant. Usage: glyphs.py <notcurses source dir>"""
import json, os, re, sys

src = open(os.path.join(sys.argv[1], "src/lib/blit.c"), encoding="utf-8").read()


def table(name):
    body = re.search(r"%s\[\d+\] = \{(.*?)\n\};" % name, src, re.S).group(1)
    out = []
    for line in body.split("\n"):
        for s in re.findall(r'(?:u8)?"((?:[^"\\]|\\.)*)"', line.split("//")[0]):
            out.append(re.sub(r"\\U([0-9a-fA-F]{8})", lambda q: chr(int(q.group(1), 16)), s))
    return out


m = {}
for name, w, h in (("sextrans", 2, 3), ("octtrans", 2, 4)):
    for i, g in enumerate(table(name)):  # bit set in the index = dot NOT inked
        ink = ~i & ((1 << (w * h)) - 1)
        m.setdefault(g, {"g": [w, h], "ink": [[ink >> (r * 2) & 1, ink >> (r * 2 + 1) & 1] for r in range(h)]})
json.dump(m, open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "glyphs.json"), "w"), ensure_ascii=False)
