<!-- SPDX-License-Identifier: LGPL-2.1-or-later -->
# notmmtui

A strip of a player for [mediamaster](https://github.com/rakwahye/mediamaster),
one to a few rows tall, drawn with Notcurses. It is a client like `mmctl`:
everything it shows comes from the daemon's socket.

Early days: `notmmtui --version` asks the daemon its version, and that is all.

    git clone --recursive https://github.com/rakwahye/mediamaster-notmmtui
    make && make test

Needs a C11 compiler. `make test` builds the pinned daemon and runs this
client against it, with a null sink.
