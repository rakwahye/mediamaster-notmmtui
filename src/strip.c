/* SPDX-License-Identifier: LGPL-2.1-or-later */
/** @file strip.c
 * @brief One row: state, title, artist, time; plain or as a powerline. */

#include "strip.h"
#include "paths.h"
#include <math.h>
#include <notcurses/notcurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define LIT       0.62 /* how far the played part goes from bg toward the sky */
#define LIT_PAUSE 0.44 /* paused, at the top of a breath */
#define UNLIT     0.14
#define GLOW      0.55 /* the playhead's glow toward fg, at its peak */
#define BRIGHT    0.55 /* luma above which text is inked dark */
#define PLASMA_PAUSE 0.45 /* how bright plasma gets while paused, at the top of a breath */
#define BREATH_LOW   0.55 /* and how far down a breath goes, of that */
#define SETTLE_FIRST 0.13 /* s: the first letter's moment */
#define SETTLE_EACH  0.035
#define METER_DB     48.0 /* the meter's range, below full scale */

struct strip {
	struct ncplane *base, *text;
};

void strip_clock(char *buf, size_t cap, double s)
{
	unsigned long t = s > 0 ? (unsigned long)s : 0;
	if (t >= 3600) snprintf(buf, cap, "%lu:%02lu:%02lu", t / 3600, t / 60 % 60, t % 60);
	else snprintf(buf, cap, "%lu:%02lu", t / 60, t % 60);
}

static const char *const LOOK[STRIP_LOOKS] = {
	[STRIP_POWERLINE] = "powerline", [STRIP_PLASMA] = "plasma", [STRIP_PLAIN] = "plain",
};

const char *strip_look_name(int look) { return look >= 0 && look < STRIP_LOOKS ? LOOK[look] : "?"; }

int strip_look_find(const char *name)
{
	for (int l = 0; l < STRIP_LOOKS; l++)
		if (!strcmp(name, LOOK[l])) return l;
	return -1;
}

int strip_look_moves(int look) { return look == STRIP_PLASMA; }

int strip_settled(size_t i, double age) { return age >= SETTLE_FIRST + SETTLE_EACH * (double)i; }

double strip_decode_time(size_t n) { return SETTLE_FIRST + SETTLE_EACH * (double)n; }

int strip_needs_frames(const struct strip_model *m)
{
	if (!m->daemon || m->msg[0] || m->refused[0]) return 0;
	if (m->st.state == TUI_PAUSED) return 1;
	if (m->st.state != TUI_PLAYING) return 0;
	return strip_look_moves(m->look) || m->decode < STRIP_SETTLED;
}

/* A breath: the low point at 0, the full at 1. */
static double breathe(double low, double breath) { return low + (1.0 - low) * breath; }

/* ------------------------------------------------------------------ colour */

static uint32_t rgb_mix(uint32_t a, uint32_t b, double k)
{
	if (k < 0) k = 0;
	if (k > 1) k = 1;
	uint32_t o = 0;
	for (int sh = 0; sh <= 16; sh += 8) {
		double ca = (a >> sh) & 255, cb = (b >> sh) & 255;
		o |= (uint32_t)lround(ca + (cb - ca) * k) << sh;
	}
	return o;
}

static double luma(uint32_t c)
{
	return (0.2126 * (c >> 16 & 255) + 0.7152 * (c >> 8 & 255) + 0.0722 * (c & 255)) / 255.0;
}

static int has_colours(const struct theme *t)
{
	static const int need[] = { TH_BG, TH_FG, TH_ORANGE, TH_MAGENTA, TH_VIOLET, TH_CYAN };
	for (size_t i = 0; i < sizeof(need) / sizeof(need[0]); i++)
		if (t->c[need[i]] == THEME_DEFAULT) return 0;
	return 1;
}

uint32_t strip_sky(const struct theme *t, double x)
{
	uint32_t o = (uint32_t)t->c[TH_ORANGE], m = (uint32_t)t->c[TH_MAGENTA];
	uint32_t v = (uint32_t)t->c[TH_VIOLET], c = (uint32_t)t->c[TH_CYAN];
	if (x < 0.18) return rgb_mix(o, m, x / 0.18);
	if (x < 0.55) return rgb_mix(m, v, (x - 0.18) / 0.37);
	return rgb_mix(v, c, (x - 0.55) / 0.45);
}

uint32_t strip_plasma(const struct theme *t, double x, double y, double time)
{
	double u = x / 4.0, w = y * 1.1;
	double v = sin(u + time * 1.9) + sin(u * 0.7 + w - time * 1.3) +
	           sin(sqrt(u * u * 0.08 + w * w + 1.0) * 2.0 - time * 2.6) + sin(u * 0.33 - w + time);
	v = (v + 4.0) / 8.0; /* 0..1 */
	double hue = fmod(v * 1.6 + time * 0.04, 1.0);
	return rgb_mix((uint32_t)t->c[TH_BG], strip_sky(t, hue), 0.30 + 0.62 * v);
}

