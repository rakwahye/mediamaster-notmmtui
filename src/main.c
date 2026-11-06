/* SPDX-License-Identifier: LGPL-2.1-or-later */
/** @file main.c
 * @brief notmmtui: a strip of a player for the mediamaster daemon.
 *
 *   notmmtui [--socket <path>] [--theme powerlines|plain|<file>]
 *            [--look powerline|plasma|plain]
 *                        the strip, in the whole terminal (make it short)
 *   notmmtui [--socket <path>] --version
 *                        this client's version and the daemon's
 *
 * The IPC protocol is the only contract with the daemon: before it
 * talks to one, on start and on every reconnect, it asks `version` and
 * refuses a daemon that is not mediamaster, or one it does not fit by the
 * SDK's rule (MM_ABI_FITS: the ABI this was built for, MM_ABI, against
 * the one the daemon offers). On start that is an error; while running,
 * the strip says so and asks again until one fits.
 *
 * Socket: --socket, else $MM_SOCK, else the daemon's default, as mmctl. 
 *
 * Keys: the transport keys (keys.h), l the next look, q quits.
 *
 * Frames: every STRIP_FRAME_MS while the strip is changing by itself (a
 * look that moves, a title decoding in, a pause breathing); otherwise a
 * wake every TICK_MS, drawing only what changed.
 *
 * It follows the daemon through a `watch` connection: status is read
 * again when a line says something changed, and every RESYNC_MS anyway;
 * between reads the position is counted here. When the connection
 * closes it is opened again; a daemon that cannot watch is asked for
 * status every tick instead.
 *
 * Levels come on a second connection, `watch levels`, 20 lines a second
 * while there is sound: kept apart so the event watch is never the slow
 * reader the daemon drops. It is open while the event watch is, opened
 * again when it closes, and left be for a daemon that has no levels. */

#include "ipc.h"
#include <mediamaster/mm_client.h>
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
#include <unistd.h>

#define TICK_MS 250
#define MSG_MS 2000
#define RESYNC_MS 10000
#define RETRY_MIN_MS 250   /* watch closed: open again after this, doubling */
#define RETRY_MAX_MS 2000
#define RETRY_OLD_MS 10000 /* the daemon cannot watch: ask again after this */
#define BREATH_S     3.0   /* one breath while paused */

static void usage(FILE *f)
{
	fputs("usage: notmmtui [--socket <path>] [--theme powerlines|plain|<file>]\n"
	      "                [--look powerline|plasma|plain]\n"
	      "       notmmtui [--socket <path>] --version\n"
	      "socket: --socket, else $MM_SOCK, else the daemon's default\n", f);
}

/* Whether the daemon on the socket is one this was built for.
 * @returns 1 yes, 0 no (why says why), TUI_NO_DAEMON when none answers.
 * reply gets what it said. */
static int daemon_fits(char *why, size_t cap, char *reply, size_t rcap)
{
	char *argv[] = { "version", NULL };
	int rc = tui_call(1, argv, reply, rcap);
	if (rc == TUI_NO_DAEMON) return TUI_NO_DAEMON;
	struct mm_version v;
	if (rc != TUI_OK || mm_version_read(reply, &v) != 0 || strcmp(v.name, MM_CLIENT_NAME) != 0) {
		snprintf(why, cap, "not a mediamaster daemon on %.120s (version: %.40s)", tui_socket(), reply);
		return 0;
	}
	if (!MM_ABI_FITS(MM_ABI, v.abi)) {
		snprintf(why, cap, "mediamaster %s offers abi %u.%u; this notmmtui was built for abi %u.%u: rebuild it (mm-update)",
		         v.version, MM_ABI_MAJOR_OF(v.abi), MM_ABI_MINOR_OF(v.abi), MM_ABI_MAJOR, MM_ABI_MINOR);
		return 0;
	}
	return 1;
}

static int version(void)
{
	char reply[256], why[256];
	printf("notmmtui %s (for mediamaster abi %u.%u)\n", TUI_VERSION, MM_ABI_MAJOR, MM_ABI_MINOR);
	int fits = daemon_fits(why, sizeof(why), reply, sizeof(reply));
	if (fits == TUI_NO_DAEMON) {
		printf("daemon: not running (%s)\n", tui_socket());
		return 1;
	}
	printf("daemon: %s\n", reply);
	if (!fits) printf("refused: %s\n", why);
	return fits ? 0 : 1;
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
	struct tui_watch lw;    /* watch levels */
	long lbackoff_ms;
	struct timespec lretry;
	struct tui_levels lv;   /* the last levels line */
	int got_levels;         /* lv is new since it was looked at */
	struct react *rx;       /* every levels line goes to it, for the beat */
};

