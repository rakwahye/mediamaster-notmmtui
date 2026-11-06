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
echo "$out" | grep -qx "daemon: not running ($MM_SOCK)" || fail "no daemon, and where it looked: $out"
if "$TUI" --nosuch 2>/dev/null; then fail "an unknown option should fail"; fi

# --- the daemon's version, through our own client
start
out=$("$TUI" --version) || fail "--version: $out"
echo "$out" | grep -qx "notmmtui [0-9.]*" || fail "own version: $out"
echo "$out" | grep -qx "daemon: $("$MMCTL" version | sed 's/^ok //')" || fail "daemon version: $out"
# --socket, over MM_SOCK: the daemon on another socket, found only by the flag
out=$(MM_SOCK="$T/nowhere.sock" "$TUI" --socket "$MM_SOCK" --version) || fail "--socket: $out"
echo "$out" | grep -q "^daemon: mediamaster" || fail "--socket: $out"
out=$("$TUI" --socket "$T/nowhere.sock" --version) && fail "--socket to nothing should exit 1"
echo "$out" | grep -qx "daemon: not running ($T/nowhere.sock)" || fail "--socket to nothing: $out"
if "$TUI" --socket 2>/dev/null; then fail "--socket without a path should fail"; fi
if "$TUI" --socket "" --version 2>/dev/null; then fail "--socket with an empty path should fail"; fi
stop

# --- following: with watch, status is read when a line says so, not each
# tick; without it (an older daemon), each tick
TTY="python3 -I tests/tty.py"
fake() { # watch|nowatch
	rm -f "$T/fake.sock.log"
	MM_SOCK="$T/fake.sock" python3 -I tests/fakemm.py "$T/fake.sock" tests/fixtures "$1" &
	FAKE=$!
	i=0
	until [ -S "$T/fake.sock" ]; do i=$((i + 1)); [ $i -lt 50 ] || fail "fake daemon"; sleep 0.05; done
}
fake watch
MM_SOCK="$T/fake.sock" $TTY 1 60 "$TUI" 'wait:Tab Title' 'wait:▶' 'wait:‖' 'sleep:1.5' key:q ||
	{ kill $FAKE; fail "strip with watch: a state line shows the pause"; }
kill $FAKE; wait $FAKE 2>/dev/null || true
grep -qx watch "$T/fake.sock.log" || fail "strip did not watch"
n=$(grep -cx status "$T/fake.sock.log")
[ "$n" -le 4 ] || fail "with watch, status read $n times in ~3 s"
fake nowatch
MM_SOCK="$T/fake.sock" $TTY 1 60 "$TUI" 'wait:Tab Title' 'sleep:1.5' key:q ||
	{ kill $FAKE; fail "strip without watch"; }
kill $FAKE; wait $FAKE 2>/dev/null || true
n=$(grep -cx status "$T/fake.sock.log")
[ "$n" -ge 5 ] || fail "without watch, status read only $n times in ~1.5 s"

# --- the strip, in a pty. It waits for the daemon, then follows it.
$TTY 1 60 "$TUI" 'wait:not running' 'wait:stopped' 'wait:sine:440:30' 'wait:0:30' 'wait:‖' key:q &
TP=$!
sleep 0.5
start
sleep 0.5
"$MMCTL" play sine:440:30 >/dev/null
sleep 0.5
"$MMCTL" pause >/dev/null
wait $TP || fail "strip: waiting for the daemon, then play and pause"

# narrow: the time stays, the title is cut; then wider again
"$MMCTL" resume >/dev/null
$TTY 1 60 "$TUI" 'wait:sine:440:30' 'size:1,20' 'wait:sine:…' 'size:1,8' 'gone:sine' \
	'size:1,60' 'wait:sine:440:30' key:q || fail "strip: resize"

if command -v ffmpeg >/dev/null; then # tags: title and artist
	ffmpeg -v error -f lavfi -i sine=f=440:d=30 -ac 2 -metadata title="Tone	One" -metadata artist="Osc" \
	       "$T/tone.flac" -y || fail "fixture"
	"$MMCTL" play "$T/tone.flac" >/dev/null
	$TTY 1 60 "$TUI" 'wait:Tone One — Osc' key:q || fail "strip: title and artist, a tab in the title shown as a space"