uint32_t strip_ink(const struct theme *t, uint32_t under)
{
	return luma(under) > BRIGHT ? (uint32_t)t->c[TH_BG] : (uint32_t)t->c[TH_FG];
}

int strip_meter_eighths(uint8_t peak)
{
	if (!peak) return 0;
	double db = 20.0 * log10(peak / 255.0);
	double h = (db + METER_DB) / METER_DB;
	if (h <= 0) return 0;
	return h >= 1 ? 8 : (int)lround(h * 8);
}

int strip_bar(unsigned cols, double pos, double duration)
{
	if (duration <= 0 || !cols) return -1;
	double f = pos / duration;
	if (f < 0) f = 0;
	if (f > 1) f = 1;
	return (int)lround(f * cols * 8);
}

/* ------------------------------------------------------------------ text */

/* How text is coloured: its role, or, over a lit cell, inked to read. */
struct pen {
	struct ncplane *n;
	const struct theme *t;
	int role;
	const uint32_t *lit; /* per column: the lit colour under it, 0 = not lit; NULL in plain */
	int veil;            /* letters sit on the theme's bg blended over what is below */
	double age;          /* s since the track changed, for letters that decode; < 0: they don't */
	size_t k;            /* letters put so far, while decoding */
	int32_t fg;          /* >= 0: this colour, not the role's (a breath) */
};

static const char *const NOISE[] = { "▖", "▗", "▘", "▚", "▞", "░", "▒", "/", "\\", "<", ">", "#", "%", "*", "+", "=" };

static void pen_at(const struct pen *p, int x)
{
	if (p->veil) {
		ncplane_set_bg_rgb(p->n, (uint32_t)p->t->c[TH_BG]);
		ncplane_set_bg_alpha(p->n, NCALPHA_BLEND);
	}
	if (p->lit && p->lit[x]) ncplane_set_fg_rgb(p->n, strip_ink(p->t, p->lit[x]));
	else if (p->fg >= 0) ncplane_set_fg_rgb(p->n, (uint32_t)p->fg);
	else if (p->t->c[p->role] == THEME_DEFAULT) ncplane_set_fg_default(p->n);
	else ncplane_set_fg_rgb(p->n, (uint32_t)p->t->c[p->role]);
}

/* Put s at (y, x), no further than column `end`; a cut string ends in
 * "…". Control characters (a tag may hold a tab) show as spaces.
 * @returns the column after the last cell written. */
static int put_clipped(struct pen *pn, int y, int x, const char *s, int end)
{
	struct ncplane *n = pn->n;
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
			if (col < end) {
				pen_at(pn, col);
				ncplane_putstr_yx(n, y, col++, "…");
			}
			return col;
		}
		char egc[8];
		memcpy(egc, ctl ? " " : p, ctl ? 1 : len);
		egc[ctl ? 1 : len] = '\0';
		const char *put = egc;
		if (pn->age >= 0 && w == 1 && wc != ' ' && !strip_settled(pn->k++, pn->age)) {
			/* not yet: noise, changing every frame, in cyan */
			put = NOISE[(pn->k * 7 + (size_t)(pn->age * 15)) % (sizeof(NOISE) / sizeof(NOISE[0]))];
			struct pen cy = *pn;
			cy.role = TH_CYAN;
			cy.fg = -1;
			pen_at(&cy, col);
		} else {
			pen_at(pn, col);
		}
		ncplane_putstr_yx(n, y, col, put);
		col += w;
		p += len;
	}
	return col;
}

static int say(struct pen *p, int role, int x, const char *s, int end)
{
	p->role = role;
	return put_clipped(p, 0, x, s, end);
}