static void on_line(void *ctx, const char *line)
{
	struct follow *f = ctx;
	f->events |= tui_watch_line(line, NULL);
}

static void on_levels(void *ctx, const char *line)
{
	struct follow *f = ctx;
	if (tui_watch_line(line, &f->lv) & TUI_EV_LEVELS) {
		f->got_levels = 1;
		react_feed(f->rx, &f->lv);
	}
}

/* Keep `watch levels` open beside the event watch, and only then. */
static void keep_levels(struct follow *f, struct strip_model *m)
{
	if (f->w.fd < 0) { /* no daemon we watch: no levels either */
		if (f->lw.fd >= 0) tui_watch_close(&f->lw);
		if (m->levels) {
			struct tui_levels none;
			memset(&none, 0, sizeof(none));
			m->levels = 0;
			react_feed(&m->rx, &none);
		}
		f->lbackoff_ms = RETRY_MIN_MS;
		return;
	}
	if (f->lw.fd >= 0 || !past(&f->lretry)) return;
	char *argv[] = { "watch", "levels", NULL };
	int rc = tui_watch_open(&f->lw, 2, argv);
	if (rc == TUI_OK) {
		f->lbackoff_ms = RETRY_MIN_MS;
	} else if (rc == TUI_ERR) {
		deadline(&f->lretry, RETRY_OLD_MS); /* a daemon with no levels: ask again later */
	} else {
		deadline(&f->lretry, f->lbackoff_ms);
		f->lbackoff_ms = f->lbackoff_ms * 2 > RETRY_MAX_MS ? RETRY_MAX_MS : f->lbackoff_ms * 2;
	}
}

/* Keep a watch connection open to a daemon that fits, or say why not.
 * Each connection, the first and every one after, is checked first. */
