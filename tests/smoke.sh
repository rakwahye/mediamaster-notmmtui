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
out=$(timeout 10 "$TUI" 2>&1 </dev/null) && fail "the strip without a terminal should exit 1"
echo "$out" | grep -q "needs a terminal" || fail "no terminal: $out"

# --- the daemon's version, through our own client
start
out=$("$TUI" --version) || fail "--version: $out"
ABI=$(sed -n 's/^#define MM_ABI \([0-9]*\).*/\1/p' "$MB/../include/mm_plugin.h")
[ -n "$ABI" ] || fail "no MM_ABI in the host's header"
echo "$out" | grep -qx "notmmtui [0-9.]* (for mediamaster abi $ABI)" || fail "own version: $out"
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
fake() { # watch|nowatch [version reply]
	rm -f "$T/fake.sock.log" "$T/fake.sock" # so the wait below is for this one
	MM_SOCK="$T/fake.sock" python3 -I tests/fakemm.py "$T/fake.sock" tests/fixtures "$1" \
		"${2:-ok mediamaster 0.1.0 abi $ABI}" &
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
grep -qx 'watch levels' "$T/fake.sock.log" || fail "strip did not watch levels"
# levels: the meter shows L 80 (-6 dB, 7/8) and R 40 (-12 dB, 6/8); when the
# daemon hangs up on the levels watch, it comes back
fake watch-drop
MM_SOCK="$T/fake.sock" $TTY 1 60 "$TUI" 'wait:Tab Title' 'wait:▇▆' 'sleep:2.5' 'wait:▇▆' key:q ||
	{ kill $FAKE; fail "strip: the meter, and levels again after a hang-up"; }
kill $FAKE; wait $FAKE 2>/dev/null || true
n=$(grep -cx 'watch levels' "$T/fake.sock.log")
[ "$n" -ge 2 ] || fail "levels watched $n times across a hang-up"
fake nowatch
MM_SOCK="$T/fake.sock" $TTY 1 60 "$TUI" 'wait:Tab Title' 'sleep:1.5' key:q ||
	{ kill $FAKE; fail "strip without watch"; }
kill $FAKE; wait $FAKE 2>/dev/null || true
n=$(grep -cx status "$T/fake.sock.log")
[ "$n" -ge 5 ] || fail "without watch, status read only $n times in ~1.5 s"

# --- a daemon this was not built for is refused: on start, and while running
fake watch "ok mediamaster 0.9.0 abi $((ABI + 1))"
out=$(MM_SOCK="$T/fake.sock" timeout 10 "$TUI" 2>&1) && { kill $FAKE; fail "another abi on start should exit 1"; }
echo "$out" | grep -q "speaks abi $((ABI + 1)); this notmmtui was built for abi $ABI" || { kill $FAKE; fail "refusal: $out"; }
out=$(MM_SOCK="$T/fake.sock" timeout 10 "$TUI" --version) && { kill $FAKE; fail "--version to another abi should exit 1"; }
echo "$out" | grep -q "^refused: mediamaster 0.9.0 speaks abi" || { kill $FAKE; fail "--version refusal: $out"; }
kill $FAKE; wait $FAKE 2>/dev/null || true
fake watch "ok fake"
out=$(MM_SOCK="$T/fake.sock" timeout 10 "$TUI" 2>&1) && { kill $FAKE; fail "a daemon of another name should be refused"; }
echo "$out" | grep -q "not a mediamaster daemon" || { kill $FAKE; fail "refusal: $out"; }
kill $FAKE; wait $FAKE 2>/dev/null || true
grep -qx status "$T/fake.sock.log" && fail "status asked of a refused daemon"
# running: waits for a daemon; one of another abi comes and is refused in the
# strip; it goes, the right one comes, and the strip follows it
rm -f "$T/fake.sock"
MM_SOCK="$T/fake.sock" $TTY 1 100 "$TUI" 'wait:not running' "touch:$T/r1" 'wait:built for abi' "touch:$T/r2" \
	'wait:Tab Title' key:q &
TP=$!
i=0; until [ -e "$T/r1" ]; do i=$((i + 1)); [ $i -lt 200 ] || fail "strip did not start"; sleep 0.05; done
fake watch "ok mediamaster 0.9.0 abi $((ABI + 1))"
i=0; until [ -e "$T/r2" ]; do i=$((i + 1)); [ $i -lt 200 ] || { kill $FAKE; fail "strip did not refuse"; }; sleep 0.05; done
grep -qx status "$T/fake.sock.log" && { kill $FAKE; fail "status asked of a refused daemon"; }
kill $FAKE; wait $FAKE 2>/dev/null || true
fake watch
wait $TP || { kill $FAKE; fail "strip: refuse, then follow the right daemon"; }
kill $FAKE; wait $FAKE 2>/dev/null || true

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
# plasma: moving behind what has played, still when paused; dots ahead; l cycles looks
"$MMCTL" seek 15 >/dev/null
$TTY 1 60 "$TUI --look plasma" 'wait:sine:440:30' 'moving:20' 'legible:1.5' 'color:·=3a3f5c' \
	'key: ' 'wait:‖' 'moving:20' 'key: ' 'moving:20' key:q || fail "plasma, and its pause breathes"
$TTY 1 60 "$TUI" 'wait:sine:440:30' 'brighter:2,57' 'key:l' 'wait:·' 'moving:20' 'key:l' 'gone:·' \
	'bgat:2=05060e' 'key:l' 'brighter:2,57' key:q || fail "l: powerline, plasma, plain, powerline"
# a track change decodes in: a later letter is cyan noise, then the title settles
$TTY 1 60 "$TUI" 'wait:sine:440:30' 'fgat:12=e6f0ff' "touch:$T/settled" 'fgat:12=19d3ff' \
	'wait:sine:660:30' 'fgat:12=e6f0ff' key:q &
TP=$!
i=0
until [ -e "$T/settled" ]; do i=$((i + 1)); [ $i -lt 200 ] || fail "strip never settled"; sleep 0.05; done
"$MMCTL" play sine:660:30 >/dev/null
wait $TP || fail "decode on a track change"
# a track change from late in a track: the new one starts at 0:00 (the
# daemon reports a new item's position at once since 290f26c)
"$MMCTL" play sine:440:400 >/dev/null
"$MMCTL" seek 300 >/dev/null
rm -f "$T/settled"
$TTY 1 60 "$TUI" 'wait:/ 6:40' "touch:$T/settled" 'wait:/ 6:23' 'soon:0:0' key:q &
TP=$!
i=0
until [ -e "$T/settled" ]; do i=$((i + 1)); [ $i -lt 200 ] || fail "strip never showed the first track"; sleep 0.05; done
"$MMCTL" play sine:550:383 >/dev/null
wait $TP || fail "after a track change, the position of the new track"
"$MMCTL" play sine:440:30 >/dev/null

# paused breathes: powerline's lit part, plain's ‖
"$MMCTL" seek 15 >/dev/null
"$MMCTL" pause >/dev/null
$TTY 1 60 "$TUI" 'wait:‖' 'moving:2' key:q || fail "powerline: a pause breathes"
$TTY 1 60 "$TUI --look plain" 'wait:‖' 'moving:0' 'bgat:2=05060e' key:q || fail "plain: the ‖ breathes"
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

# levels from the real daemon: a tone of 0.2 is -14 dB, 6/8 of the meter; the
# pause's silence empties it
"$MMCTL" play sine:440:30 >/dev/null
$TTY 1 60 "$TUI" 'wait:sine:440:30' 'wait:▆▆' 'key: ' 'wait:‖' 'gone:▆▆' 'key: ' 'wait:▆▆' \
	key:q || fail "strip: the meter, from the daemon's levels"

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
