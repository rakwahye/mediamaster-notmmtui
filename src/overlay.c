/* SPDX-License-Identifier: LGPL-2.1-or-later */
/** @file overlay.c
 * @brief The queue over the strip: keys, a window on the list, drawing. */

#include "overlay.h"
#include "strip.h"
#include <notcurses/notcurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

struct overlay *overlay_new(struct ncplane *parent)
{
	struct overlay *o = calloc(1, sizeof(*o));
	if (!o) return NULL;
	unsigned rows, cols;
	ncplane_dim_yx(parent, &rows, &cols);
	struct ncplane_options po = { .rows = rows ? rows : 1, .cols = cols ? cols : 1 };
	o->n = ncplane_create(parent, &po);
	if (!o->n) {
		free(o);
		return NULL;
	}
	uint64_t clear = 0;
	ncchannels_set_fg_alpha(&clear, NCALPHA_TRANSPARENT);
	ncchannels_set_bg_alpha(&clear, NCALPHA_TRANSPARENT);
	ncplane_set_base(o->n, "", 0, clear);
	return o;
}

void overlay_free(struct overlay *o)
{
	if (!o) return;
	tui_list_free(&o->q);
	ncplane_destroy(o->n);
	free(o);
}

/* ------------------------------------------------------------------ keys */

static enum overlay_did on_item(struct overlay *o, char **argv, const char *verb)
{
	if (!o->q.n) return OVERLAY_TAKEN;
	snprintf(o->pos, sizeof(o->pos), "%ld", o->q.v[o->sel].pos);
	argv[0] = "queue";
	argv[1] = (char *)verb;
	argv[2] = o->pos;
	argv[3] = NULL;
	return OVERLAY_COMMAND;
}

/* The add: line's keys: text in, the last letter out, ⏎ adds, esc leaves. */
static enum overlay_did adding(struct overlay *o, uint32_t id, const struct ncinput *ni, char **argv)
{
	size_t len = strlen(o->input);
	if (id == NCKEY_ESC) {
		o->adding = 0;
	} else if (id == NCKEY_ENTER || id == '\r' || id == '\n') {
		o->adding = 0;
		if (!len) return OVERLAY_TAKEN;
		argv[0] = "queue";
		argv[1] = "add";
		argv[2] = o->input;
		argv[3] = NULL;
		return OVERLAY_COMMAND;
	} else if (id == NCKEY_BACKSPACE || id == 0x7f || id == 0x08) {
		while (len && (o->input[len - 1] & 0xc0) == 0x80) len--; /* a whole letter */
		if (len) len--;
		o->input[len] = '\0';
	} else if (id >= 0x20 && !nckey_synthesized_p(id) && ni && ni->utf8[0]) {
		size_t add = strlen(ni->utf8);
		if (len + add < sizeof(o->input)) memcpy(o->input + len, ni->utf8, add + 1);
	}
	return OVERLAY_TAKEN; /* every key is the line's while it is up */
}

enum overlay_did overlay_key(struct overlay *o, uint32_t id, const struct ncinput *ni, char **argv)
{
	if (ni && (ncinput_ctrl_p(ni) || ncinput_alt_p(ni))) return OVERLAY_NOT_MINE;
	if (!o->open) {
		if (id != NCKEY_TAB) return OVERLAY_NOT_MINE;
		o->open = 1;
		o->adding = 0;
		return OVERLAY_OPENED;
	}
	if (o->adding) return adding(o, id, ni, argv);
	size_t n = o->q.n;
	switch (id) {
	case NCKEY_TAB: case NCKEY_ESC: case 'q':
		o->open = 0;
		return OVERLAY_CLOSED;
	case 'j': case NCKEY_DOWN:
		if (o->sel + 1 < n) o->sel++;
		return OVERLAY_TAKEN;
	case 'k': case NCKEY_UP:
		if (o->sel) o->sel--;
		return OVERLAY_TAKEN;
	case 'g': case NCKEY_HOME:
		o->sel = 0;
		return OVERLAY_TAKEN;
	case 'G': case NCKEY_END:
		o->sel = n ? n - 1 : 0;
		return OVERLAY_TAKEN;
	case NCKEY_ENTER: case '\r': case '\n':
		return on_item(o, argv, "play");
	case 'd':
		return on_item(o, argv, "rm");
	case 'a':
		o->adding = 1;
		o->input[0] = '\0';
		return OVERLAY_TAKEN;
	default:
		return OVERLAY_NOT_MINE;
	}
}

