/* SPDX-License-Identifier: LGPL-2.1-or-later */
/** @file watch.c
 * @brief The kept-open connection: open, read lines, close. */

#include "watch.h"
#include "ipc.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

void tui_watch_close(struct tui_watch *w)
{
	if (w->fd >= 0) close(w->fd);
	w->fd = -1;
	w->n = 0;
}

int tui_watch_open(struct tui_watch *w, int argc, char *const *argv)
{
	tui_watch_close(w);
	int fd = tui_connect();
	if (fd < 0) return TUI_NO_DAEMON;
	if (tui_send(fd, argc, argv) != 0) {
		close(fd);
		return TUI_ERR;
	}
	/* The reply line, a byte at a time so nothing after it is taken. */
	char line[256];
	size_t n = 0;
	for (;;) {
		char c;
		ssize_t r = recv(fd, &c, 1, 0);
		if (r < 0 && errno == EINTR) continue;
		if (r <= 0 || c == '\n' || n + 1 == sizeof(line)) break;
		line[n++] = c;
	}
	line[n] = '\0';
	if (strncmp(line, "ok watching", 11) != 0) {
		close(fd);
		return TUI_ERR;
	}
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
	w->fd = fd;
	w->n = 0;
	return TUI_OK;
}

int tui_watch_read(struct tui_watch *w, void (*fn)(void *ctx, const char *line), void *ctx)
{
	if (w->fd < 0) return -1;
	for (;;) {
		if (w->n == sizeof(w->buf)) w->n = 0; /* a line longer than any the daemon writes: drop it */
		ssize_t r = recv(w->fd, w->buf + w->n, sizeof(w->buf) - w->n, 0);
		if (r < 0 && errno == EINTR) continue;
		if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
		if (r <= 0) {
			tui_watch_close(w);
			return -1;
		}
		w->n += (size_t)r;
		char *start = w->buf, *nl;
		while ((nl = memchr(start, '\n', w->n - (size_t)(start - w->buf)))) {
			*nl = '\0';
			fn(ctx, start);
			start = nl + 1;
		}
		w->n -= (size_t)(start - w->buf);
		memmove(w->buf, start, w->n);
	}
}