static void keep_watching(struct follow *f, struct strip_model *m, int *need_status)
{
	if (f->w.fd >= 0 || !past(&f->retry)) return;
	char why[sizeof(m->refused)], reply[256];
	int fits = daemon_fits(why, sizeof(why), reply, sizeof(reply));
	if (fits == 0) { /* say so, and look again in a while */
		if (strcmp(m->refused, why)) {
			snprintf(m->refused, sizeof(m->refused), "%s", why);
			*need_status = 1; /* a redraw; status itself is not asked */
		}
		f->polling = 0;
		deadline(&f->retry, RETRY_MAX_MS);
		return;
	}
	if (m->refused[0]) {
		m->refused[0] = '\0';
		*need_status = 1;
	}
	if (fits == TUI_NO_DAEMON) {
		f->polling = 1; /* status says it is not running, every tick */
		deadline(&f->retry, f->backoff_ms);
		f->backoff_ms = f->backoff_ms * 2 > RETRY_MAX_MS ? RETRY_MAX_MS : f->backoff_ms * 2;
		return;
	}
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
	react_reset(&m.rx);
	struct follow f = { .w = TUI_WATCH_INIT, .polling = 1, .backoff_ms = RETRY_MIN_MS,
	                    .lw = TUI_WATCH_INIT, .lbackoff_ms = RETRY_MIN_MS, .rx = &m.rx };
	struct timespec msg_until = { 0, 0 };
	int dirty = 1, need_status = 1, quit = 0;
	struct timespec frame_at, changed_at, start;
	clock_gettime(CLOCK_MONOTONIC, &frame_at);
	start = changed_at = frame_at;
	unsigned long last_item = 0;
	m.decode = STRIP_SETTLED;

	while (!quit) {
		keep_watching(&f, &m, &need_status);
		keep_levels(&f, &m);
		if (f.polling || past(&f.resync)) need_status = 1;
		if (need_status && m.refused[0]) { /* not ours to talk to: no status, just the refusal */
			m.daemon = 0;
			memset(&m.st, 0, sizeof(m.st));
			dirty = 1;
			need_status = 0;
		}
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
		/* a new item decodes in; its title and artist are known once status is */
		if (m.daemon && m.st.item && m.st.item != last_item) {
			last_item = m.st.item;
			clock_gettime(CLOCK_MONOTONIC, &changed_at);
			m.decode = 0;
		}
		if (m.decode < STRIP_SETTLED) {
			m.decode = since(&changed_at);
			if (m.decode > strip_decode_time(strlen(m.st.title) + strlen(m.st.artist) + strlen(m.st.uri)))
				m.decode = STRIP_SETTLED;
		}
		/* a look that moves: its clock runs while playing; a pause breathes */
		int playing = m.daemon && m.st.state == TUI_PLAYING;
		double dt = since(&frame_at);
		clock_gettime(CLOCK_MONOTONIC, &frame_at);
		if (dt > 0.5) dt = 0.5;
		react_step(&m.rx, dt);
		/* the plasma runs faster as it gets louder, and jumps on a beat */
		double pace = m.levels ? 0.45 + 1.4 * m.rx.level + 0.8 * m.rx.beat : 1.0;
		if (playing && strip_look_moves(m.look)) m.anim += dt * pace;
		m.breath = m.st.state == TUI_PAUSED ? 0.5 + 0.5 * cos(2 * M_PI * since(&start) / BREATH_S) : 0;
		int moving = strip_needs_frames(&m);
		if (moving) dirty = 1;
		if (m.msg[0] && past(&msg_until)) {
			m.msg[0] = '\0';
			dirty = 1;
		}
		if (dirty) {
			strip_draw(strip, &m, theme);
			notcurses_render(nc);
			dirty = 0;
		}

		struct pollfd pf[3] = { { infd, POLLIN, 0 }, { -1, POLLIN, 0 }, { -1, POLLIN, 0 } };
		nfds_t npf = 1;
		if (f.w.fd >= 0) pf[npf++].fd = f.w.fd;
		if (f.lw.fd >= 0) pf[npf++].fd = f.lw.fd;
		if (poll(pf, npf, moving ? STRIP_FRAME_MS : TICK_MS) < 0 && errno != EINTR) break;

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
			} else if (k == 'l' && ncinput_nomod_p(&ni)) {
				m.look = (m.look + 1) % STRIP_LOOKS;
				dirty = 1;
			} else if (!m.refused[0] && (cargc = keys_command(k, &ni, cargv, buf)) > 0) {
				command(cargc, cargv, &m, &msg_until);
				need_status = dirty = 1; /* show the result now */
			}
		}
		if (f.w.fd >= 0 && tui_watch_read(&f.w, on_line, &f) != 0) {
			deadline(&f.retry, f.backoff_ms); /* closed: the daemon went, or dropped us */
			need_status = 1;
		}
		if (f.lw.fd >= 0 && tui_watch_read(&f.lw, on_levels, &f) != 0) {
			deadline(&f.lretry, f.lbackoff_ms); /* closed: opened again in a moment */
			if (m.levels) { /* no meter frozen on its last line meanwhile */
				m.levels = 0;
				dirty = 1;
			}
			struct tui_levels none;
			memset(&none, 0, sizeof(none));
			react_feed(&m.rx, &none); /* and the looks settle */
		}
		if (f.got_levels) {
			f.got_levels = 0;
			if (!m.levels || memcmp(&m.lv, &f.lv, sizeof(m.lv))) {
				m.lv = f.lv;
				m.levels = 1;
				dirty = 1;
			}
		}
		if (f.events) {
			need_status = 1;
			f.events = 0;
		}
	}
	tui_watch_close(&f.w);
	tui_watch_close(&f.lw);
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
			look = strip_look_find(argv[++i]);
			if (look < 0) {
				fprintf(stderr, "notmmtui: no look %s (powerline, plasma, plain)\n", argv[i]);
				return 2;
			}
		} else if ((!strcmp(a, "--socket") || !strcmp(a, "--theme")) && i + 1 < argc) {
			if (a[2] == 's') {
				/* read by tui_socket on first use, which is after this */
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
	/* what was asked first, then the daemon, then the terminal */
	if (theme_name) {
		char err[256];
		if (theme_load(theme_name, &theme, err, sizeof(err)) != 0) {
			fprintf(stderr, "notmmtui: theme %s\n", err);
			return 2;
		}
	}
	char why[256], reply[256];
	if (daemon_fits(why, sizeof(why), reply, sizeof(reply)) == 0) { /* none running is fine: the strip waits */
		fprintf(stderr, "notmmtui: %s\n", why);
		return 1;
	}
	if (!isatty(STDOUT_FILENO) || !isatty(STDIN_FILENO)) {
		fputs("notmmtui: the strip needs a terminal (--version works without one)\n", stderr);
		return 1;
	}
	return run(&theme, look);
}
