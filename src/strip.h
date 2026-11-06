/* SPDX-License-Identifier: LGPL-2.1-or-later */
/** @file strip.h
 * @brief Drawing the strip: what plays, on one row.
 *
 * The strip is as tall and wide as the terminal it is given; a pane one
 * to a few rows high is what it is for. For now everything is on the
 * top row:
 *
 *     ▶ Title — Artist                         1:23 / 6:53
 *
 * The time stays; the title is cut to what is left. */

#ifndef TUI_STRIP_H
#define TUI_STRIP_H

#include "reply.h"
#include "theme.h"
#include <stddef.h>

struct ncplane;

/** What the strip shows; the main loop fills it from the daemon. */
struct strip_model {
	int daemon;            /* 0: nothing answers on the socket */
	struct tui_status st;  /* valid when daemon */
	char msg[160];         /* shown instead of the row while set: a command's error */
	double pos;            /* position to show, whole seconds (counted on between reads) */
};

/** Redraw `n` from `m`, in theme `t`. */
void strip_draw(struct ncplane *n, const struct strip_model *m, const struct theme *t);

/** "m:ss", or "h:mm:ss" from an hour. */
void strip_clock(char *buf, size_t cap, double s);

#endif
