<!-- SPDX-License-Identifier: LGPL-2.1-or-later -->
# notmmtui

A strip of a player for [mediamaster](https://github.com/rakwahye/mediamaster),
one to a few rows tall, drawn with Notcurses. It is a client like `mmctl`:
everything it shows comes from the daemon's socket.

One row: what plays and its time. In the default look, `--look powerline`,
the row is the progress bar: the sky gradient lit up to the playhead, dark
after it. `--look plasma`: plasma moving behind what has played, still when
paused. `--look plain` draws it on the theme's background. A two-cell meter
before the time shows the left and right peaks from the daemon's
`watch levels`, and the looks move with them.

Two rows: `--look scope`, the default there. The words take the
left on two rows, the meter stands two cells tall, and an oscilloscope
drawn from the daemon's 32 bands runs on the right in octants, its old
traces fading like phosphor.

Three rows: `--look road`, the default there. The bands stand as
bars across the whole width, bright up to the playhead and dim after it,
with peaks that hold and fall; the last row is a wet road with their
reflection and the words of one row on it.

Four to eight rows: `--look horizon`, the default there. A road runs to a
sun on the horizon, and the sun is the playhead; the mountains either side
are the bands, bass by the sun; the cross-ties rush in faster as it gets
louder and on every beat. The words are on the sky above.

Nine rows or more: `--look stage`, the default there; a sun setting over
a lake. The sun is the playhead on an arc, rising at the left as the
track starts and setting at the right at its end; stars over it, and
mountains of the bands on the horizon. The lake mirrors it all, the
sun's light a broken column on it, glints on the water as loud as the
music. The title is big, in a 5×7 font drawn in octants, bigger as the
stage is taller; artist and album under it, what is next in the queue
at the right; the time, a bar and the meter along the bottom. `l` switches between the looks the strip is tall
enough for, as it runs. Keys: space toggles,
n/p next/prev, ←/→ seek 5 s (⇧ 30 s), +/- volume, s stop, l look, q quits. Run
it in a short terminal or pane; it fills what it is given.

Tab opens the queue over the strip, on the item playing: j/k (↑/↓) select,
g/G the first and last, J/K (⇧↓/⇧↑) move it down or up, N puts it next
after the item playing, ⏎ plays it, d removes it, a opens a line to add
from; Tab, Esc or q close it. Moving needs a daemon with `queue mv`.

The add line knows directories. A path is from your home (`~` is it, and
an empty line lists it), and the rows above the line list what starts
with what you have typed, directories first. Tab completes as readline
does; ⏎ on a directory goes into it, on a file adds it by its whole path;
a URI (`scheme:...`) is added as typed. Ctrl+U clears the line, Ctrl+W
takes back a directory or a word, Esc leaves it. A key with modifiers
types what it says, so a `~` behind Ctrl still types `~`. The keys
above still work under it. It is a pane of smoked glass, as
notcurses-demo draws its HUD: the look goes on under it, glyphs and all,
dimly, without its words; the queue's words sit on it solid. From four rows a header says how
many and the keys; on one row it is the item selected alone. It follows the
daemon through `watch`, and reconnects when the daemon restarts.

Daemon: it talks to mediamaster over its socket and nothing else. It is
built against the installed mediamaster SDK and applies the SDK's one
rule to the daemon it finds (its `version` says which ABI it offers): a
daemon with a newer minor is fine, one of another major is refused. On
start that is an error; while running the strip says so until a daemon
it fits comes back. `mm-update` rebuilds it with the daemon.

Socket: `--socket <path>`, else `$MM_SOCK`, else the daemon's default,
as `mmctl` finds it; for a daemon started with `-o host.socket=<path>`:

    notmmtui --socket <path>

Colours: `--theme powerlines` (the default: magenta and electric cyan on
black), `--theme plain` (the terminal's own), or `--theme <file>` with
`role = #rrggbb` lines over powerlines; roles are bg fg cyan magenta
violet orange green dim.

    git clone https://github.com/rakwahye/mediamaster-notmmtui
    make && make test          # mediamaster installed first: it builds against its SDK
    sudo make install          # /usr/local/bin/notmmtui; PREFIX=, DESTDIR= as usual

Or let `mm-update` keep it current with the daemon: `mm-update add
~/path/to/mediamaster-notmmtui` once, then `mm-update`. `make uninstall`
removes it.

Needs a C11 compiler, pkg-config and Notcurses 3 (`notcurses-core` and its
headers; Arch: `notcurses`, or a source install under /usr/local, which is
looked for too). `make test` runs this client against the installed
daemon with a null sink, in a pty driven by python3.
