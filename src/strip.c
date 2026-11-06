/* SPDX-License-Identifier: LGPL-2.1-or-later */
/** @file strip.c
 * @brief One row: state, title, artist, time. */

#include "strip.h"
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

void strip_draw(struct ncplane *n, const struct strip_model *m)
{
	unsigned rows, cols;
	ncplane_dim_yx(n, &rows, &cols);
	ncplane_erase(n);
	if (!rows || !cols) return;

	if (m->msg[0]) {
		put_clipped(n, 0, 0, m->msg, (int)cols);
		return;
	}
	if (!m->daemon) {
		put_clipped(n, 0, 0, "mediamaster is not running", (int)cols);
		return;
	}
	const struct tui_status *st = &m->st;
	if (st->state == TUI_STOPPED) {
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
		put_clipped(n, 0, 0, right + 1, (int)cols);
		return;
	}

	int x = put_clipped(n, 0, 0, st->state == TUI_PAUSED ? "‖ " : "▶ ", end);
	const char *title = st->title[0] ? st->title : st->uri;
	x = put_clipped(n, 0, x, title, end);
	if (st->artist[0] && x < end - 4) {
		x = put_clipped(n, 0, x, " — ", end);
		put_clipped(n, 0, x, st->artist, end);
	}
	ncplane_putstr_yx(n, 0, end, right);
}
