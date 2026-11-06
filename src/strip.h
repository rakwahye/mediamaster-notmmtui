/* SPDX-License-Identifier: LGPL-2.1-or-later */
/** @file strip.h
 * @brief Drawing the strip: what plays, on one row.
 *
 * The strip is as tall and wide as the terminal it is given; a pane one
 * to a few rows high is what it is for. Everything is on the top row:
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
 *
 * A theme with no colours of its own (plain) is drawn in the plain look. */

#ifndef TUI_STRIP_H
#define TUI_STRIP_H

#include "reply.h"
#include "theme.h"
#include <stddef.h>
#include <stdint.h>

struct ncplane;

enum strip_look { STRIP_PLAIN, STRIP_POWERLINE };

/** What the strip shows; the main loop fills it from the daemon. */
struct strip_model {
	int daemon;            /* 0: nothing answers on the socket */
	struct tui_status st;  /* valid when daemon */
	char msg[160];         /* shown instead of the row while set: a command's error */
	double pos;            /* position to show, whole seconds (counted on between reads) */
	int bar;               /* playhead in eighths of a cell (strip_bar), -1 for none */
	int look;              /* enum strip_look */
};

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

/** Text colour that reads on `under`: the theme's bg on a bright cell,
 * its fg otherwise. */
uint32_t strip_ink(const struct theme *t, uint32_t under);

/** "m:ss", or "h:mm:ss" from an hour. */
void strip_clock(char *buf, size_t cap, double s);

#endif
