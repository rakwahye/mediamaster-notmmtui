# notmmtui sketches

Six looks for a strip one to four rows tall, before any of it is the
real thing. Each is drawn by Notcurses 3.0.17 (octants on, as under
foot or kitty), dumped cell by cell after every frame, and rasterised.
The audio is made up; nothing talks to the daemon.

![still](out/notmmtui-sketches.png)

`out/notmmtui-sketches.gif` plays a track change (the title decodes in),
playback, then a pause from frame 34: lit parts breathe, the scope
freezes, the spectrum falls.

| | rows | idea | Notcurses doing the work |
|---|---|---|---|
| powerline | 1 | the row is the progress bar; playhead to an eighth of a cell | left-eighth blocks, truecolour per cell |
| plasma | 1 | plasma behind what has played, dots ahead | half blocks (2 px a row); text on an `NCALPHA_BLEND` veil |
| scope | 2 | title/artist, L/R meter, oscilloscope with a ghost | `NCBLIT_4x2` octants; eighth blocks |
| road | 3 | spectrum over its wet-road reflection, falling peaks | eighth blocks, inverted for the reflection |
| wires | 3 | braille pylons and power lines; a comet rides to the playhead | `NCBLIT_BRAILLE` |
| horizon | 4 | the reference's road: a grid rushing in on the beat, vanishing point = playhead | octants, half-block sky |

## Run

Needs Notcurses ≥ 3.0.17 (`notcurses-core`), Python 3 with Pillow, and
DejaVu Sans Mono.

    cc -std=c11 -Wall -Wextra -O2 sketch.c -o sketch $(pkg-config --cflags --libs notcurses-core) -lm
    cc -shared -fPIC -O2 wcshim.c -o wcshim.so -ldl
    python3 sheet.py            # -> out/

- `sketch <concept> <frames> <dump>` renders into whatever terminal it
  has and writes each frame's cells (glyph, fg, bg, style) to `dump`.
- `run.py` gives it a pty that answers Notcurses' startup queries as
  foot 1.21 would, so octants and truecolour are on without a display.
- `wcshim.so` (LD_PRELOAD) makes octants one column wide on a glibc
  older than Unicode 16; Arch's glibc needs nothing.
- `raster.py` draws block, sextant, octant and braille glyphs as
  geometry; `glyphs.json` is Notcurses' own sextant/octant tables, made
  by `glyphs.py <notcurses source>`.
- `sheet.py` runs them all into one PNG and one GIF.
