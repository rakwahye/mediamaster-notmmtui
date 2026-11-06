/* SPDX-License-Identifier: LGPL-2.1-or-later */
/** @file strip.c
 * @brief The strip: state, title, artist, time, on the ground of a look. */

#include "strip.h"
#include "ipc.h"
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
#define PHOSPHOR     0.12 /* s: how fast an old trace fades (by the look's clock) */
#define AFTERGLOW    0.45 /* how bright a trace is once it is no longer the newest */
#define TRACE_TALL   0.92 /* of the scope's half height, at full level */
#define SCOPE_COLS   48   /* narrower than this, two rows of words and no scope */

struct strip {
	struct ncplane *base, *text;
	/* the scope's phosphor: a glow 0..1 a dot, as of the look's clock at `at` */
	float *glow;
	int gw, gh;
	double at;
};

void strip_clock(char *buf, size_t cap, double s)
{
	unsigned long t = s > 0 ? (unsigned long)s : 0;
	if (t >= 3600) snprintf(buf, cap, "%lu:%02lu:%02lu", t / 3600, t / 60 % 60, t % 60);
	else snprintf(buf, cap, "%lu:%02lu", t / 60, t % 60);
}

static const char *const LOOK[STRIP_LOOKS] = {
	[STRIP_POWERLINE] = "powerline", [STRIP_PLASMA] = "plasma", [STRIP_SCOPE] = "scope", [STRIP_PLAIN] = "plain",
};

const char *strip_look_name(int look) { return look >= 0 && look < STRIP_LOOKS ? LOOK[look] : "?"; }

int strip_look_find(const char *name)
{
	for (int l = 0; l < STRIP_LOOKS; l++)
		if (!strcmp(name, LOOK[l])) return l;
	return -1;
}

int strip_look_moves(int look) { return look == STRIP_PLASMA || look == STRIP_SCOPE; }

int strip_look_rows(int look) { return look == STRIP_SCOPE ? 2 : 1; }

int strip_look_for(unsigned rows) { return rows >= 2 ? STRIP_SCOPE : STRIP_POWERLINE; }

int strip_look_next(int look, unsigned rows)
{
	for (int i = 1; i <= STRIP_LOOKS; i++) {
		int l = (look + i) % STRIP_LOOKS;
		if ((unsigned)strip_look_rows(l) <= rows) return l;
	}
	return look;
}

int strip_settled(size_t i, double age) { return age >= SETTLE_FIRST + SETTLE_EACH * (double)i; }

double strip_decode_time(size_t n) { return SETTLE_FIRST + SETTLE_EACH * (double)n; }

