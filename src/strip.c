/* SPDX-License-Identifier: LGPL-2.1-or-later */
/** @file strip.c
 * @brief One row: state, title, artist, time. */

#include "strip.h"
#include "paths.h"
#include <notcurses/notcurses.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

void strip_clock(char *buf, size_t cap, double s)
{
	unsigned long t = s > 0 ? (unsigned long)s : 0;
	if (t >= 3600) snprintf(buf, cap, "%lu:%02lu:%02lu", t / 3600, t / 60 % 60, t % 60);
	else snprintf(buf, cap, "%lu:%02lu", t / 60, t % 60);
}

static void ink(struct ncplane *n, const struct theme *t, int role)
{
	if (t->c[role] == THEME_DEFAULT) ncplane_set_fg_default(n);
	else ncplane_set_fg_rgb(n, (uint32_t)t->c[role]);
}

/* The theme's background under everything, and for what is written. */
static void ground(struct ncplane *n, const struct theme *t)
{
	uint64_t ch = 0;
	if (t->c[TH_BG] == THEME_DEFAULT) ncchannels_set_bg_default(&ch);
	else ncchannels_set_bg_rgb(&ch, (uint32_t)t->c[TH_BG]);
	if (t->c[TH_FG] == THEME_DEFAULT) ncchannels_set_fg_default(&ch);
	else ncchannels_set_fg_rgb(&ch, (uint32_t)t->c[TH_FG]);
	ncplane_set_base(n, " ", 0, ch);
	ncplane_set_channels(n, ch);
	ncplane_set_styles(n, NCSTYLE_NONE);
}

/* Put s at (y, x), no further than column `end`; a cut string ends in
 * "…". Control characters (a tag may hold a tab) show as spaces.
 * @returns the column after the last cell written. */
static int put_clipped(struct ncplane *n, int y, int x, const char *s, int end)
{
	mbstate_t ps;
	memset(&ps, 0, sizeof(ps));
	int col = x;
	const char *p = s;
	while (*p) {
		wchar_t wc;
		size_t len = mbrtowc(&wc, p, strlen(p), &ps);
		if (len == (size_t)-1 || len == (size_t)-2) break;
		int ctl = wc < 0x20 || (wc >= 0x7f && wc < 0xa0);
		int w = ctl ? 1 : wcwidth(wc);
		if (w < 0) w = 0;
		/* room for this glyph, and for "…" if anything follows it */
		int need = w + (p[len] ? 1 : 0);
		if (col + need > end) {
			if (col < end) ncplane_putstr_yx(n, y, col++, "…");
			return col;
		}
		char egc[8];
		memcpy(egc, ctl ? " " : p, ctl ? 1 : len);
		egc[ctl ? 1 : len] = '\0';
		ncplane_putstr_yx(n, y, col, egc);
		col += w;
		p += len;
	}
	return col;
}

void strip_draw(struct ncplane *n, const struct strip_model *m, const struct theme *t)
{
	unsigned rows, cols;
	ncplane_dim_yx(n, &rows, &cols);
	ground(n, t);
	ncplane_erase(n);
	if (!rows || !cols) return;

	if (m->msg[0]) {
		ink(n, t, TH_ORANGE);
		put_clipped(n, 0, 0, m->msg, (int)cols);
		return;
	}
	if (!m->daemon) {
		ink(n, t, TH_DIM);
		char line[300];
		snprintf(line, sizeof(line), "mediamaster is not running (%s)", mm_paths_socket());
		put_clipped(n, 0, 0, line, (int)cols);
		return;
	}
	const struct tui_status *st = &m->st;
	if (st->state == TUI_STOPPED) {
		ink(n, t, TH_DIM);
		put_clipped(n, 0, 0, "■ stopped", (int)cols);
		return;
	}

	char now[16], dur[16], right[40];
	strip_clock(now, sizeof(now), m->pos);
	if (st->duration > 0) {
		strip_clock(dur, sizeof(dur), st->duration);
		snprintf(right, sizeof(right), " %s / %s", now, dur);
	} else {
		snprintf(right, sizeof(right), " %s", now);
	}
	int rw = (int)strlen(right);
	int end = (int)cols - rw;
	if (end < 4) { /* too narrow for both: the time wins */
		ink(n, t, TH_CYAN);
		put_clipped(n, 0, 0, right + 1, (int)cols);
		return;
	}

	int paused = st->state == TUI_PAUSED;
	ink(n, t, paused ? TH_ORANGE : TH_MAGENTA);
	int x = put_clipped(n, 0, 0, paused ? "‖ " : "▶ ", end);
	ink(n, t, TH_FG);
	if (t->bold_title) ncplane_set_styles(n, NCSTYLE_BOLD);
	x = put_clipped(n, 0, x, st->title[0] ? st->title : st->uri, end);
	ncplane_set_styles(n, NCSTYLE_NONE);
	if (st->artist[0] && x < end - 4) {
		ink(n, t, TH_DIM);
		x = put_clipped(n, 0, x, " — ", end);
		ink(n, t, TH_VIOLET);
		put_clipped(n, 0, x, st->artist, end);
	}
	/* " now / dur": what has played in cyan, the rest dim */
	const char *slash = strchr(right, '/');
	size_t nl = slash ? (size_t)(slash - right) : strlen(right);
	ink(n, t, TH_CYAN);
	ncplane_putnstr_yx(n, 0, end, nl, right);
	if (slash) {
		ink(n, t, TH_DIM);
		ncplane_putstr_yx(n, 0, end + (int)nl, slash);
	}
}
