/* SPDX-License-Identifier: LGPL-2.1-or-later */
/** @file reply.h
 * @brief The daemon's replies, read into structs.
 *
 * Every reader takes the reply text as tui_call left it and never fails
 * outright: what it does not understand it skips, and says so by its
 * return value. The screen keeps showing the last good state. */

#ifndef TUI_REPLY_H
#define TUI_REPLY_H

#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------ status */

enum tui_state { TUI_STOPPED = 0, TUI_PLAYING, TUI_PAUSED };

/** `status`: "key: value" lines; "-" is empty. Tags are the daemon's raw
 * values (they may hold tabs). */
struct tui_status {
	int state;           /* enum tui_state */
	unsigned long item;  /* changes per item; 0 when stopped */
	char uri[1024];
	char source[64];
	char title[256], artist[256], album[256];
	double position;     /* s */
	double duration;     /* s; 0 = unknown or live */
	int seekable;
	int volume;          /* 0..100 */
	char next[1024];
};

/** @returns 0, or -1 if `text` has no state line (not a status reply). */
int tui_status_read(const char *text, struct tui_status *st);

/* ------------------------------------------------------------------ lists */

enum tui_kind { TUI_TRACK = 1, TUI_LIST = 2 };

/** A row of a list. Strings point into the list's own copy of the
 * reply; "" where the daemon said "-". */
struct tui_item {
	int kind;            /* enum tui_kind; queue rows are tracks */
	long pos;            /* queue position, 1-based; 0 for browse/search */
	int cursor;          /* the queue's cursor is on it */
	const char *uri, *title, *artist, *album, *duration;
};

struct tui_list {
	char *text;          /* owned: the reply, split in place */
	struct tui_item *v;
	size_t n;
	int more;            /* the daemon cut the list short */
};

/** `browse` and `search`: "kind\turi\ttitle\tartist\talbum\tduration"
 * lines; "empty" and "no results" are no items. Replaces `l`'s rows.
 * @returns the number of lines skipped as unreadable, or -1 out of memory. */
int tui_list_items(struct tui_list *l, const char *text);

/** `queue list` ("> 1  uri") or `queue list -l` ("[>]1\turi\ttitle...").
 * "queue is empty" is no items. @returns as tui_list_items. */
int tui_list_queue(struct tui_list *l, const char *text);

void tui_list_free(struct tui_list *l);

/* ------------------------------------------------------------------ watch */

enum tui_event {
	TUI_EV_STATE  = 1u << 0,
	TUI_EV_TRACK  = 1u << 1,
	TUI_EV_VOLUME = 1u << 2,
	TUI_EV_SEEK   = 1u << 3,
	TUI_EV_QUEUE  = 1u << 4,
	TUI_EV_META   = 1u << 5,
	TUI_EV_LEVELS = 1u << 6, /* a levels line */
};

#define TUI_BANDS 32

struct tui_levels {
	uint8_t peak[2];         /* L, R: 0..255 for 0..1 */
	uint8_t band[TUI_BANDS]; /* 40 Hz..16 kHz, log-spaced: 0 = -72 dBFS, 255 = 0 dBFS */
};

/** One line of `watch` (no newline needed): the event names it lists, or
 * TUI_EV_LEVELS with `lv` filled for a levels line. The "ok watching"
 * line and unknown names count as nothing.
 * @returns the TUI_EV_* bits, 0 for none. */
unsigned tui_watch_line(const char *line, struct tui_levels *lv);

#endif
