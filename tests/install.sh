#!/bin/sh
# SPDX-License-Identifier: LGPL-2.1-or-later
# make install and make uninstall into a scratch root, with DESTDIR and
# PREFIX as a packager or mm-update would give them.
# Usage: tests/install.sh <make> [SAN]
set -eu
MAKE=${1:-make}
SAN=${2:-}
T=$(mktemp -d /tmp/notmmtui-install.XXXXXX)
trap 'rm -rf "$T"' EXIT INT TERM
fail() { echo "FAIL install: $*" >&2; exit 1; }
m() { $MAKE --no-print-directory SAN="$SAN" "$@" >/dev/null; }

m install DESTDIR="$T" PREFIX=/usr || fail "make install"
[ -x "$T/usr/bin/notmmtui" ] || fail "no $T/usr/bin/notmmtui"
[ "$(stat -c %a "$T/usr/bin/notmmtui")" = 755 ] || fail "mode $(stat -c %a "$T/usr/bin/notmmtui")"
[ "$(find "$T" -type f | wc -l)" = 1 ] || fail "more than the binary: $(find "$T" -type f)"
"$T/usr/bin/notmmtui" --help | grep -q '^usage: notmmtui' || fail "the installed binary does not run"
m install DESTDIR="$T" BINDIR=/opt/bin || fail "make install BINDIR"
[ -x "$T/opt/bin/notmmtui" ] || fail "BINDIR not honoured"
m uninstall DESTDIR="$T" PREFIX=/usr || fail "make uninstall"
[ ! -e "$T/usr/bin/notmmtui" ] || fail "make uninstall left it"
m uninstall DESTDIR="$T" PREFIX=/usr || fail "make uninstall twice"
echo "install: all checks passed"
