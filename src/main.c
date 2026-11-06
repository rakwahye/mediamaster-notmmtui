/* SPDX-License-Identifier: LGPL-2.1-or-later */
/** @file main.c
 * @brief notmmtui: a strip of a player for the mediamaster daemon.
 *
 *   notmmtui [--socket <path>] [--theme powerlines|plain|<file>]
 *            [--look powerline|plain]
 *                        the strip, in the whole terminal (make it short)
 *   notmmtui [--socket <path>] --version
 *                        this client's version and the daemon's
 *
 * Socket: --socket, else $MM_SOCK, else the daemon's default, as mmctl. 
 *
 * Keys: the transport keys (keys.h), q quits.
 *
 * It follows the daemon through a `watch` connection: status is read
 * again when a line says something changed, and every RESYNC_MS anyway;
 * between reads the position is counted here. When the connection
 * closes it is opened again; a daemon that cannot watch is asked for
 * status every tick instead. */

#include "ipc.h"
#include "paths.h"
#include "keys.h"
#include "reply.h"
#include "strip.h"
#include "theme.h"
#include "watch.h"
#include <errno.h>
#include <locale.h>
#include <math.h>
#include <notcurses/notcurses.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define TICK_MS 250
#define MSG_MS 2000
#define RESYNC_MS 10000
#define RETRY_MIN_MS 250   /* watch closed: open again after this, doubling */
#define RETRY_MAX_MS 2000
#define RETRY_OLD_MS 10000 /* the daemon cannot watch: ask again after this */

static void usage(FILE *f)
{
	fputs("usage: notmmtui [--socket <path>] [--theme powerlines|plain|<file>]\n"
	      "                [--look powerline|plain]\n"
	      "       notmmtui [--socket <path>] --version\n"
	      "socket: --socket, else $MM_SOCK, else the daemon's default\n", f);
}

