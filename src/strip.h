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
#include <stddef.h>

struct ncplane;

/** What the strip shows; the main loop fills it from the daemon. */
struct strip_model {
	int daemon;            /* 0: nothing answers on the socket */
	struct tui_status st;  /* valid when daemon */
};

/** Redraw `n` from `m`. */
void strip_draw(struct ncplane *n, const struct strip_model *m);

/** "m:ss", or "h:mm:ss" from an hour. */
void strip_clock(char *buf, size_t cap, double s);

#endif
