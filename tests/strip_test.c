/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* The strip's arithmetic: the playhead, the sky, the ink. */
#include "strip.h"
#include "check.h"
#include <string.h>

static int bar(void)
{
	CHECK(strip_bar(100, 0, 100) == 0, "start");
	CHECK(strip_bar(100, 50, 100) == 400, "half way: 50 cells, in eighths");
	CHECK(strip_bar(60, 15, 30) == 240, "half of 60");
	CHECK(strip_bar(10, 1, 80) == 1, "an eighth of a cell");
	CHECK(strip_bar(100, 200, 100) == 800 && strip_bar(100, -5, 100) == 0, "clamped");
	CHECK(strip_bar(100, 10, 0) == -1, "live: none");
	CHECK(strip_bar(0, 10, 100) == -1, "no columns: none");
	return 0;
}

static int sky(void)
{
	const struct theme *t = &theme_powerlines;
	CHECK(strip_sky(t, 0) == (uint32_t)t->c[TH_ORANGE], "orange at the left");
	CHECK(strip_sky(t, 0.18) == (uint32_t)t->c[TH_MAGENTA], "then magenta");
	CHECK(strip_sky(t, 0.55) == (uint32_t)t->c[TH_VIOLET], "then violet");
	CHECK(strip_sky(t, 1) == (uint32_t)t->c[TH_CYAN], "cyan at the right");
	return 0;
}

static int ink(void)
{
	const struct theme *t = &theme_powerlines;
	CHECK(strip_ink(t, 0x101020) == (uint32_t)t->c[TH_FG], "light ink on dark");
	CHECK(strip_ink(t, 0xf0f0ff) == (uint32_t)t->c[TH_BG], "dark ink on bright");
	CHECK(strip_ink(t, 0x9e1b84) == (uint32_t)t->c[TH_FG], "light ink on lit magenta");
	return 0;
}

static int plasma(void)
{
	const struct theme *t = &theme_powerlines;
	CHECK(strip_plasma(t, 10, 0, 1.0) == strip_plasma(t, 10, 0, 1.0), "the same place and time, the same colour");
	int moved = 0, varied = 0;
	for (int x = 0; x < 40; x++) {
		moved += strip_plasma(t, x, 0, 1.0) != strip_plasma(t, x, 0, 1.5);
		varied += strip_plasma(t, x, 0, 1.0) != strip_plasma(t, x + 1, 0, 1.0);
	}
	CHECK(moved > 30, "it moves with time");
	CHECK(varied > 30, "and varies across the row");
	CHECK(strip_look_find("plasma") == STRIP_PLASMA && strip_look_find("x") == -1 &&
	      !strcmp(strip_look_name(STRIP_POWERLINE), "powerline"), "look names");
	CHECK(strip_look_moves(STRIP_PLASMA) && !strip_look_moves(STRIP_POWERLINE), "plasma moves, powerline not");
	return 0;
}

static int decode(void)
{
	CHECK(!strip_settled(0, 0) && strip_settled(0, 0.2), "the first letter settles after a moment");
	CHECK(!strip_settled(10, 0.2) && strip_settled(10, 0.49), "later ones later");
	for (size_t i = 0; i < 60; i++)
		CHECK(strip_settled(i, strip_decode_time(i + 1)), "all settled by strip_decode_time");
	CHECK(strip_decode_time(40) < 1.6, "a long line settles within a second and a half");

	struct strip_model m;
	memset(&m, 0, sizeof(m));
	m.daemon = 1;
	m.decode = STRIP_SETTLED;
	m.look = STRIP_POWERLINE;
	m.st.state = TUI_PLAYING;
	CHECK(!strip_needs_frames(&m), "powerline, settled, playing: no frames");
	m.decode = 0.1;
	CHECK(strip_needs_frames(&m), "decoding: frames");
	m.decode = STRIP_SETTLED;
	m.st.state = TUI_PAUSED;
	CHECK(strip_needs_frames(&m), "paused: breathing, frames");
	m.st.state = TUI_PLAYING;
	m.look = STRIP_PLASMA;
	CHECK(strip_needs_frames(&m), "plasma playing: frames");
	m.st.state = TUI_STOPPED;
	CHECK(!strip_needs_frames(&m), "stopped: none");
	m.st.state = TUI_PLAYING;
	m.daemon = 0;
	CHECK(!strip_needs_frames(&m), "no daemon: none");
	return 0;
}

static int meter(void)
{
	CHECK(strip_meter_eighths(255) == 8, "full scale is a full cell");
	CHECK(strip_meter_eighths(0x80) == 7, "-6 dB: 7/8");
	CHECK(strip_meter_eighths(0x40) == 6, "-12 dB: 6/8");
	CHECK(strip_meter_eighths(0x33) == 6, "a tone of 0.2, -14 dB: 6/8");
	CHECK(strip_meter_eighths(1) == 0, "-48 dB: nothing");
	CHECK(strip_meter_eighths(0) == 0, "silence: nothing");
	for (int p = 1; p < 255; p++)
		CHECK(strip_meter_eighths((uint8_t)p) <= strip_meter_eighths((uint8_t)(p + 1)), "never falls as the peak rises");
	return 0;
}

int main(void)
{
	RUN(bar);
	RUN(sky);
	RUN(ink);
	RUN(plasma);
	RUN(decode);
	RUN(meter);
	CHECK_PASS("test-strip");
}