int strip_needs_frames(const struct strip_model *m)
{
	if (!m->daemon || m->msg[0] || m->refused[0]) return 0;
	if (m->st.state == TUI_PAUSED) return 1;
	if (m->st.state != TUI_PLAYING) return 0;
	int moving = m->levels && m->look != STRIP_PLAIN && (m->rx.level > 0.01f || m->rx.beat > 0.01f);
	return strip_look_moves(m->look) || m->decode < STRIP_SETTLED || moving;
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

int strip_meter_steps(uint8_t peak, int steps)
{
	if (!peak) return 0;
	double db = 20.0 * log10(peak / 255.0);
	double h = (db + METER_DB) / METER_DB;
	if (h <= 0) return 0;
	return h >= 1 ? steps : (int)lround(h * steps);
}

int strip_meter_eighths(uint8_t peak) { return strip_meter_steps(peak, 8); }

double strip_scope_wave(const float *band, double u, double t)
{
	double s = 0, w = 0;
	for (int k = 0; k < TUI_BANDS; k++) {
		if (band[k] <= 0) continue;
		double cycles = 1.0 + 0.75 * k, speed = 0.35 + 0.09 * k;
		double phase = 0.618 * k; /* of a turn: the bands do not all start together */
		s += band[k] * sin(2 * M_PI * (cycles * u - speed * t + phase));
		w += band[k] * band[k];
	}
	/* over the bands' RMS weight: one band alone is a whole sine, many
	 * together swing wider and are clipped softly rather than cancelling */
	return w > 1e-9 ? tanh(2.0 * s / sqrt(w)) : 0;
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
	int y;               /* the row */
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
	return put_clipped(p, p->y, x, s, end);
}

static int text_cols(const char *s)
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

/* Where the scope's parts go across `cols`: the words in [0, words), the
 * meter's two cells at meter, the scope from scope on; no meter or scope
 * when meter < 0 (too narrow). */
struct scope_at {
	int words, meter, scope;
};

static struct scope_at scope_at(unsigned cols)
{
	struct scope_at a = { (int)cols, -1, (int)cols };
	if (cols < SCOPE_COLS) return a;
	int w = (int)cols * 2 / 5;
	a.words = w < 28 ? 28 : w > 44 ? 44 : w;
	a.meter = a.words + 1;
	a.scope = a.meter + 3;
	return a;
}

/* The scope's words: state and title over the time played, artist over
 * the duration (or live), and the meter, 16 steps a channel over two rows. */
static void two_rows(struct pen *p, const struct strip_model *m, unsigned cols)
{
	const struct theme *t = p->t;
	const struct tui_status *st = &m->st;
	struct scope_at a = scope_at(cols);
	char now[16], dur[16];
	strip_clock(now, sizeof(now), m->pos);
	int live = st->duration <= 0;
	if (live) snprintf(dur, sizeof(dur), "live");
	else strip_clock(dur, sizeof(dur), st->duration);
	int w = text_cols(now) > text_cols(dur) ? text_cols(now) : text_cols(dur);
	int end = a.words - w - 1;
	if (end < 4) { /* the time wins */
		say(p, TH_CYAN, 0, now, a.words);
		return;
	}
	if (a.meter >= 0 && m->levels) {
		static const char *const H[9] = { " ", "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█" };
		for (int ch = 0; ch < 2; ch++) {
			int e = strip_meter_steps(m->lv.peak[ch], 16);
			int lo = e > 8 ? 8 : e, hi = e > 8 ? e - 8 : 0;
			int role = ch ? TH_MAGENTA : TH_CYAN;
			p->y = 0;
			say(p, role, a.meter + ch, H[hi], a.meter + ch + 1);
			p->y = 1;
			say(p, role, a.meter + ch, H[lo], a.meter + ch + 1);
		}
	}

	int paused = st->state == TUI_PAUSED;
	p->y = 0;
	if (paused && t->c[TH_ORANGE] != THEME_DEFAULT && t->c[TH_DIM] != THEME_DEFAULT)
		p->fg = (int32_t)rgb_mix((uint32_t)t->c[TH_DIM], (uint32_t)t->c[TH_ORANGE], m->breath);
	int x = say(p, paused ? TH_ORANGE : TH_MAGENTA, 0, paused ? "‖ " : "▶ ", end);
	p->fg = -1;
	p->age = m->decode < STRIP_SETTLED ? m->decode : -1;
	p->k = 0;
	if (t->bold_title) ncplane_set_styles(p->n, NCSTYLE_BOLD);
	say(p, TH_FG, x, st->title[0] ? st->title : st->uri, end);
	ncplane_set_styles(p->n, NCSTYLE_NONE);
	if (st->artist[0]) {
		p->y = 1;
		say(p, TH_VIOLET, x, st->artist, end);
	}
	p->age = -1;
	p->y = 0;
	say(p, TH_CYAN, a.words - text_cols(now), now, a.words);
	p->y = 1;
	say(p, live ? TH_GREEN : TH_DIM, a.words - text_cols(dur), dur, a.words);
	p->y = 0;
}

/* The words, wherever the ground came from: on one row, or on two for
 * the scope. */
static void row(struct pen *p, const struct strip_model *m, unsigned cols, int two)
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
		snprintf(line, sizeof(line), "mediamaster is not running (%s)", tui_socket());
		say(p, TH_DIM, 0, line, (int)cols);
		return;
	}
	const struct tui_status *st = &m->st;
	if (st->state == TUI_STOPPED) {
		say(p, TH_DIM, 0, "■ stopped", (int)cols);
		return;
	}
	if (two) {
		two_rows(p, m, cols);
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
	/* with levels: the glow as wide as the bass, flaring on a beat; lit with the level */
	double width = 3.0, glow = GLOW;
	if (m->levels && m->st.state == TUI_PLAYING) {
		width = 2.0 + 14.0 * m->rx.bass;
		glow = GLOW * (0.55 + 0.45 * m->rx.level + 0.6 * m->rx.beat);
		k = LIT * (0.80 + 0.35 * m->rx.level);
	}
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
			on = rgb_mix(on, fg, exp(-d * d / width) * glow);
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
	double flash = 0; /* with levels: as bright as the bass, a flash on a beat */
	if (m->levels && m->st.state == TUI_PLAYING) {
		k = 0.70 + 0.30 * m->rx.bass;
		flash = 0.35 * m->rx.beat;
	}
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
		uint32_t top = rgb_mix(rgb_mix(bg, strip_plasma(t, x, 0, m->anim), k), (uint32_t)t->c[TH_FG], flash);
		uint32_t low = rgb_mix(rgb_mix(bg, strip_plasma(t, x, 1, m->anim), k), (uint32_t)t->c[TH_FG], flash);
		ncplane_set_fg_rgb(n, top);
		ncplane_set_bg_rgb(n, low);
		ncplane_putstr_yx(n, 0, (int)x, "▀");
		lit[x] = rgb_mix(low, bg, 0.5); /* the veil over it */
	}
}

/* The scope: its dots, a glow each, drawn with the finest blitter the
 * terminal has. The trace is laid on while playing; what was there fades
 * by the look's clock, so a pause holds it. */
