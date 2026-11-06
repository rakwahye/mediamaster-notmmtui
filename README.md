<!-- SPDX-License-Identifier: LGPL-2.1-or-later -->
# notmmtui

A strip of a player for [mediamaster](https://github.com/rakwahye/mediamaster),
one to a few rows tall, drawn with Notcurses. It is a client like `mmctl`:
everything it shows comes from the daemon's socket.

Early days: one row, plain: what plays, its time, and `q` to quit. Run it
in a short terminal or pane; it fills what it is given.

    git clone --recursive https://github.com/rakwahye/mediamaster-notmmtui
    make && make test

Needs a C11 compiler and Notcurses 3 (`notcurses-core` and its headers;
Arch: `notcurses`). `make test` builds the pinned daemon and runs this
client against it with a null sink, in a pty driven by python3.
