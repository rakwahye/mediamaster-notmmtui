/* SPDX-License-Identifier: LGPL-2.1-or-later */
/** @file strip.h
 * @brief Drawing the strip: what plays, on a row or two.
 *
 * The strip is as tall and wide as the terminal it is given; a pane one
 * to a few rows high is what it is for. In the one-row looks everything
 * is on the top row:
 *
 *     ▶ Title — Artist                         1:23 / 6:53
 *
 * The time stays; the title is cut to what is left. Two planes: the
 * base draws the ground, the text sits above it with no background of
 * its own. Looks:
 *
 *   plain      the theme's background, text in its roles.
 *   powerline  the row is the progress bar: the sky gradient (orange,
 *              magenta, violet, cyan) lit up to the playhead, dark after
 *              it, the playhead to an eighth of a cell with a glow on it.
 *              Text on the lit part is inked to read against it.
 *   plasma     plasma in the sky's colours, two pixels to a row (upper
 *              half blocks), moving behind what has played; faint dots
 *              ahead; the whole row when live. Text sits on a veil the
 *              plasma shows through (NCALPHA_BLEND). It animates: the
 *              caller redraws every STRIP_FRAME_MS while it plays.
 *
 *   scope      two rows or more: an oscilloscope drawn from the bands, in
 *              octants (4x2 dots a cell; quadrants or half blocks where
 *              the terminal has none), each band a wave across it, bass
 *              long and treble fine, as tall as the level, travelling,
 *              flaring white on a beat; old traces glow on faintly and
 *              sink to violet (phosphor). The
 *              words take the left, on two rows, and a meter of two
 *              cells by two rows (16 steps a channel) stands between:
 *
 *     ▶ Title           1:23 ▅▆  ∿∿∿ the scope ∿∿∿
 *       Artist          6:53 ██  ∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿
 *
 *              Paused, the trace stays where it was and breathes.
 *
 *   road       three rows or more: a spectrum over a wet road. The bands
 *              stand as bars across the whole width, 8 steps a row on
 *              every row but the last, in the sky's colours, bright up to
 *              the playhead and dim after it, peaks held a moment over
 *              them and falling. The last row is the road: the bars'
 *              reflection, shorter and darker, the water a little lit
 *              where it has played, ripples drifting through it, and the
 *              words of the one-row looks on it:
 *
 *     ▁▃▅▇▆▄▂▁ ▂▃▄▃▂▁   ▁▂▁
 *     ████████▇█████▆▄▂▃▅██▆▃
 *     ▶ Title — Artist     ▅▆ 1:23 / 6:53
 *
 * A theme with no colours of its own (plain) is drawn in the plain look,
 * and a look taller than the strip (strip_look_rows) in powerline. With
 * no look asked for, the height picks one (strip_look_for).
 *
 * In every look, on a track change the title and artist decode in: each
 * letter is noise, in cyan, until its moment, then settles, left to
 * right (strip_settled). Paused, the strip breathes: the lit part, or in
 * plain the ‖, fades slowly up and down. Both need frames while they
 * last (strip_needs_frames).
 *
 * With levels from the daemon (watch levels), a two-cell meter stands
 * before the time, left channel then right, each an eighth block whose
 * height is its peak on a 48 dB scale (strip_meter_eighths):
 *
 *     ▶ Title — Artist                      ▆▃ 1:23 / 6:53
 *
 * The looks move with the levels too (react.h): plasma runs faster as it
 * gets louder and brighter with the bass, and flashes on a beat;
 * powerline's glow widens with the bass and flares on a beat, and its lit
 * part brightens with the level. With no levels they look as before. */

#ifndef TUI_STRIP_H
#define TUI_STRIP_H

#include "react.h"
#include "reply.h"
#include "theme.h"
#include <stddef.h>
#include <stdint.h>

struct ncplane;

enum strip_look { STRIP_POWERLINE, STRIP_PLASMA, STRIP_SCOPE, STRIP_ROAD, STRIP_PLAIN, STRIP_LOOKS };

#define STRIP_FRAME_MS 66 /* about 15 frames a second, for a look that moves */

/** "powerline", "plasma", "scope", "road", "plain". */
const char *strip_look_name(int look);