/* The row's words, wherever the ground came from. */
static void row(struct pen *p, const struct strip_model *m, unsigned cols)
{
	const struct theme *t = p->t;
	if (m->refused[0]) {
		say(p, TH_ORANGE, 0, m->refused, (int)cols);
		return;
	}
	if (m->msg[0]) {
		say(p, TH_ORANGE, 0, m->msg, (int)cols);
		return;
	}
	if (!m->daemon) {
		char line[300];
		snprintf(line, sizeof(line), "mediamaster is not running (%s)", mm_paths_socket());
		say(p, TH_DIM, 0, line, (int)cols);
		return;
	}
	const struct tui_status *st = &m->st;
	if (st->state == TUI_STOPPED) {
		say(p, TH_DIM, 0, "■ stopped", (int)cols);
		return;
	}

	/* the right side: "live 0:12" or "1:23 / 6:53" */
	char now[16], dur[16], right[40];
	strip_clock(now, sizeof(now), m->pos);
	int live = st->duration <= 0;
	if (live) {
		snprintf(right, sizeof(right), " live %s", now);
	} else {
		strip_clock(dur, sizeof(dur), st->duration);
		snprintf(right, sizeof(right), " %s / %s", now, dur);
	}
	int end = (int)cols - (int)strlen(right);
	if (end < 4) { /* too narrow for both: the time wins */
		say(p, TH_CYAN, 0, now, (int)cols);
		return;
	}
	int at = end; /* where the time starts */
	/* the meter, just before the time, when there is room for it */
	if (m->levels && end >= 16) {
		static const char *const H[9] = { " ", "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█" };
		end -= 3; /* " LR" */
		say(p, TH_CYAN, end + 1, H[strip_meter_eighths(m->lv.peak[0])], end + 2);
		say(p, TH_MAGENTA, end + 2, H[strip_meter_eighths(m->lv.peak[1])], end + 3);
	}

	int paused = st->state == TUI_PAUSED;
	if (paused && !p->lit && t->c[TH_ORANGE] != THEME_DEFAULT && t->c[TH_DIM] != THEME_DEFAULT)
		p->fg = (int32_t)rgb_mix((uint32_t)t->c[TH_DIM], (uint32_t)t->c[TH_ORANGE], m->breath);
	int x = say(p, paused ? TH_ORANGE : TH_MAGENTA, 0, paused ? "‖ " : "▶ ", end);
	p->fg = -1;
	p->age = m->decode < STRIP_SETTLED ? m->decode : -1;
	p->k = 0;
	if (t->bold_title) ncplane_set_styles(p->n, NCSTYLE_BOLD);
	x = say(p, TH_FG, x, st->title[0] ? st->title : st->uri, end);
	ncplane_set_styles(p->n, NCSTYLE_NONE);
	if (st->artist[0] && x < end - 4) {
		x = say(p, TH_DIM, x, " — ", end);
		say(p, TH_VIOLET, x, st->artist, end);
	}
	p->age = -1;
	if (live) {
		x = say(p, TH_GREEN, at, " live", (int)cols);
		say(p, TH_CYAN, x, right + 5, (int)cols);
	} else { /* what has played in cyan, the rest dim */
		const char *slash = strchr(right, '/');
		char head[24];
		snprintf(head, sizeof(head), "%.*s", (int)(slash - right), right);
		x = say(p, TH_CYAN, at, head, (int)cols);
		say(p, TH_DIM, x, slash, (int)cols);
	}
}

/* ------------------------------------------------------------------ ground */

static void set_bg(struct ncplane *n, const struct theme *t, int role)
{
	if (t->c[role] == THEME_DEFAULT) ncplane_set_bg_default(n);
	else ncplane_set_bg_rgb(n, (uint32_t)t->c[role]);
}

static void plain_ground(struct ncplane *n, const struct theme *t, unsigned rows, unsigned cols)
{
	set_bg(n, t, TH_BG);
	for (unsigned y = 0; y < rows; y++)
		for (unsigned x = 0; x < cols; x++) ncplane_putchar_yx(n, (int)y, (int)x, ' ');
}

/* The bar across row 0; lit[x] gets the lit colour of each played cell. */
static void powerline_ground(struct ncplane *n, const struct strip_model *m, const struct theme *t,
                             unsigned rows, unsigned cols, uint32_t *lit)
{
	plain_ground(n, t, rows, cols);
	uint32_t bg = (uint32_t)t->c[TH_BG], fg = (uint32_t)t->c[TH_FG];
	int bar = m->daemon && m->st.state != TUI_STOPPED && !m->msg[0] ? m->bar : -1;
	int full = bar < 0 ? 0 : bar / 8, part = bar < 0 ? 0 : bar % 8;
	double k = m->st.state == TUI_PAUSED ? LIT_PAUSE * breathe(BREATH_LOW, m->breath) : LIT;
	double head = bar < 0 ? -100 : bar / 8.0;
	/* The playhead cell, lit `part` eighths from the left. Under half, a
	 * left block of lit on the dark ground; from half on, the lit ground
	 * with a right block of dark over the rest, so text there sits on lit,
	 * as its ink expects (a glyph above hides the block, not the ground). */
	static const char *const LEFT[4] = { " ", "▏", "▎", "▍" };
	static const char *const RIGHT[8] = { [4] = "▐", [5] = "\U0001FB88", [6] = "\U0001FB87", [7] = "▕" };
	for (unsigned x = 0; x < cols; x++) {
		uint32_t sky = strip_sky(t, (x + 0.5) / cols);
		uint32_t off = rgb_mix(bg, sky, UNLIT), on = rgb_mix(bg, sky, k);
		if (m->st.state == TUI_PLAYING) {
			double d = (x + 0.5) - head;
			on = rgb_mix(on, fg, exp(-d * d / 3.0) * GLOW);
		}
		lit[x] = 0;
		if ((int)x < full) {
			ncplane_set_bg_rgb(n, on);
			ncplane_putchar_yx(n, 0, (int)x, ' ');
			lit[x] = on;
		} else if ((int)x == full && part) {
			ncplane_set_fg_rgb(n, part < 4 ? on : off);
			ncplane_set_bg_rgb(n, part < 4 ? off : on);
			ncplane_putstr_yx(n, 0, (int)x, part < 4 ? LEFT[part] : RIGHT[part]);
			if (part >= 4) lit[x] = on;
		} else {
			ncplane_set_bg_rgb(n, off);
			ncplane_putchar_yx(n, 0, (int)x, ' ');
		}
	}
}

