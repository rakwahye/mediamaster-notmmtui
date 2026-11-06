#!/bin/sh
# SPDX-License-Identifier: LGPL-2.1-or-later
# End-to-end: the pinned daemon with a null sink, temp config and socket,
# and notmmtui talking to it.
# Usage: tests/smoke.sh <mediamaster-build-dir> <notmmtui-build-dir>
set -eu

MB=${1:-mediamaster/build}
B=${2:-build}
T=$(mktemp -d /tmp/notmmtui-smoke.XXXXXX)
PID=
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; rm -rf "$T"; }
trap cleanup EXIT INT TERM

fail() { echo "FAIL smoke: $*" >&2; [ -f "$T/log" ] && sed 's/^/  log| /' "$T/log" >&2; exit 1; }

export MM_SOCK="$T/mm.sock"
MMCTL="$MB/mmctl"
TUI="$B/notmmtui"

start() {
	HOME="$T" XDG_CACHE_HOME="$T/cache" "$MB/mediamaster" -c "$T/config" -o "host.socket=$MM_SOCK" "$@" 2>>"$T/log" &
	PID=$!
	i=0
	until "$MMCTL" version >/dev/null 2>&1; do
		i=$((i + 1)); [ $i -lt 50 ] || fail "daemon did not answer"
		kill -0 "$PID" 2>/dev/null || fail "daemon exited during startup"
		sleep 0.1
	done
}

stop() {
	"$MMCTL" quit >/dev/null || fail "quit"
	wait "$PID" || fail "daemon exit status $?"
	PID=
}

printf 'host.plugin_dir = %s/plugins\nhost.sink = null\n' "$MB" >"$T/config"

# --- no daemon
out=$("$TUI" --version) && fail "--version with no daemon should exit 1"
echo "$out" | grep -qx "daemon: not running" || fail "no daemon: $out"
if "$TUI" --nosuch 2>/dev/null; then fail "an unknown option should fail"; fi

# --- the daemon's version, through our own client
start
out=$("$TUI" --version) || fail "--version: $out"
echo "$out" | grep -qx "notmmtui [0-9.]*" || fail "own version: $out"
echo "$out" | grep -qx "daemon: $("$MMCTL" version | sed 's/^ok //')" || fail "daemon version: $out"
stop

echo "smoke: all checks passed"