/** The look named `name`, or -1. */
int strip_look_find(const char *name);

/** Whether `look` moves while playing (and needs frames, not ticks). */
int strip_look_moves(int look);

/** The rows `look` needs: 2 for scope, 3 for road, 1 for the rest. */
int strip_look_rows(int look);

/** The look for a strip `rows` high when none was asked for. */
int strip_look_for(unsigned rows);

/** The look after `look` that fits `rows`, for the key that switches. */
int strip_look_next(int look, unsigned rows);

/** What the strip shows; the main loop fills it from the daemon. */
struct strip_model {
	int daemon;            /* 0: nothing answers on the socket */
	struct tui_status st;  /* valid when daemon */
	char msg[160];         /* shown instead of the row while set: a command's error */
	char refused[200];     /* the daemon is not one this was built for, and why */
	double pos;            /* position to show, whole seconds (counted on between reads) */
	int bar;               /* playhead in eighths of a cell (strip_bar), -1 for none */
	int look;              /* enum strip_look */
	double anim;           /* s of animation: runs while playing, stands still paused */
	double decode;         /* s since the track changed; STRIP_SETTLED once all has settled */
	double breath;         /* 0..1 while paused: how far up the fade is */
	int levels;            /* lv holds what the daemon last said: show the meter */
	struct tui_levels lv;
	struct react rx;       /* envelopes of the levels, for the looks to move with */
};

#define STRIP_SETTLED 1e9

/** Whether letter i of a decoding line has settled `age` s after the
 * track changed: at 0.13 s plus 35 ms a letter. */
int strip_settled(size_t i, double age);

/** s until every letter of a line of `n` letters has settled. */
double strip_decode_time(size_t n);

/** Whether `m` is mid-change (decoding, breathing, a moving look) and
 * wants a frame every STRIP_FRAME_MS. */
int strip_needs_frames(const struct strip_model *m);

struct strip;

/** The strip's planes over `base` (which it draws the ground on).
 * @returns NULL if a plane cannot be made. */
struct strip *strip_new(struct ncplane *base);
void strip_free(struct strip *s);

/** Redraw from `m`, in theme `t`. */
void strip_draw(struct strip *s, const struct strip_model *m, const struct theme *t);

/** The playhead for `pos` of `duration` s across `cols` cells, in
 * eighths of a cell; -1 when there is no duration (live). */
int strip_bar(unsigned cols, double pos, double duration);

/** The sky gradient at t (0 left .. 1 right): orange, magenta, violet,
 * cyan from the theme. */
uint32_t strip_sky(const struct theme *t, double x);

/** The plasma at (x, y) pixels and time t s: a colour of the sky. */
uint32_t strip_plasma(const struct theme *t, double x, double y, double time);

/** Text colour that reads on `under`: the theme's bg on a bright cell,
 * its fg otherwise. */
uint32_t strip_ink(const struct theme *t, uint32_t under);

/** A peak (0..255 for 0..1 linear) as eighths of a cell, 0..8: 0 at
 * -48 dBFS or below, 8 at 0 dBFS. */
int strip_meter_eighths(uint8_t peak);

/** The same in `steps` (16: two cells of eighths). */
int strip_meter_steps(uint8_t peak, int steps);

/** The scope's trace at u (0 left .. 1 right) and time t s, -1..1 before
 * the level: band k (envelope band[k], 0..1) is a wave of 1 + 0.75k
 * cycles across, travelling right at 0.35 + 0.09k cycles a second, the
 * sum taken over the bands' RMS weight and softly clipped. 0 when all
 * rest. */
double strip_scope_wave(const float *band, double u, double t);

/** The road's bar at u (0 left .. 1 right), 0..1: the bands spread
 * evenly over the width, each column between its two nearest. */
double strip_road_bar(const float *band, double u);

/** The road's peaks over `n` bars (in steps), `dt` s on: a peak under its
 * bar jumps to it and holds 0.4 s, then falls at 24 steps a second, never
 * below its bar. `hold` is each one's time left to hold. */
void strip_road_peaks(float *peak, float *hold, const float *bar, int n, double dt);

/** "m:ss", or "h:mm:ss" from an hour. */
void strip_clock(char *buf, size_t cap, double s);

#endif
