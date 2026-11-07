/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* The strip's arithmetic: the playhead, the sky, the ink. */
#include "strip.h"
#include "check.h"
#include <math.h>
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
	CHECK(strip_meter_steps(255, 16) == 16, "two cells: full scale is both full");
	CHECK(strip_meter_steps(0x33, 16) == 11, "-14 dB: the lower cell full, 3/8 of the upper");
	return 0;
}

static int looks(void)
{
	CHECK(strip_look_for(1) == STRIP_POWERLINE, "one row: powerline");
	CHECK(strip_look_for(2) == STRIP_SCOPE, "two rows: scope");
	CHECK(strip_look_for(3) == STRIP_ROAD, "three rows: road");
	CHECK(strip_look_for(4) == STRIP_HORIZON && strip_look_for(9) == STRIP_HORIZON, "four rows or more: horizon");
	CHECK(strip_look_next(STRIP_PLASMA, 1) == STRIP_PLAIN, "one row: scope is skipped");
	CHECK(strip_look_next(STRIP_PLAIN, 1) == STRIP_POWERLINE, "and round again");
	CHECK(strip_look_next(STRIP_PLASMA, 2) == STRIP_SCOPE, "two rows: scope is one of them");
	CHECK(strip_look_next(STRIP_SCOPE, 2) == STRIP_PLAIN, "but not road");
	CHECK(strip_look_next(STRIP_SCOPE, 3) == STRIP_ROAD, "three rows: road too");
	CHECK(strip_look_find("road") == STRIP_ROAD && strip_look_moves(STRIP_ROAD), "road, and it moves");
	CHECK(strip_look_next(STRIP_ROAD, 3) == STRIP_PLAIN, "three rows: not horizon");
	CHECK(strip_look_next(STRIP_ROAD, 4) == STRIP_HORIZON, "four rows: horizon too");
	CHECK(strip_look_find("horizon") == STRIP_HORIZON && strip_look_moves(STRIP_HORIZON), "horizon, and it moves");
	CHECK(!strip_look_moves(STRIP_POWERLINE) && !strip_look_moves(STRIP_PLAIN), "powerline and plain do not");
	CHECK(strip_look_find("scope") == STRIP_SCOPE && strip_look_moves(STRIP_SCOPE), "scope, and it moves");
	return 0;
}

static int scope(void)
{
	float band[TUI_BANDS] = { 0 };
	CHECK(strip_scope_wave(band, 0.3, 1.0) == 0, "all at rest: a flat line");
	band[0] = 0.5f;
	CHECK(strip_scope_wave(band, 0.25, 0) > 0.95, "the lowest band alone: one whole cycle across, at its top a quarter in");
	CHECK(strip_scope_wave(band, 0.75, 0) < -0.95, "and its bottom three quarters in");
	CHECK(fabs(strip_scope_wave(band, 0.5, 0)) < 1e-6, "crossing in the middle");
	CHECK(strip_scope_wave(band, 0.25, 0.5) != strip_scope_wave(band, 0.25, 0), "it travels");
	for (int k = 0; k < TUI_BANDS; k++) band[k] = 1.0f;
	double lo = 0, hi = 0;
	for (int i = 0; i < 400; i++) {
		double v = strip_scope_wave(band, i / 400.0, 0.37);
		CHECK(v >= -1 && v <= 1, "every band at once: within the scope");
		lo = v < lo ? v : lo;
		hi = v > hi ? v : hi;
	}
	CHECK(hi - lo > 1.0, "and swinging wide rather than cancelling");
	return 0;
}

static int horizon(void)
{
	CHECK(strip_horizon_tie(0, 4, 0) == 0, "the first tie starts on the horizon");
	CHECK(fabs(strip_horizon_tie(1, 4, 0) - 0.25) < 1e-9, "the next a quarter of the way in");
	CHECK(strip_horizon_tie(0, 4, 1) > strip_horizon_tie(0, 4, 0), "they come in");
	double t = 0.25 / 0.35 + 0.1; /* the last tie (from 0.75) has passed your feet */
	CHECK(strip_horizon_tie(3, 4, t) < 0.1, "and once past, are back on the horizon");
	for (int i = 0; i < 4; i++) {
		double d = strip_horizon_tie(i, 4, 123.4);
		CHECK(d >= 0 && d < 1, "always between the horizon and your feet");
	}
	CHECK(strip_horizon_tie(0, 4, -1.0) >= 0, "even on a clock before zero");

	float band[TUI_BANDS] = { 0 };
	band[0] = 0.9f;
	band[TUI_BANDS - 1] = 0.3f;
	CHECK(fabs(strip_horizon_hill(band, 0) - 0.9) < 1e-6, "by the sun: the lowest band");
	CHECK(fabs(strip_horizon_hill(band, 1) - 0.3) < 1e-6, "at the far edge: the highest");
	CHECK(strip_horizon_hill(band, 5) == strip_horizon_hill(band, 1), "no farther than the edge");
	return 0;
}

static int road(void)
{
	float band[TUI_BANDS] = { 0 };
	band[0] = 1.0f;
	band[TUI_BANDS - 1] = 0.5f;
	CHECK(strip_road_bar(band, 0.0) == 1.0, "the left edge: the lowest band");
	CHECK(strip_road_bar(band, 1.0) == 0.5, "the right edge: the highest");
	double half = strip_road_bar(band, 1.0 / TUI_BANDS); /* between bands 0 and 1 */
	CHECK(half > 0.45 && half < 0.55, "between two bands, between their bars");

	float bar[2] = { 10, 0 }, peak[2] = { 0, 12 }, hold[2] = { 0, 0 };
	strip_road_peaks(peak, hold, bar, 2, 0.05);
	CHECK(peak[0] == 10 && hold[0] > 0.39f, "a bar over its peak lifts it, and it holds");
	CHECK(peak[1] < 12 && peak[1] > 10, "a peak with no hold left falls");
	bar[0] = 0;
	strip_road_peaks(peak, hold, bar, 2, 0.3);
	CHECK(peak[0] == 10, "held for a while");
	strip_road_peaks(peak, hold, bar, 2, 0.2); /* 0.1 s past the hold */
	CHECK(peak[0] < 10 && peak[0] > 6, "then falling, from where the hold ran out");
	strip_road_peaks(peak, hold, bar, 2, 5.0);
	CHECK(peak[0] == 0 && peak[1] == 0, "down to the bar and no further");
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
	RUN(looks);
	RUN(scope);
	RUN(road);
	RUN(horizon);
	CHECK_PASS("test-strip");
}
