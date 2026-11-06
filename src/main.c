/* SPDX-License-Identifier: LGPL-2.1-or-later */
/** @file main.c
 * @brief notmmtui: a strip of a player for the mediamaster daemon.
 *
 *   notmmtui             the strip, in the whole terminal (make it short)
 *   notmmtui --version   this client's version and the daemon's
 *
 * Keys: the transport keys (keys.h), q quits. */

#include "ipc.h"
#include "keys.h"
#include "reply.h"
#include "strip.h"
#include <locale.h>
#include <notcurses/notcurses.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define TICK_MS 250
#define MSG_MS 2000

static void usage(FILE *f)
{
	fputs("usage: notmmtui [--version]\n", f);
}

static int version(void)
{
	char *argv[] = { "version", NULL };
	char reply[256];
	printf("notmmtui %s\n", TUI_VERSION);
	int rc = tui_call(1, argv, reply, sizeof(reply));
	if (rc == TUI_NO_DAEMON) {
		puts("daemon: not running");
		return 1;
	}
	printf("daemon: %s\n", reply);
	return rc == TUI_OK ? 0 : 1;
}

/* Ask for status. @returns 1 if what the strip shows changed. */
static int poll_status(struct strip_model *m)
{
	static char reply[8192];
	char *argv[] = { "status", NULL };
	struct strip_model next = *m;
	int rc = tui_call(1, argv, reply, sizeof(reply));
	if (rc == TUI_NO_DAEMON) next.msg[0] = '\0'; /* nothing to answer for */
	next.daemon = rc != TUI_NO_DAEMON;
	if (rc == TUI_OK && tui_status_read(reply, &next.st) != 0) next.st = m->st; /* keep the last good one */
	if (!next.daemon) memset(&next.st, 0, sizeof(next.st));
	int changed = memcmp(&next, m, sizeof(next)) != 0;
	*m = next;
	return changed;
}

static void deadline(struct timespec *ts, long ms)
{
	clock_gettime(CLOCK_MONOTONIC, ts); /* notcurses_get wants a monotonic deadline */
	ts->tv_sec += ms / 1000;
	ts->tv_nsec += (ms % 1000) * 1000000L;
	if (ts->tv_nsec >= 1000000000L) {
		ts->tv_sec++;
		ts->tv_nsec -= 1000000000L;
	}
}

static int past(const struct timespec *t)
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return now.tv_sec > t->tv_sec || (now.tv_sec == t->tv_sec && now.tv_nsec >= t->tv_nsec);
}

/* Run a key's command; an error shows in the strip for MSG_MS. */
static void command(int argc, char **argv, struct strip_model *m, struct timespec *msg_until)
{
	char reply[256];
	int rc = tui_call(argc, argv, reply, sizeof(reply));
	if (rc == TUI_OK) return;
	snprintf(m->msg, sizeof(m->msg), "%.16s: %.128s", argv[0], rc == TUI_NO_DAEMON ? "mediamaster is not running" : reply);
	deadline(msg_until, MSG_MS);
}

static int run(void)
{
	struct notcurses_options opts = {
		.flags = NCOPTION_SUPPRESS_BANNERS | NCOPTION_INHIBIT_SETLOCALE,
		.loglevel = NCLOGLEVEL_SILENT,
	};
	struct notcurses *nc = notcurses_core_init(&opts, NULL);
	if (!nc) {
		fputs("notmmtui: cannot start the terminal\n", stderr);
		return 1;
	}
	struct ncplane *std = notcurses_stdplane(nc);
	struct strip_model m;
	memset(&m, 0, sizeof(m));
	int dirty = 1;
	struct timespec msg_until = { 0, 0 };
	poll_status(&m);

	for (;;) {
		if (dirty) {
			strip_draw(std, &m);
			notcurses_render(nc);
			dirty = 0;
		}
		struct timespec ts;
		deadline(&ts, TICK_MS);
		ncinput ni;
		uint32_t k = notcurses_get(nc, &ts, &ni);
		if (k == (uint32_t)-1 || k == NCKEY_EOF) break;
		if (m.msg[0] && past(&msg_until)) {
			m.msg[0] = '\0';
			dirty = 1;
		}
		if (k == 0) { /* the tick */
			dirty |= poll_status(&m);
			continue;
		}
		if (ni.evtype == NCTYPE_RELEASE) continue;
		char *cargv[KEYS_ARGV_MAX], buf[32];
		int cargc;
		if (k == NCKEY_RESIZE) {
			notcurses_refresh(nc, NULL, NULL);
			dirty = 1;
		} else if (k == 'q' && ncinput_nomod_p(&ni)) {
			break;
		} else if ((cargc = keys_command(k, &ni, cargv, buf)) > 0) {
			command(cargc, cargv, &m, &msg_until);
			poll_status(&m); /* show the result now, not at the next tick */
			dirty = 1;
		}
	}
	notcurses_stop(nc);
	return 0;
}

int main(int argc, char **argv)
{
	setlocale(LC_ALL, "");
	if (argc == 2 && !strcmp(argv[1], "--version")) return version();
	if (argc == 2 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
		usage(stdout);
		return 0;
	}
	if (argc != 1) {
		usage(stderr);
		return 2;
	}
	return run();
}