void overlay_set(struct overlay *o, const char *reply, int first)
{
	tui_list_free(&o->q);
	if (!reply || tui_list_queue(&o->q, reply) < 0) {
		tui_list_free(&o->q);
		memset(&o->q, 0, sizeof(o->q));
	}
	if (first)
		for (size_t i = 0; i < o->q.n; i++)
			if (o->q.v[i].cursor) o->sel = i;
	if (o->sel >= o->q.n) o->sel = o->q.n ? o->q.n - 1 : 0;
}

size_t overlay_top(size_t sel, size_t top, size_t n, int rows)
{
	if (rows < 1 || !n) return 0;
	size_t r = (size_t)rows;
	if (sel < top) top = sel;
	if (sel >= top + r) top = sel - r + 1;
	if (top + r > n) top = n > r ? n - r : 0; /* no empty rows below while there is more above */
	return top;
}

/* ------------------------------------------------------------------ drawing */

static int cols_of(const char *s)
{
	mbstate_t ps;
	memset(&ps, 0, sizeof(ps));
	int n = 0;
	for (const char *p = s; *p;) {
		wchar_t wc;
		size_t len = mbrtowc(&wc, p, strlen(p), &ps);
		if (len == (size_t)-1 || len == (size_t)-2) break;
		int w = wcwidth(wc);
		n += w > 0 ? w : 1;
		p += len;
	}
	return n;
}

static void fg(struct ncplane *n, const struct theme *t, int role)
{
	if (t->c[role] == THEME_DEFAULT) ncplane_set_fg_default(n);
	else ncplane_set_fg_rgb(n, (uint32_t)t->c[role]);
}

/* `s` from column x, cut with "…" to end before `end`; control characters
 * as spaces. @returns the column after it. */
static int put(struct ncplane *n, int y, int x, const char *s, int end)
{
	mbstate_t ps;
	memset(&ps, 0, sizeof(ps));
	for (const char *p = s; *p;) {
		wchar_t wc;
		size_t len = mbrtowc(&wc, p, strlen(p), &ps);
		if (len == (size_t)-1 || len == (size_t)-2) break;
		int ctl = wc < 0x20 || (wc >= 0x7f && wc < 0xa0);
		int w = ctl ? 1 : wcwidth(wc);
		if (w < 0) w = 0;
		if (x + w + (p[len] ? 1 : 0) > end) {
			if (x < end) ncplane_putstr_yx(n, y, x++, "…");
			return x;
		}
		char egc[8];
		memcpy(egc, ctl ? " " : p, ctl ? 1 : len);
		egc[ctl ? 1 : len] = '\0';
		ncplane_putstr_yx(n, y, x, egc);
		x += w;
		p += len;
	}
	return x;
}

/* A row's ground: the bar (bar 1); the theme's bg, solid under words
 * (bar 0) or blended over the look (bar -1). */
static void ground(struct ncplane *n, const struct theme *t, int y, unsigned cols, int bar)
{
	uint32_t bg = t->c[TH_BG] == THEME_DEFAULT ? 0 : (uint32_t)t->c[TH_BG];
	uint32_t violet = t->c[TH_VIOLET] == THEME_DEFAULT ? 0x5f5f87 : (uint32_t)t->c[TH_VIOLET];
	uint32_t c = bar > 0 ? (((bg >> 16 & 0xff) * 11 + (violet >> 16 & 0xff) * 9) / 20) << 16 |
	                   (((bg >> 8 & 0xff) * 11 + (violet >> 8 & 0xff) * 9) / 20) << 8 |
	                   ((bg & 0xff) * 11 + (violet & 0xff) * 9) / 20
	                 : bg;
	ncplane_set_bg_rgb(n, c);
	ncplane_set_bg_alpha(n, bar >= 0 ? NCALPHA_OPAQUE : NCALPHA_BLEND);
	for (unsigned x = 0; x < cols; x++) ncplane_putstr_yx(n, y, (int)x, " ");
}