static int version(void)
{
	char *argv[] = { "version", NULL };
	char reply[256];
	printf("notmmtui %s\n", TUI_VERSION);
	int rc = tui_call(1, argv, reply, sizeof(reply));
	if (rc == TUI_NO_DAEMON) {
		printf("daemon: not running (%s)\n", mm_paths_socket());
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
	clock_gettime(CLOCK_MONOTONIC, ts);
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

static double since(const struct timespec *t)
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (double)(now.tv_sec - t->tv_sec) + (double)(now.tv_nsec - t->tv_nsec) / 1e9;
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

/* How the strip keeps up with the daemon. */
struct follow {
	struct tui_watch w;
	int polling;            /* no watch connection: status every tick */
	long backoff_ms;
	struct timespec retry;  /* open watch again from then */
	struct timespec resync; /* read status again from then */
	struct timespec read_at;/* when status was last read */
	unsigned events;        /* TUI_EV_* from lines not yet acted on */
};

static void on_line(void *ctx, const char *line)
{
	struct follow *f = ctx;
	f->events |= tui_watch_line(line, NULL);
}

/* Keep a watch connection open, or say why not. */
static void keep_watching(struct follow *f, int *need_status)
{
	if (f->w.fd >= 0 || !past(&f->retry)) return;
	char *argv[] = { "watch", NULL };
	int rc = tui_watch_open(&f->w, 1, argv);
	if (rc == TUI_OK) {
		f->polling = 0;
		f->backoff_ms = RETRY_MIN_MS;
		*need_status = 1; /* whatever happened while not watching */
		return;
	}
	f->polling = 1;
	if (rc == TUI_ERR) {
		deadline(&f->retry, RETRY_OLD_MS);
	} else {
		deadline(&f->retry, f->backoff_ms);
		f->backoff_ms = f->backoff_ms * 2 > RETRY_MAX_MS ? RETRY_MAX_MS : f->backoff_ms * 2;
	}
}

/* The position now: counted on from the last status while playing. */
static double shown_position(const struct strip_model *m, const struct follow *f)
{
	double p = m->st.position;
	if (m->daemon && m->st.state == TUI_PLAYING) p += since(&f->read_at);
	if (m->st.duration > 0 && p > m->st.duration) p = m->st.duration;
	return p;
}

static int run(const struct theme *theme, int look)
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
	int infd = notcurses_inputready_fd(nc);
	struct strip *strip = strip_new(std);
	if (!strip) {
		notcurses_stop(nc);
		fputs("notmmtui: cannot make the strip's planes\n", stderr);
		return 1;
	}
	struct strip_model m;
	memset(&m, 0, sizeof(m));
	m.look = look;
	struct follow f = { .w = TUI_WATCH_INIT, .polling = 1, .backoff_ms = RETRY_MIN_MS };
	struct timespec msg_until = { 0, 0 };
	int dirty = 1, need_status = 1, quit = 0;

	while (!quit) {
		keep_watching(&f, &need_status);
		if (f.polling || past(&f.resync)) need_status = 1;
		if (need_status) {
			dirty |= poll_status(&m);
			clock_gettime(CLOCK_MONOTONIC, &f.read_at);
			deadline(&f.resync, RESYNC_MS);
			need_status = 0;
		}
		double pos = shown_position(&m, &f);
		unsigned rows, cols;
		ncplane_dim_yx(std, &rows, &cols);
		int bar = strip_bar(cols, pos, m.st.duration);
		if (floor(pos) != m.pos || bar != m.bar) { /* the clock, or the bar by an eighth */
			m.pos = floor(pos);
			m.bar = bar;
			dirty = 1;
		}
		if (m.msg[0] && past(&msg_until)) {
			m.msg[0] = '\0';
			dirty = 1;
		}
		if (dirty) {
			strip_draw(strip, &m, theme);
			notcurses_render(nc);
			dirty = 0;
		}

		struct pollfd pf[2] = { { infd, POLLIN, 0 }, { f.w.fd, POLLIN, 0 } };
		if (poll(pf, f.w.fd >= 0 ? 2 : 1, TICK_MS) < 0 && errno != EINTR) break;

		ncinput ni;
		uint32_t k;
		while (!quit && (k = notcurses_get_nblock(nc, &ni)) != 0) {
			char *cargv[KEYS_ARGV_MAX], buf[32];
			int cargc;
			if (k == (uint32_t)-1 || k == NCKEY_EOF) quit = 1;
			else if (ni.evtype == NCTYPE_RELEASE) continue;
			else if (k == NCKEY_RESIZE) {
				notcurses_refresh(nc, NULL, NULL);
				dirty = 1;
			} else if (k == 'q' && ncinput_nomod_p(&ni)) {
				quit = 1;
			} else if ((cargc = keys_command(k, &ni, cargv, buf)) > 0) {
				command(cargc, cargv, &m, &msg_until);
				need_status = dirty = 1; /* show the result now */
			}
		}
		if (f.w.fd >= 0 && tui_watch_read(&f.w, on_line, &f) != 0) {
			deadline(&f.retry, f.backoff_ms); /* closed: the daemon went, or dropped us */
			need_status = 1;
		}
		if (f.events) {
			need_status = 1;
			f.events = 0;
		}
	}
	tui_watch_close(&f.w);
	strip_free(strip);
	notcurses_stop(nc);
	return 0;
}

int main(int argc, char **argv)
{
	setlocale(LC_ALL, "");
	struct theme theme = theme_powerlines;
	const char *theme_name = NULL;
	int want_version = 0, look = STRIP_POWERLINE;
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
			usage(stdout);
			return 0;
		} else if (!strcmp(a, "--version")) {
			want_version = 1;
		} else if (!strcmp(a, "--look") && i + 1 < argc) {
			const char *l = argv[++i];
			if (!strcmp(l, "powerline")) look = STRIP_POWERLINE;
			else if (!strcmp(l, "plain")) look = STRIP_PLAIN;
			else {
				fprintf(stderr, "notmmtui: no look %s (powerline, plain)\n", l);
				return 2;
			}
		} else if ((!strcmp(a, "--socket") || !strcmp(a, "--theme")) && i + 1 < argc) {
			if (a[2] == 's') {
				/* read by mm_paths_socket on first use, which is after this */
				if (!argv[i + 1][0] || setenv("MM_SOCK", argv[i + 1], 1) != 0) {
					fputs("notmmtui: --socket needs a path\n", stderr);
					return 2;
				}
			} else {
				theme_name = argv[i + 1];
			}
			i++;
		} else {
			usage(stderr);
			return 2;
		}
	}
	if (want_version) return version();
	if (theme_name) {
		char err[256];
		if (theme_load(theme_name, &theme, err, sizeof(err)) != 0) {
			fprintf(stderr, "notmmtui: theme %s\n", err);
			return 2;
		}
	}
	return run(&theme, look);
}
