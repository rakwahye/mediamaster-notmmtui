<!-- SPDX-License-Identifier: LGPL-2.1-or-later -->
# notmmtui

A strip of a player for [mediamaster](https://github.com/rakwahye/mediamaster),
one to a few rows tall, drawn with Notcurses. It is a client like `mmctl`:
everything it shows comes from the daemon's socket.

One row: what plays and its time. In the default look, `--look powerline`,
the row is the progress bar: the sky gradient lit up to the playhead, dark
after it. `--look plasma`: plasma moving behind what has played, still when
paused. `--look plain` draws it on the theme's background. `l` switches
between them as it runs. A two-cell meter before the time shows the left
and right peaks from the daemon's `watch levels`. Keys: space toggles,
n/p next/prev, ←/→ seek 5 s (⇧ 30 s), +/- volume, s stop, l look, q quits. Run
it in a short terminal or pane; it fills what it is given. It follows the
daemon through `watch`, and reconnects when the daemon restarts.

Daemon: it talks to mediamaster over its socket and nothing else. Built
against the pinned host, it refuses a daemon of another ABI (its
`version` says which): on start it exits, while running the strip says
so until a daemon it fits comes back. `mm-update` rebuilds both together.

Socket: `--socket <path>`, else `$MM_SOCK`, else the daemon's default,
as `mmctl` finds it; for a daemon started with `-o host.socket=<path>`:

    notmmtui --socket <path>

Colours: `--theme powerlines` (the default: magenta and electric cyan on
black), `--theme plain` (the terminal's own), or `--theme <file>` with
`role = #rrggbb` lines over powerlines; roles are bg fg cyan magenta
violet orange green dim.

    git clone --recursive https://github.com/rakwahye/mediamaster-notmmtui
    make && make test
    sudo make install          # /usr/local/bin/notmmtui; PREFIX=, DESTDIR= as usual

Or let `mm-update` keep it current with the daemon: `mm-update add
~/path/to/mediamaster-notmmtui` once, then `mm-update`. `make uninstall`
removes it.

Needs a C11 compiler, pkg-config and Notcurses 3 (`notcurses-core` and its
headers; Arch: `notcurses`, or a source install under /usr/local, which is
looked for too). `make test` builds the pinned daemon and runs this
client against it with a null sink, in a pty driven by python3.
