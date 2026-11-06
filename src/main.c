/* SPDX-License-Identifier: LGPL-2.1-or-later */
/** @file main.c
 * @brief notmmtui: a strip of a player for the mediamaster daemon.
 *
 *   notmmtui             the strip, in the whole terminal (make it short)
 *   notmmtui --version   this client's version and the daemon's
 *
 * Keys: q quits. */

#include "ipc.h"
#include "reply.h"
#include "strip.h"
#include <locale.h>
#include <notcurses/notcurses.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define TICK_MS 250

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
		if (k == 0) { /* the tick */
			dirty |= poll_status(&m);
			continue;
		}
		if (ni.evtype == NCTYPE_RELEASE) continue;
		if (k == NCKEY_RESIZE) {
			notcurses_refresh(nc, NULL, NULL);
			dirty = 1;
		} else if (k == 'q' && ncinput_nomod_p(&ni)) {
			break;
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
