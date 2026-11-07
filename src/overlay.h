/* SPDX-License-Identifier: LGPL-2.1-or-later */
/** @file overlay.h
 * @brief The queue, over the strip.
 *
 * Tab opens it: a pane of glass over the strip's rows, as notcurses-demo
 * draws its HUD. Its cells are empty, fg and bg the theme's background
 * (the veil the stage's words are on) blended over the look, so the look goes on under it with its own glyphs,
 * dimly. A cell with a word in it is solid, in the colour the glass is
 * over black, so a bright pixel cannot come up behind a letter, and where
 * the look is dark the words and the glass are one pane. The selected row
 * is violet glass the same way. Under it the strip draws its look alone,
 * without its words, so only the queue's are on the glass. One row
 * shows the item selected; from four rows a header says how many and the
 * keys, and the items fill the rest:
 *
 *     queue 3/12                     j k move · ⏎ play · a add · d remove
 *       2   Tab Title — Osc                                         0:03
 *     ▶ 3   Time — Pink Floyd                                       6:53
 *       4   sine:220:30
 *
 * The item playing has ▶; the one selected is on violet.
 *
 *     j k ↑ ↓  select          g G    first, last
 *     ⏎        play it         d      remove it
 *     a        add: a line to type a path or a URI into
 *
 * The add: line knows directories: a path is from your home (~ is it,
 * and an empty line is it), and while you type the rows above it list
 * what there starts with what you typed, directories first. Tab completes
 * as readline does (as far as all the matches agree; a directory gets its
 * /); ⏎ on a directory goes into it, on a file adds it (by its whole path,
 * as the daemon's directory is not yours), on one match takes it; a URI
 * (scheme:...) is added as typed. ctrl+u clears the line, ctrl+w takes
 * back a word or a directory, backspace a letter, esc leaves it. Keys
 * with modifiers type what they say (a ~ behind ctrl, say), but for
 * ctrl or alt with a letter or a digit.
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
	char path[4096];       /* the word an added path lives in, whole */
	char home[1024];       /* where a path starts from */
	char **match;          /* what the add: line's path could go on to: names, a directory's with / */
	size_t nmatch, more;   /* how many, and how many more there were */
	int uri;               /* the add: line holds a URI, not a path */
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

/** `in` as the add: line reads it, with `home`: a path (into `out`,
 * whole: ~ and a relative path from home) @returns 1; a URI (scheme: and
 * on, as typed) @returns 0. */
int overlay_expand(const char *in, const char *home, char *out, size_t n);

/** Look again at what the add: line's path could go on to. */
void overlay_scan(struct overlay *o);

/** The first item to show so `sel` is in view in `rows` rows, moving
 * `top` no more than it must. */
size_t overlay_top(size_t sel, size_t top, size_t n, int rows);

/** Draw it, `rows` x `cols` (closed: nothing). `msg`, when set, is shown
 * where the keys are (a command's error). */
void overlay_draw(struct overlay *o, const struct theme *t, const char *msg, unsigned rows, unsigned cols);

#endif