fi
# keys: toggle twice, volume, seek; the daemon says what happened
"$MMCTL" play sine:440:30 >/dev/null
"$MMCTL" vol 50 >/dev/null
$TTY 1 60 "$TUI" 'wait:sine:440:30' 'key: ' 'wait:‖' 'key: ' 'wait:▶' 'key:+' 'key:[C' \
	'sleep:0.3' key:q || fail "strip: keys"
st=$("$MMCTL" status)
echo "$st" | grep -qx 'state: playing' || fail "keys: toggled twice is playing: $st"
echo "$st" | grep -qx 'volume: 55' || fail "keys: + is volume +5: $st"
pos=$(echo "$st" | sed -n 's/^position: //p')
[ "${pos%%.*}" -ge 5 ] || fail "keys: right arrow seeks +5 s, at $pos"
$TTY 1 60 "$TUI" 'wait:sine:440:30' 'key:s' 'wait:stopped' 'key:n' 'wait:next:' key:q || fail "strip: stop, and an error shown"
"$MMCTL" play sine:440 >/dev/null # live: no bar; a seek is refused, and the strip says so
$TTY 1 60 "$TUI" 'wait:live' 'color:live=c6ff3a' key:q || fail "powerline: live"
$TTY 1 60 "$TUI" 'wait:sine:440' 'key:[C' 'wait:seek:' 'gone:seek:' key:q || fail "strip: a refused seek, shown for a while"
# themes: powerlines by default, plain, a file over powerlines
"$MMCTL" play sine:440:30 >/dev/null
$TTY 1 60 "$TUI --look plain" 'wait:sine:440:30' 'color:▶=ff2bd6' 'color:sine:440:30=e6f0ff' \
	'color:0:0=19d3ff' 'color:/ 0:30=3a3f5c' 'bgat:2=05060e' 'bgat:57=05060e' 'key: ' 'color:‖=ff7a1a' 'key: ' \
	key:q || fail "theme powerlines, plain look"

# powerline (the default look): the row is the bar, lit to the playhead
"$MMCTL" seek 15 >/dev/null
$TTY 1 60 "$TUI" 'wait:sine:440:30' 'brighter:2,57' 'brighter:28,32' 'color:sine:440:30=e6f0ff' \
	'color:/ 0:30=3a3f5c' key:q || fail "powerline: lit to half way, text inked on the lit part"
"$MMCTL" play sine:440:30 >/dev/null # the playhead passes under the title: it stays legible
$TTY 1 60 "$TUI" 'wait:sine:440:30' 'legible:3.5' key:q || fail "powerline: text over the playhead"
$TTY 1 60 "$TUI --theme plain" 'wait:sine:440:30' 'bgat:2=default' 'bgat:57=default' key:q ||
	fail "powerline with a theme of no colours is drawn plain"
if "$TUI" --look nosuch 2>/dev/null; then fail "an unknown look should fail"; fi
"$MMCTL" play sine:440:30 >/dev/null # from the start again for what follows
MM_SOCK="$T/nowhere.sock" $TTY 1 60 "$TUI --socket $MM_SOCK" 'wait:sine:440:30' key:q || fail "strip: --socket"
$TTY 1 60 "$TUI --theme plain" 'wait:sine:440:30' 'color:▶=default' 'color:sine:440:30=default' key:q ||
	fail "theme plain"
printf '# mine\nmagenta = #00ff00\n' >"$T/theme"
$TTY 1 60 "$TUI --look plain --theme $T/theme" 'wait:sine:440:30' 'color:▶=00ff00' 'color:0:0=19d3ff' key:q ||
	fail "theme file"
printf 'magenta = green\n' >"$T/theme"
out=$("$TUI" --theme "$T/theme" 2>&1) && fail "a bad theme file should fail"
echo "$out" | grep -q 'line 1' || fail "a bad theme says where: $out"

# the position counts on between reads; the daemon goes, comes back, is followed
"$MMCTL" play sine:440:30 >/dev/null
$TTY 1 60 "$TUI" 'wait:sine:440:30' 'wait:0:02 /' 'wait:not running' 'wait:sine:660:30' key:q &
TP=$!
sleep 3
stop
start
"$MMCTL" play sine:660:30 >/dev/null
wait $TP || fail "strip: counting, then a daemon restart"
"$MMCTL" stop >/dev/null
stop

echo "smoke: all checks passed"