/* Plasma behind what has played (all of the row when live), dots ahead.
 * lit[x] gets what a letter's veil will blend to, for its ink. */
static void plasma_ground(struct ncplane *n, const struct strip_model *m, const struct theme *t,
                          unsigned rows, unsigned cols, uint32_t *lit)
{
	plain_ground(n, t, rows, cols);
	uint32_t bg = (uint32_t)t->c[TH_BG];
	int going = m->daemon && m->st.state != TUI_STOPPED && !m->msg[0];
	int upto = !going ? 0 : m->bar < 0 ? (int)cols : (m->bar + 4) / 8;
	double k = m->st.state == TUI_PAUSED ? PLASMA_PAUSE * breathe(BREATH_LOW, m->breath) : 1.0;
	for (unsigned x = 0; x < cols; x++) {
		lit[x] = 0;
		if ((int)x >= upto) {
			if (x % 3 == 2 && going) {
				ncplane_set_bg_rgb(n, bg);
				ncplane_set_fg_rgb(n, (uint32_t)t->c[TH_DIM]);
				ncplane_putstr_yx(n, 0, (int)x, "·");
			}
			continue;
		}
		uint32_t top = rgb_mix(bg, strip_plasma(t, x, 0, m->anim), k);
		uint32_t low = rgb_mix(bg, strip_plasma(t, x, 1, m->anim), k);
		ncplane_set_fg_rgb(n, top);
		ncplane_set_bg_rgb(n, low);
		ncplane_putstr_yx(n, 0, (int)x, "▀");
		lit[x] = rgb_mix(low, bg, 0.5); /* the veil over it */
	}
}

/* ------------------------------------------------------------------ strip */

struct strip *strip_new(struct ncplane *base)
{
	struct strip *s = calloc(1, sizeof(*s));
	if (!s) return NULL;
	unsigned rows, cols;
	ncplane_dim_yx(base, &rows, &cols);
	struct ncplane_options po = { .rows = rows ? rows : 1, .cols = cols ? cols : 1 };
	s->base = base;
	s->text = ncplane_create(base, &po);
	if (!s->text) {
		free(s);
		return NULL;
	}
	uint64_t clear = 0;
	ncchannels_set_fg_alpha(&clear, NCALPHA_TRANSPARENT);
	ncchannels_set_bg_alpha(&clear, NCALPHA_TRANSPARENT);
	ncplane_set_base(s->text, "", 0, clear);
	return s;
}

void strip_free(struct strip *s)
{
	if (!s) return;
	ncplane_destroy(s->text);
	free(s);
}

void strip_draw(struct strip *s, const struct strip_model *m, const struct theme *t)
{
	unsigned rows, cols, tr, tc;
	ncplane_dim_yx(s->base, &rows, &cols);
	if (!rows || !cols) return;
	ncplane_dim_yx(s->text, &tr, &tc);
	if (tr != rows || tc != cols) ncplane_resize_simple(s->text, rows, cols);

	ncplane_erase(s->base);
	ncplane_set_styles(s->base, NCSTYLE_NONE);
	uint32_t *lit = NULL;
	int look = has_colours(t) ? m->look : STRIP_PLAIN;
	if (look != STRIP_PLAIN) lit = calloc(cols, sizeof(*lit));
	if (lit && look == STRIP_POWERLINE) powerline_ground(s->base, m, t, rows, cols, lit);
	else if (lit && look == STRIP_PLASMA) plasma_ground(s->base, m, t, rows, cols, lit);
	else plain_ground(s->base, t, rows, cols);

	ncplane_erase(s->text);
	ncplane_set_styles(s->text, NCSTYLE_NONE);
	ncplane_set_bg_alpha(s->text, NCALPHA_TRANSPARENT);
	struct pen p = { .n = s->text, .t = t, .lit = lit, .veil = lit && look == STRIP_PLASMA, .age = -1, .fg = -1 };
	row(&p, m, cols);
	free(lit);
}
