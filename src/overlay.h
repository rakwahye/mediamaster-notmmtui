/* SPDX-License-Identifier: LGPL-2.1-or-later */
/** @file overlay.h
 * @brief The queue, over the strip.
 *
 * Tab opens it: a plane over the strip's rows. Rows with words are on the
 * theme's background; the rows under the list Notcurses blends over the
 * look, so it goes on underneath, dimmed. (Words on a blend would have a
 * bright pixel of the look come up behind a letter at half its light.) One row
 * shows the item selected; from four rows a header says how many and the
 * keys, and the items fill the rest:
 *
 *     queue 3/12                     j k move · ⏎ play · a add · d remove
 *       2   Tab Title — Osc                                         0:03
 *     ▶ 3   Time — Pink Floyd                                       6:53
 *       4   sine:220:30
 *
 * The item playing has ▶; the one selected a bar of violet behind it.
 *
 *     j k ↑ ↓  select          g G    first, last
 *     ⏎        play it         d      remove it
 *     a        add: a line to type a URI into, ⏎ adds it, esc leaves it
 *     tab esc q  close
 *
 * Any other key is the strip's (space, n, p, seek, volume). The queue is
 * `queue list -l`, read again when it or the track changes. */

#ifndef TUI_OVERLAY_H
#define TUI_OVERLAY_H

#include "reply.h"
#include "theme.h"
#include <stddef.h>
#include <stdint.h>

struct ncplane;
struct ncinput;

#define OVERLAY_INPUT 256
#define OVERLAY_ARGV  4   /* a command's words and its NULL */

struct overlay {
	struct ncplane *n;
	int open;
	struct tui_list q;     /* the queue as last read */
	size_t sel, top;       /* the item selected, the first one shown */
	int adding;            /* the add: line is up */
	char input[OVERLAY_INPUT];
	char pos[24];          /* the word a command's position lives in */
};

enum overlay_did {
	OVERLAY_NOT_MINE = 0,  /* not an overlay key: the strip's */
	OVERLAY_TAKEN,         /* done: draw it again */
	OVERLAY_COMMAND,       /* run argv, then read the queue again */
	OVERLAY_OPENED,        /* read the queue */
	OVERLAY_CLOSED,
};

/** A closed overlay over `parent`'s rows (a plane on top of the strip's).
 * @returns NULL when out of memory. */
struct overlay *overlay_new(struct ncplane *parent);
void overlay_free(struct overlay *o);

/** Key `id` (with `ni`) for the overlay: Tab opens it from closed, and
 * nothing else does. A command fills argv (NULL-terminated, words in
 * `o` or constants: OVERLAY_ARGV slots). */
enum overlay_did overlay_key(struct overlay *o, uint32_t id, const struct ncinput *ni, char **argv);

/** The queue as `queue list -l` gave it: the selection stays where it was
 * (held to the list), and on first reading after opening goes to the item
 * playing. */
void overlay_set(struct overlay *o, const char *reply, int first);

/** The first item to show so `sel` is in view in `rows` rows, moving
 * `top` no more than it must. */
size_t overlay_top(size_t sel, size_t top, size_t n, int rows);

/** Draw it, `rows` x `cols` (closed: nothing). `msg`, when set, is shown
 * where the keys are (a command's error). */
void overlay_draw(struct overlay *o, const struct theme *t, const char *msg, unsigned rows, unsigned cols);

#endif
