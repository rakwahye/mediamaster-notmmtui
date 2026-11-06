<!-- SPDX-License-Identifier: LGPL-2.1-or-later -->
# notmmtui

A strip of a player for [mediamaster](https://github.com/rakwahye/mediamaster),
one to a few rows tall, drawn with Notcurses. It is a client like `mmctl`:
everything it shows comes from the daemon's socket.

Early days: one row, plain: what plays and its time. Keys: space toggles,
n/p next/prev, ←/→ seek 5 s (⇧ 30 s), +/- volume, s stop, q quits. Run
it in a short terminal or pane; it fills what it is given. It follows the
daemon through `watch`, and reconnects when the daemon restarts.

Colours: `--theme powerlines` (the default: magenta and electric cyan on
black), `--theme plain` (the terminal's own), or `--theme <file>` with
`role = #rrggbb` lines over powerlines; roles are bg fg cyan magenta
violet orange green dim.

    git clone --recursive https://github.com/rakwahye/mediamaster-notmmtui
    make && make test

Needs a C11 compiler, pkg-config and Notcurses 3 (`notcurses-core` and its
headers; Arch: `notcurses`, or a source install under /usr/local, which is
looked for too). `make test` builds the pinned daemon and runs this
client against it with a null sink, in a pty driven by python3.
