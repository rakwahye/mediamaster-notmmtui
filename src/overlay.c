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

/* The glass: a tint Notcurses blends over the look, fg and bg, so the
 * look's own glyphs show through it, dimly (as notcurses-demo's HUD). A
 * cell with a word in it is solid instead, in the colour the glass is over
 * black, half the tint, so a bright pixel of the look cannot come up
 * behind a letter, and over the dark of a look the two are one pane. */
static uint32_t rgb(const struct theme *t, int role, uint32_t fallback)
{
	return t->c[role] == THEME_DEFAULT ? fallback : (uint32_t)t->c[role];
}

static uint32_t mix(uint32_t a, uint32_t b, double k)
{
	uint32_t o = 0;
	for (int sh = 0; sh <= 16; sh += 8) {
		double x = (a >> sh & 0xff) * (1 - k) + (b >> sh & 0xff) * k;
		o |= (uint32_t)(x + 0.5) << sh;
	}
	return o;
}

static uint32_t half(uint32_t c) { return c >> 1 & 0x7f7f7f; }

/* the glass's tint, the theme's background (the veil the stage's words
 * are on), and the selection's */
static uint32_t tint(const struct theme *t) { return rgb(t, TH_BG, 0x000000); }
static uint32_t bar_tint(const struct theme *t) { return mix(tint(t), rgb(t, TH_VIOLET, 0x8060c0), 0.75); }

/* Words from here on: in `fg`, solid on what `under` (a tint) is over black. */
static void ink(struct ncplane *n, uint32_t fg, uint32_t under)
{
	ncplane_set_fg_rgb(n, fg);
	ncplane_set_fg_alpha(n, NCALPHA_OPAQUE);
	ncplane_set_bg_rgb(n, half(under));
	ncplane_set_bg_alpha(n, NCALPHA_OPAQUE);
}

/* A role's colour on the glass: dim is lifted, as it would be lost on
 * the glass over black. */
