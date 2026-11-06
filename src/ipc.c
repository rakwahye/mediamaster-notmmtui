/* SPDX-License-Identifier: LGPL-2.1-or-later */
/** @file ipc.c
 * @brief One command, one connection, the whole reply. */

#include "ipc.h"
#include <mediamaster/mm_client.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#define IO_TIMEOUT_S 5 /* a search over the network may take a while */

const char *tui_socket(void)
{
	static char buf[1024];
	if (!buf[0]) mm_client_socket(buf, sizeof(buf)); /* too long: truncated, fails to connect */
	return buf;
}

int tui_connect(void)
{
	const char *path = tui_socket();
	struct sockaddr_un a;
	memset(&a, 0, sizeof(a));
	a.sun_family = AF_UNIX;
	if (strlen(path) >= sizeof(a.sun_path)) return -1;
	memcpy(a.sun_path, path, strlen(path) + 1);
	int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0) return -1;
	if (connect(fd, (struct sockaddr *)&a, sizeof(a)) != 0) {
		close(fd);
		return -1;
	}
	struct timeval tv = { IO_TIMEOUT_S, 0 };
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
	return fd;
}

static int send_all(int fd, const void *buf, size_t len)
{
	const char *p = buf;
	while (len > 0) {
		ssize_t w = send(fd, p, len, MSG_NOSIGNAL);
		if (w < 0 && errno == EINTR) continue;
		if (w <= 0) return -1;
		p += w;
		len -= (size_t)w;
	}
	return 0;
}

int tui_send(int fd, int argc, char *const *argv)
{
	char hdr[256];
	if (argc < 1) return -1;
	int hl = snprintf(hdr, sizeof(hdr), "%s argc=%d\n", argv[0], argc);
	int bad = hl < 0 || hl >= (int)sizeof(hdr) || send_all(fd, hdr, (size_t)hl);
	for (int i = 0; i < argc && !bad; i++) bad = send_all(fd, argv[i], strlen(argv[i]) + 1);
	return bad ? -1 : 0;
}

int tui_call(int argc, char *const *argv, char *reply, size_t cap)
{
	if (!cap) return TUI_ERR;
	reply[0] = '\0';
	if (argc < 1) return TUI_ERR;
	int fd = tui_connect();
	if (fd < 0) return TUI_NO_DAEMON;
	int bad = tui_send(fd, argc, argv);

	size_t n = 0;
	char sink[4096];
	while (!bad) { /* read to EOF; what does not fit is dropped */
		char *dst = n + 1 < cap ? reply + n : sink;
		size_t room = n + 1 < cap ? cap - 1 - n : sizeof(sink);
		ssize_t r = recv(fd, dst, room, 0);
		if (r < 0 && errno == EINTR) continue;
		if (r <= 0) break;
		if (dst == reply + n) n += (size_t)r;
	}
	close(fd);
	reply[n] = '\0';
	if (bad || !n) {
		snprintf(reply, cap, "%s", bad ? "send failed" : "no reply");
		return TUI_ERR;
	}

	int rc = TUI_OK;
	size_t skip = 0;
	if (!strncmp(reply, "ok", 2) && (reply[2] == ' ' || reply[2] == '\n' || !reply[2])) skip = 2;
	else if (!strncmp(reply, "err", 3) && (reply[3] == ' ' || reply[3] == '\n' || !reply[3])) skip = 3, rc = TUI_ERR;
	if (skip) {
		if (reply[skip] == ' ') skip++;
		memmove(reply, reply + skip, n - skip + 1);
		n -= skip;
		if (n && reply[n - 1] == '\n') reply[--n] = '\0';
	}
	return rc;
}