static void scope_ground(struct strip *s, const struct strip_model *m, const struct theme *t,
                         unsigned rows, unsigned cols)
{
	struct ncplane *n = s->base;
	plain_ground(n, t, rows, cols);
	struct scope_at a = scope_at(cols);
	int going = m->daemon && !m->msg[0] && !m->refused[0] &&
	            (m->st.state == TUI_PLAYING || m->st.state == TUI_PAUSED);
	if (a.meter < 0 || !going) {
		if (s->glow) memset(s->glow, 0, sizeof(float) * (size_t)s->gw * (size_t)s->gh);
		return;
	}
	struct notcurses *nc = ncplane_notcurses(n);
	ncblitter_e blit = NCBLIT_2x1;
	int dy = 2, dx = 1;
	if (notcurses_canoctant(nc)) blit = NCBLIT_4x2, dy = 4, dx = 2;
	else if (notcurses_canquadrant(nc)) blit = NCBLIT_2x2, dy = 2, dx = 2;
	int W = ((int)cols - a.scope) * dx, H = (int)rows * dy;
	if (W <= 0 || H <= 0) return;
	if (W != s->gw || H != s->gh) {
		free(s->glow);
		s->glow = calloc((size_t)W * (size_t)H, sizeof(float));
		s->gw = s->glow ? W : 0;
		s->gh = s->glow ? H : 0;
		s->at = m->anim;
		if (!s->glow) return;
	}
	double since = m->anim - s->at;
	s->at = m->anim;
	float keep = since < 0 || since > 2 ? 0.0f : (float)exp(-since / PHOSPHOR);
	for (int i = 0; i < W * H; i++) s->glow[i] = s->glow[i] * keep < 0.03f ? 0 : s->glow[i] * keep;

	if (m->st.state == TUI_PLAYING) {
		double tall = TRACE_TALL * (m->levels ? m->rx.level : 0);
		int prev = -1;
		for (int x = 0; x < W; x++) {
			double v = tall * strip_scope_wave(m->rx.band, (x + 0.5) / W, m->anim);
			int y = (int)lround((1.0 - (v + 1.0) / 2.0) * (H - 1));
			y = y < 0 ? 0 : y >= H ? H - 1 : y;
			int lo = prev < 0 ? y : prev < y ? prev : y, hi = prev < 0 ? y : prev > y ? prev : y;
			for (int yy = lo; yy <= hi; yy++) s->glow[yy * W + x] = 1.0f;
			prev = y;
		}
	}

	uint32_t bg = (uint32_t)t->c[TH_BG], fg = (uint32_t)t->c[TH_FG], violet = (uint32_t)t->c[TH_VIOLET];
	double k = m->st.state == TUI_PAUSED ? breathe(BREATH_LOW, m->breath) : 1.0;
	uint32_t *px = malloc(sizeof(uint32_t) * (size_t)W * (size_t)H);
	if (!px) return;
	for (int x = 0; x < W; x++) {
		uint32_t hot = rgb_mix(strip_sky(t, (x + 0.5) / W), fg, 0.2 + 0.5 * (m->levels ? m->rx.beat : 0));
		for (int y = 0; y < H; y++) {
			double g = s->glow[y * W + x];
			/* the newest trace hot, in the sky; older ones fainter, sinking to violet */
			uint32_t c = g >= 1.0f ? rgb_mix(bg, hot, k)
			                       : rgb_mix(bg, rgb_mix(violet, hot, g * g), AFTERGLOW * g * k);
			/* RGBA in memory: R first */
			px[y * W + x] = 0xff000000u | (c & 0xff) << 16 | (c & 0xff00) | (c >> 16 & 0xff);
		}
	}
	struct ncvisual *v = ncvisual_from_rgba(px, H, W * 4, W);
	free(px);
	if (!v) return;
	struct ncvisual_options vo = { .n = n, .x = a.scope, .blitter = blit, .scaling = NCSCALE_NONE,
	                               .flags = NCVISUAL_OPTION_NODEGRADE };
	ncvisual_blit(nc, v, &vo);
	ncvisual_destroy(v);
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
	free(s->glow);
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
	if ((unsigned)strip_look_rows(look) > rows) look = STRIP_POWERLINE;
	if (look == STRIP_SCOPE) {
		scope_ground(s, m, t, rows, cols);
		ncplane_erase(s->text);
		ncplane_set_styles(s->text, NCSTYLE_NONE);
		ncplane_set_bg_alpha(s->text, NCALPHA_TRANSPARENT);
		struct pen p = { .n = s->text, .t = t, .age = -1, .fg = -1 };
		row(&p, m, cols, 1);
		return;
	}
	if (look != STRIP_PLAIN) lit = calloc(cols, sizeof(*lit));
	if (lit && look == STRIP_POWERLINE) powerline_ground(s->base, m, t, rows, cols, lit);
	else if (lit && look == STRIP_PLASMA) plasma_ground(s->base, m, t, rows, cols, lit);
	else plain_ground(s->base, t, rows, cols);

	ncplane_erase(s->text);
	ncplane_set_styles(s->text, NCSTYLE_NONE);
	ncplane_set_bg_alpha(s->text, NCALPHA_TRANSPARENT);
	struct pen p = { .n = s->text, .t = t, .lit = lit, .veil = lit && look == STRIP_PLASMA, .age = -1, .fg = -1 };
	row(&p, m, cols, 0);
	free(lit);
}