static uint32_t role(const struct theme *t, int r)
{
	if (r == TH_DIM) return mix(rgb(t, TH_FG, 0xc0c0c0), half(tint(t)), 0.45);
	return rgb(t, r, 0xc0c0c0);
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

/* The selection's row: violet glass across it (its gaps hide the look's
 * glyphs, as a bar should). */
static void bar(struct ncplane *n, const struct theme *t, int y, unsigned cols)
{
	ncplane_set_bg_rgb(n, bar_tint(t));
	ncplane_set_bg_alpha(n, NCALPHA_BLEND);
	ncplane_set_fg_alpha(n, NCALPHA_BLEND);
	for (unsigned x = 0; x < cols; x++) ncplane_putstr_yx(n, y, (int)x, " ");
}

static void item(struct overlay *o, const struct theme *t, size_t i, int y, unsigned cols, int numw)
{
	struct ncplane *n = o->n;
	const struct tui_item *it = &o->q.v[i];
	int sel = i == o->sel;
	uint32_t under = sel ? bar_tint(t) : tint(t);
	if (sel) bar(n, t, y, cols);
	int end = (int)cols - 1;
	/* one label from the marker to the end of the name: its gaps solid too,
	 * so the look's glyphs do not come up between its parts */
	ink(n, role(t, TH_MAGENTA), under);
	ncplane_putstr_yx(n, y, 1, it->cursor ? "▶ " : "  ");
	char num[24], name[256];
	snprintf(num, sizeof(num), "%*ld  ", numw, it->pos);
	ink(n, sel ? role(t, TH_FG) : role(t, TH_DIM), under);
	int x = put(n, y, 3, num, end);
	int dw = it->duration[0] ? cols_of(it->duration) + 2 : 0;
	if (dw) { /* with a space before it, solid */
		char d[40];
		snprintf(d, sizeof(d), " %s", it->duration);
		put(n, y, end - dw + 1, d, end);
	}
	strip_item_name(it, name, sizeof(name));
	ink(n, role(t, TH_FG), under);
	x = put(n, y, x, name, end - dw);
	if (it->title[0] && it->artist[0] && x + 4 < end - dw) {
		ink(n, sel ? role(t, TH_FG) : role(t, TH_DIM), under);
		x = put(n, y, x, " — ", end - dw);
		ink(n, sel ? role(t, TH_FG) : role(t, TH_VIOLET), under);
		put(n, y, x, it->artist, end - dw);
	}
}

void overlay_draw(struct overlay *o, const struct theme *t, const char *msg, unsigned rows, unsigned cols)
{
	struct ncplane *n = o->n;
	unsigned r, c;
	ncplane_dim_yx(n, &r, &c);
	if (r != rows || c != cols) ncplane_resize_simple(n, rows, cols);
	uint64_t base = 0;
	if (o->open) { /* the glass: an empty glyph, so the look's show; fg and bg blended with the tint */
		ncchannels_set_fg_rgb(&base, tint(t));
		ncchannels_set_fg_alpha(&base, NCALPHA_BLEND);
		ncchannels_set_bg_rgb(&base, tint(t));
		ncchannels_set_bg_alpha(&base, NCALPHA_BLEND);
	} else {
		ncchannels_set_fg_alpha(&base, NCALPHA_TRANSPARENT);
		ncchannels_set_bg_alpha(&base, NCALPHA_TRANSPARENT);
	}
	ncplane_set_base(n, "", 0, base);
	ncplane_erase(n);
	if (!o->open || !rows || !cols) return;
	ncplane_set_styles(n, NCSTYLE_NONE);
	uint32_t glass = tint(t);
	int head = rows >= 4, line = o->adding && rows >= 2;
	int list = (int)rows - head - line, y = 0, end = (int)cols - 1;
	char count[48];
	snprintf(count, sizeof(count), "%zu/%zu%s", o->q.n ? o->sel + 1 : 0, o->q.n, o->q.more ? "+" : "");

	if (head) {
		ink(n, role(t, TH_MAGENTA), glass);
		int x = put(n, 0, 1, "queue ", end);
		ink(n, role(t, TH_DIM), glass);
		x = put(n, 0, x, count, end) + 3;
		const char *keys = msg && msg[0] ? msg : "j k move · ⏎ play · a add · d remove · tab close";
		int kw = cols_of(keys);
		ink(n, msg && msg[0] ? role(t, TH_ORANGE) : role(t, TH_DIM), glass);
		put(n, 0, end - kw > x ? end - kw : x, keys, end);
		y = 1;
	}
	if (list > 0) {
		if (!o->q.n) {
			ink(n, role(t, TH_DIM), glass);
			put(n, y, 3, head || line ? "the queue is empty" : "the queue is empty · a add · tab close", end);
		} else {
			o->top = overlay_top(o->sel, o->top, o->q.n, list);
			int numw = snprintf(NULL, 0, "%ld", o->q.v[o->q.n - 1].pos);
			for (int i = 0; i < list && o->top + (size_t)i < o->q.n; i++)
				item(o, t, o->top + (size_t)i, y + i, cols, numw);
		}
	}
	if (o->adding) { /* the add: line, at the bottom, or the only row */
		int ay = (int)rows - 1;
		if (rows == 1) ncplane_erase(n);
		ink(n, role(t, TH_CYAN), glass);
		int x = put(n, ay, 1, "add: ", end);
		ink(n, role(t, TH_FG), glass);
		int w = cols_of(o->input);
		/* the end of what is typed stays in view */
		const char *s = o->input;
		while (w > end - x - 1 && *s) {
			s++;
			while ((*s & 0xc0) == 0x80) s++;
			w = cols_of(s);
		}
		x = put(n, ay, x, s, end);
		ink(n, role(t, TH_CYAN), glass);
		ncplane_putstr_yx(n, ay, x, "▏");
	}
}