static void item(struct overlay *o, const struct theme *t, size_t i, int y, unsigned cols, int numw)
{
	struct ncplane *n = o->n;
	const struct tui_item *it = &o->q.v[i];
	ground(n, t, y, cols, i == o->sel);
	int end = (int)cols - 1;
	if (it->cursor) {
		fg(n, t, TH_MAGENTA);
		ncplane_putstr_yx(n, y, 1, "▶");
	}
	char num[24], name[256];
	snprintf(num, sizeof(num), "%*ld", numw, it->pos);
	fg(n, t, i == o->sel ? TH_FG : TH_DIM); /* dim on the bar would not read */
	int x = put(n, y, 3, num, end) + 2;
	int dw = it->duration[0] ? cols_of(it->duration) + 2 : 0;
	if (dw) {
		fg(n, t, i == o->sel ? TH_FG : TH_DIM);
		put(n, y, end - dw + 2, it->duration, end);
	}
	strip_item_name(it, name, sizeof(name));
	fg(n, t, TH_FG);
	x = put(n, y, x, name, end - dw);
	if (it->title[0] && it->artist[0] && x + 4 < end - dw) {
		fg(n, t, i == o->sel ? TH_FG : TH_DIM);
		x = put(n, y, x, " — ", end - dw);
		fg(n, t, TH_VIOLET);
		put(n, y, x, it->artist, end - dw);
	}
}

void overlay_draw(struct overlay *o, const struct theme *t, const char *msg, unsigned rows, unsigned cols)
{
	struct ncplane *n = o->n;
	unsigned r, c;
	ncplane_dim_yx(n, &r, &c);
	if (r != rows || c != cols) ncplane_resize_simple(n, rows, cols);
	ncplane_erase(n);
	if (!o->open || !rows || !cols) return;
	ncplane_set_styles(n, NCSTYLE_NONE);
	int head = rows >= 4, line = o->adding && rows >= 2;
	int list = (int)rows - head - line, y = 0, end = (int)cols - 1;
	char count[48];
	snprintf(count, sizeof(count), "%zu/%zu%s", o->q.n ? o->sel + 1 : 0, o->q.n, o->q.more ? "+" : "");

	if (head) {
		ground(n, t, 0, cols, 0);
		fg(n, t, TH_MAGENTA);
		int x = put(n, 0, 1, "queue", end) + 1;
		fg(n, t, TH_DIM);
		x = put(n, 0, x, count, end) + 3;
		const char *keys = msg && msg[0] ? msg : "j k move · ⏎ play · a add · d remove · tab close";
		int kw = cols_of(keys);
		fg(n, t, msg && msg[0] ? TH_ORANGE : TH_DIM);
		put(n, 0, end - kw > x ? end - kw : x, keys, end);
		y = 1;
	}
	if (list > 0) {
		if (!o->q.n) {
			ground(n, t, y, cols, 0);
			fg(n, t, TH_DIM);
			put(n, y, 3, head || line ? "the queue is empty" : "the queue is empty · a add · tab close", end);
			for (int i = 1; i < list; i++) ground(n, t, y + i, cols, -1);
		} else {
			o->top = overlay_top(o->sel, o->top, o->q.n, list);
			int numw = snprintf(NULL, 0, "%ld", o->q.v[o->q.n - 1].pos);
			for (int i = 0; i < list; i++) {
				size_t k = o->top + (size_t)i;
				if (k < o->q.n) item(o, t, k, y + i, cols, numw);
				else ground(n, t, y + i, cols, -1);
			}
		}
	}
	if (o->adding) { /* the add: line, at the bottom, or the only row */
		int ay = (int)rows - 1;
		ground(n, t, ay, cols, 0);
		fg(n, t, TH_CYAN);
		int x = put(n, ay, 1, "add: ", end);
		fg(n, t, TH_FG);
		int w = cols_of(o->input);
		/* the end of what is typed stays in view */
		const char *s = o->input;
		while (w > end - x - 1 && *s) {
			s++;
			while ((*s & 0xc0) == 0x80) s++;
			w = cols_of(s);
		}
		x = put(n, ay, x, s, end);
		fg(n, t, TH_CYAN);
		ncplane_putstr_yx(n, ay, x, "▏");
	}
}
