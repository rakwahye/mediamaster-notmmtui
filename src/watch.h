/* SPDX-License-Identifier: LGPL-2.1-or-later */
/** @file watch.h
 * @brief A `watch` connection to the daemon, read without blocking.
 *
 * The daemon writes one line per tick naming what changed; it drops a
 * watcher that stops reading, so the reader must keep up and, when the
 * connection closes, open it again. */

#ifndef TUI_WATCH_H
#define TUI_WATCH_H

#include <stddef.h>

struct tui_watch {
	int fd;          /* -1 when closed */
	char buf[1024];  /* a line not yet complete */
	size_t n;
};

#define TUI_WATCH_INIT { -1, { 0 }, 0 }

/** Open `watch argv[1..]` (argv[0] is "watch"). The "ok watching" reply
 * is read before this returns.
 * @returns TUI_OK, TUI_NO_DAEMON, or TUI_ERR when the daemon refuses
 * (a daemon too old to watch says "unknown verb"). */
int tui_watch_open(struct tui_watch *w, int argc, char *const *argv);

/** Read what has arrived and give each whole line, without its newline,
 * to fn. @returns 0, or -1 when the connection has closed (w is then
 * closed too). */
int tui_watch_read(struct tui_watch *w, void (*fn)(void *ctx, const char *line), void *ctx);

void tui_watch_close(struct tui_watch *w);

#endif
