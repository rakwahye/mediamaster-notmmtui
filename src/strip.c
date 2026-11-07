/* SPDX-License-Identifier: LGPL-2.1-or-later */
/** @file strip.c
 * @brief The strip: state, title, artist, time, on the ground of a look. */

#include "strip.h"
#include "font.h"
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
#define PEAK_HOLD    0.4  /* s a road peak holds before it falls */
#define PEAK_FALL    24.0 /* steps a second it falls */
#define REFLECT      0.30 /* how bright the reflection is, of its bar */

struct strip {
	struct ncplane *base, *text;
	/* the scope's phosphor: a glow 0..1 a dot, as of the look's clock at `at` */
	float *glow;
	int gw, gh;
	double at;
	/* the road's bars and peaks, in steps, and the peaks' time left to
	 * hold, as of `road_at`; a pause keeps the bars as they were */
	float *bars, *peak, *hold;
	int pn;
	double road_at;
};

void strip_clock(char *buf, size_t cap, double s)
{
	unsigned long t = s > 0 ? (unsigned long)s : 0;
	if (t >= 3600) snprintf(buf, cap, "%lu:%02lu:%02lu", t / 3600, t / 60 % 60, t % 60);
	else snprintf(buf, cap, "%lu:%02lu", t / 60, t % 60);
}

static const char *const LOOK[STRIP_LOOKS] = {
	[STRIP_POWERLINE] = "powerline", [STRIP_PLASMA] = "plasma", [STRIP_SCOPE] = "scope", [STRIP_ROAD] = "road", [STRIP_HORIZON] = "horizon", [STRIP_STAGE] = "stage",
	[STRIP_PLAIN] = "plain",
};

const char *strip_look_name(int look) { return look >= 0 && look < STRIP_LOOKS ? LOOK[look] : "?"; }

int strip_look_find(const char *name)
{
	for (int l = 0; l < STRIP_LOOKS; l++)
		if (!strcmp(name, LOOK[l])) return l;
	return -1;
}

int strip_look_moves(int look) { return look != STRIP_POWERLINE && look != STRIP_PLAIN; }

int strip_look_rows(int look)
{
	return look == STRIP_SCOPE ? 2 : look == STRIP_ROAD ? 3 : look == STRIP_HORIZON ? 4 : look == STRIP_STAGE ? 9 : 1;
}

int strip_look_for(unsigned rows)
{
	return rows >= 9 ? STRIP_STAGE : rows >= 4 ? STRIP_HORIZON : rows == 3 ? STRIP_ROAD
	     : rows == 2 ? STRIP_SCOPE : STRIP_POWERLINE;
}

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

double strip_road_bar(const float *band, double u)
{
	double at = u * TUI_BANDS - 0.5;
	if (at <= 0) return band[0];
	if (at >= TUI_BANDS - 1) return band[TUI_BANDS - 1];
	int i = (int)at;
	double f = at - i;
	return band[i] * (1 - f) + band[i + 1] * f;
}

void strip_road_peaks(float *peak, float *hold, const float *bar, int n, double dt)
{
	for (int i = 0; i < n; i++) {
		if (bar[i] >= peak[i]) {
			peak[i] = bar[i];
			hold[i] = PEAK_HOLD;
			continue;
		}
		double fall = dt - hold[i];
		hold[i] = fall > 0 ? 0 : (float)(hold[i] - dt);
		if (fall > 0) peak[i] = (float)(peak[i] - PEAK_FALL * fall);
		if (peak[i] < bar[i]) peak[i] = bar[i];
	}
}

double strip_horizon_tie(int i, int n, double t)
{
	double d = fmod((double)i / n + 0.35 * t, 1.0);
	return d < 0 ? d + 1 : d;
}

double strip_horizon_hill(const float *band, double d)
{
	return strip_road_bar(band, d < 0 ? 0 : d > 1 ? 1 : d);
}

int strip_stage_scale(unsigned rows)
{
	int sc = (int)rows / 11;
	return sc < 1 ? 1 : sc > 4 ? 4 : sc;
}

void strip_stage_sun(double p, double *x, double *alt)
{
	p = p < 0 ? 0 : p > 1 ? 1 : p;
	*x = 0.06 + 0.88 * p;
	*alt = sin(M_PI * p);
}

int strip_big_text(const char *s, char *out, size_t n, int max)
{
	int len = 0;
	for (const unsigned char *c = (const unsigned char *)s; *c; c++) {
		if (!font5x7(*c)) return 0;
		len++;
	}
	if (max < 1 || n < 2) return 0;
	int keep = len <= max ? len : max > 3 ? max - 3 : max;
	if ((size_t)keep >= n) keep = (int)n - 1;
	int i = 0;
	for (; i < keep; i++) out[i] = (char)(s[i] >= 'a' && s[i] <= 'z' ? s[i] - ('a' - 'A') : s[i]);
	if (len > max && max > 3)
		for (int d = 0; d < 3 && (size_t)i < n - 1; d++) out[i++] = '.';
	out[i] = '\0';
	return i;
}

void strip_item_name(const struct tui_item *it, char *out, size_t n)
{
	if (it->title[0]) {
		snprintf(out, n, "%s", it->title);
		return;
	}
	/* untitled: a file by its name, without folder or extension */
	const char *u = it->uri, *slash = strrchr(u, '/');
	if (!slash || !slash[1]) {
		snprintf(out, n, "%s", u);
		return;
	}
	const char *name = slash + 1, *dot = strrchr(name, '.');
	int len = dot && dot != name && strlen(dot) <= 6 ? (int)(dot - name) : (int)strlen(name);
	snprintf(out, n, "%.*s", len, name);
}

int strip_next(const struct tui_list *q, char next[][STRIP_NEXT_LEN], int max)
{
	size_t from = 0;
	for (size_t i = 0; i < q->n; i++)
		if (q->v[i].cursor) from = i + 1;
	int n = 0;
	if (max > STRIP_NEXT) max = STRIP_NEXT;
	for (size_t i = from; i < q->n && n < max; i++, n++) strip_item_name(&q->v[i], next[n], STRIP_NEXT_LEN);
	return n;
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

/* The finest blitter the terminal has for coloured dots: octants, else
 * quadrants, else half blocks; *dy, *dx its dots a cell. */
static ncblitter_e dots(struct notcurses *nc, int *dy, int *dx)
{
	if (notcurses_canoctant(nc)) return *dy = 4, *dx = 2, NCBLIT_4x2;
	if (notcurses_canquadrant(nc)) return *dy = 2, *dx = 2, NCBLIT_2x2;
	return *dy = 2, *dx = 1, NCBLIT_2x1;
}

/* 0xRRGGBB as an opaque RGBA pixel (R first in memory). */
static uint32_t pixel(uint32_t c) { return 0xff000000u | (c & 0xff) << 16 | (c & 0xff00) | (c >> 16 & 0xff); }

static void blit(struct ncplane *n, uint32_t *px, int W, int H, int y, int x, ncblitter_e b)
{
	struct ncvisual *v = ncvisual_from_rgba(px, H, W * 4, W);
	if (!v) return;
	struct ncvisual_options vo = { .n = n, .y = y, .x = x, .blitter = b, .scaling = NCSCALE_NONE,
	                               .flags = NCVISUAL_OPTION_NODEGRADE };
	ncvisual_blit(ncplane_notcurses(n), v, &vo);
	ncvisual_destroy(v);
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
	int dy, dx;
	ncblitter_e how = dots(ncplane_notcurses(n), &dy, &dx);
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
			px[y * W + x] = pixel(c);
		}
	}
	blit(n, px, W, H, 0, a.scope, how);
	free(px);
}

/* The road: bars of the bands on every row but the last, peaks over them,
 * and on the last the reflection, the water and its ripples. */
static void road_ground(struct strip *s, const struct strip_model *m, const struct theme *t,
                        unsigned rows, unsigned cols)
{
	struct ncplane *n = s->base;
	plain_ground(n, t, rows, cols);
	int going = m->daemon && !m->msg[0] && !m->refused[0] &&
	            (m->st.state == TUI_PLAYING || m->st.state == TUI_PAUSED);
	int up = (int)rows - 1, steps = up * 8, water = (int)rows - 1;
	if ((int)cols != s->pn) {
		free(s->bars);
		free(s->peak);
		free(s->hold);
		s->bars = calloc(cols, sizeof(float));
		s->peak = calloc(cols, sizeof(float));
		s->hold = calloc(cols, sizeof(float));
		s->pn = s->bars && s->peak && s->hold ? (int)cols : 0;
		s->road_at = m->anim;
		if (!s->pn) return;
	}
	float *bar = s->bars;
	if (m->st.state != TUI_PAUSED)
		for (unsigned x = 0; x < cols; x++)
			bar[x] = going && m->levels ? (float)(strip_road_bar(m->rx.band, (x + 0.5) / cols) * steps) : 0;
	double since = m->anim - s->road_at;
	s->road_at = m->anim;
	strip_road_peaks(s->peak, s->hold, bar, (int)cols, since < 0 || since > 2 ? 2 : since);

	static const char *const UP[9] = { " ", "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█" };
	uint32_t bg = (uint32_t)t->c[TH_BG], fg = (uint32_t)t->c[TH_FG], cyan = (uint32_t)t->c[TH_CYAN];
	double head = going ? m->bar / 8.0 : -1;
	double k = m->st.state == TUI_PAUSED ? breathe(BREATH_LOW, m->breath) : 1.0;
	for (unsigned x = 0; x < cols; x++) {
		uint32_t sky = strip_sky(t, (x + 0.5) / cols);
		int played = x + 0.5 < head;
		uint32_t c = rgb_mix(bg, played ? sky : rgb_mix(sky, (uint32_t)t->c[TH_DIM], 0.55), (played ? 1.0 : 0.6) * k);
		int b = (int)lround(bar[x]);
		for (int r = 0; r < up; r++) { /* r = 0 the lowest */
			int e = b - r * 8;
			e = e < 0 ? 0 : e > 8 ? 8 : e;
			if (!e) continue;
			/* the top of a bar a little whiter, as if lit from above */
			ncplane_set_fg_rgb(n, e < 8 || b <= (r + 1) * 8 ? rgb_mix(c, fg, 0.2) : c);
			ncplane_set_bg_rgb(n, bg);
			ncplane_putstr_yx(n, up - 1 - r, (int)x, UP[e]);
		}
		/* the peak: a thin cap where the bar is not */
		int pk = (int)lround(s->peak[x]), pr = (pk - 1) / 8;
		if (going && pk > b && pk > 0 && pr < up && pr != (b ? (b - 1) / 8 : -1)) {
			ncplane_set_fg_rgb(n, rgb_mix(c, fg, 0.6));
			ncplane_set_bg_rgb(n, bg);
			ncplane_putstr_yx(n, up - 1 - pr, (int)x, (pk - 1) % 8 >= 4 ? "▔" : "▁");
		}
		/* the water: lit a little where it has played; the reflection
		 * of the lowest row hangs from its top, shorter, darker */
		uint32_t wet = rgb_mix(bg, sky, played ? 0.10 : 0.03);
		int e = b > 8 ? 8 : b;
		e = e * 3 / 4;
		ncplane_set_fg_rgb(n, wet);
		ncplane_set_bg_rgb(n, e ? rgb_mix(bg, c, REFLECT) : wet);
		ncplane_putstr_yx(n, water, (int)x, e ? UP[8 - e] : " ");
		/* ripples drifting right */
		if (going && ((int)x - (int)(m->anim * 3.0)) % 13 == 0 && !e) {
			ncplane_set_fg_rgb(n, rgb_mix(wet, cyan, 0.30));
			ncplane_putstr_yx(n, water, (int)x, "─");
		}
	}
}

/* The horizon: the sky row, then the picture in dots below it. */
static void horizon_ground(const struct strip_model *m, const struct theme *t, struct ncplane *n,
                           unsigned rows, unsigned cols)
{
	plain_ground(n, t, rows, cols);
	int going = m->daemon && !m->msg[0] && !m->refused[0] &&
	            (m->st.state == TUI_PLAYING || m->st.state == TUI_PAUSED);
	uint32_t bg = (uint32_t)t->c[TH_BG], fg = (uint32_t)t->c[TH_FG], cyan = (uint32_t)t->c[TH_CYAN],
	         magenta = (uint32_t)t->c[TH_MAGENTA], violet = (uint32_t)t->c[TH_VIOLET],
	         orange = (uint32_t)t->c[TH_ORANGE];
	double k = m->st.state == TUI_PAUSED ? breathe(BREATH_LOW, m->breath) : going ? 1.0 : 0.45;
	double beat = m->levels && m->st.state == TUI_PLAYING ? m->rx.beat : 0;
	int dy, dx;
	ncblitter_e how = dots(ncplane_notcurses(n), &dy, &dx);
	int W = (int)cols * dx, H = ((int)rows - 1) * dy;
	double sun = going ? (m->bar / 8.0) / cols : 0.5; /* where the road meets the sky */
	double vx = sun * W;

	/* the sky row: violet above, glowing magenta to orange by the sun below,
	 * kept dark enough under the words */
	for (unsigned x = 0; x < cols; x++) {
		double d = (x + 0.5 - sun * cols) / cols, g = exp(-d * d * 60.0) * k;
		ncplane_set_fg_rgb(n, rgb_mix(bg, violet, 0.20 * g));
		ncplane_set_bg_rgb(n, rgb_mix(rgb_mix(bg, magenta, 0.35 * g), orange, 0.20 * g * g));
		ncplane_putstr_yx(n, 0, (int)x, "▀");
	}
	if (W <= 0 || H <= 2) return;

	uint32_t *px = malloc(sizeof(uint32_t) * (size_t)W * (size_t)H);
	if (!px) return;
	/* Few colours, as a cell of dots holds two: the ground, the mountains,
	 * the horizon, the rails, the ties. */
	int hz = H / 3 > 1 ? H / 3 : 1; /* the horizon's dot row */
	double half = 0.25 * W;         /* the road's half width at your feet */
	uint32_t ground = pixel(bg);
	uint32_t hill = pixel(rgb_mix(bg, rgb_mix(violet, magenta, 0.35), 0.55 * k));
	uint32_t rail = pixel(rgb_mix(bg, rgb_mix(magenta, fg, 0.15), 0.85 * k));
	uint32_t tie = pixel(rgb_mix(bg, cyan, 0.75 * k));
	for (int i = 0; i < W * H; i++) px[i] = ground;
	/* the mountains: the bands, standing on the horizon either side of the sun */
	if (going && m->levels) {
		double far = vx > W - vx ? vx : W - vx;
		for (int x = 0; x < W; x++) {
			double h = strip_horizon_hill(m->rx.band, fabs(x + 0.5 - vx) / (far > 1 ? far : 1)) * hz;
			for (int y = hz - (int)lround(h); y < hz; y++)
				if (y >= 0) px[y * W + x] = hill;
		}
	}
	/* the horizon, hot by the sun and on a beat */
	for (int x = 0; x < W; x++) {
		double d = (x + 0.5 - vx) / W, g = exp(-d * d * 30.0);
		uint32_t c = rgb_mix(rgb_mix(bg, violet, 0.5), rgb_mix(magenta, fg, 0.3 + 0.5 * beat), g);
		px[hz * W + x] = pixel(rgb_mix(bg, c, k));
	}
	/* the cross-ties, rushing in, nearer is lower and wider */
	for (int i = 0; i < 4 && going; i++) {
		double d = strip_horizon_tie(i, 4, m->anim), u = d * d;
		int y = hz + 1 + (int)lround(u * (H - 2 - hz));
		for (int x = (int)(vx - half * u); x <= (int)(vx + half * u); x++)
			if (x >= 0 && x < W) px[y * W + x] = tie;
	}
	/* the road's edges and its middle, fanning out from the sun */
	for (int r = -2; r <= 2; r += 2) {
		int last = (int)vx;
		for (int y = hz + 1; y < H; y++) {
			double u = (double)(y - hz) / (H - 1 - hz);
			int x = (int)lround(vx + half * u * r / 2.0);
			int a = last < x ? last : x, b = last < x ? x : last;
			if (b - a > 1) a = last < x ? last + 1 : x;
			for (int xx = a; xx <= b; xx++)
				if (xx >= 0 && xx < W) px[y * W + xx] = rail;
			last = x;
		}
	}
	blit(n, px, W, H, 1, 0, how);
	free(px);
}

/* ------------------------------------------------------------------ stage */

/* A steady noise, 0..1, for stars, glints and the sun's broken column. */
static double noise(int a, int b)
{
	unsigned x = (unsigned)a * 0x9e3779b1u + 0x7f4a7c15u;
	x ^= x >> 16;
	x *= 0x85ebca6bu;
	x += (unsigned)b * 0xc2b2ae35u;
	x ^= x >> 15;
	x *= 2246822519u;
	x ^= x >> 13;
	x *= 3266489917u;
	x ^= x >> 16;
	return (x & 0xffff) / 65535.0;
}

/* The stage's layout, in cells: the words take rows [0, words); the
 * title's letters are big when big > 0; the left words end at `left`, the
 * right ones (what is next) start at `right`. */
struct stage_at {
	int sc, words, big, left, right;
	char title[64];
};

static struct stage_at stage_at(const struct strip_model *m, unsigned rows, unsigned cols, int octants)
{
	struct stage_at a = { .sc = strip_stage_scale(rows) };
	a.right = cols >= 70 ? (int)cols - 30 : (int)cols;
	const char *title = m->st.title[0] ? m->st.title : m->st.uri;
	int room = (a.right - 2 - 2) / (3 * a.sc);
	if (octants) a.big = strip_big_text(title, a.title, sizeof(a.title), room);
	a.words = a.big ? 2 * a.sc + 3 : 3;
	int lw = a.big ? 2 + 3 * a.sc * a.big : 2 + text_cols(title);
	int aw = 2 + text_cols(m->st.artist), bw = 2 + text_cols(m->st.album);
	a.left = lw > aw ? lw : aw;
	a.left = a.left > bw ? a.left : bw;
	return a;
}

static void stage_ground(const struct strip_model *m, const struct theme *t, struct ncplane *n,
                         unsigned rows, unsigned cols, const struct stage_at *a)
{
	plain_ground(n, t, rows, cols);
	int dy, dx;
	ncblitter_e how = dots(ncplane_notcurses(n), &dy, &dx);
	int W = (int)cols * dx, H = ((int)rows - 1) * dy;
	int hz = (int)(H * 0.58);
	if (hz < a->words * dy + 1) hz = a->words * dy + 1;
	if (W <= 0 || hz >= H - 2) return;
	uint32_t bg = (uint32_t)t->c[TH_BG], fg = (uint32_t)t->c[TH_FG], cyan = (uint32_t)t->c[TH_CYAN],
	         magenta = (uint32_t)t->c[TH_MAGENTA], violet = (uint32_t)t->c[TH_VIOLET],
	         orange = (uint32_t)t->c[TH_ORANGE];
	double k = m->st.state == TUI_PAUSED ? breathe(BREATH_LOW, m->breath) : 1.0;
	double now = m->anim, level = m->levels ? m->rx.level : 0;
	double p = m->st.duration > 0 ? m->pos / m->st.duration : 0.5, ux, alt;
	strip_stage_sun(p, &ux, &alt);
	double r = hz * 0.30, sx = W * ux;
	double sy = hz + r * 0.55 - alt * (hz - r * 0.45 - 1); /* rising, high, setting */
	double warm = 1.0 - 0.6 * alt;                          /* a low sun, a warmer sky */
	uint32_t *c = malloc(sizeof(uint32_t) * (size_t)W * (size_t)H);
	if (!c) return;

	/* the sky, and the sun: orange-white at the top to magenta, its lower
	 * half striped, the stripes rising */
	for (int y = 0; y < hz; y++)
		for (int x = 0; x < W; x++) {
			double v = (double)y / hz, d = (x - sx) / W, g = exp(-d * d * 14.0);
			uint32_t px = rgb_mix(bg, violet, 0.25 * v * v);
			px = rgb_mix(px, rgb_mix(magenta, orange, 0.3 * warm), v * v * v * g * (0.30 + 0.35 * warm));
			double ex = (x - sx) / dx * 2.0, ey = (y - sy) * 4.0 / dy; /* the sun is round in cells */
			double rr = r * 4.0 / dy;
			if (ex * ex + ey * ey <= rr * rr) {
				double u = (ey + rr) / (2 * rr);
				int gap = u > 0.5 && fmod(y * 4.0 / dy + now * 1.5, 4.0) < (u - 0.5) * 5.0;
				if (!gap) px = rgb_mix(rgb_mix(orange, fg, 0.35), magenta, u * 1.1 > 1 ? 1 : u * 1.1);
			}
			c[y * W + x] = px;
		}
	/* stars, in the dark above, twinkling; clear of the sun and the words */
	for (int i = 0; i < W * hz / 90; i++) {
		int x = (int)(noise(i, 1) * W), y = (int)(noise(i, 2) * hz * 0.80);
		double ex = (x - sx) / dx * 2.0, ey = (y - sy) * 4.0 / dy, rr = r * 4.0 / dy;
		if (ex * ex + ey * ey <= rr * rr * 1.3) continue;
		if (y / dy < a->words && (x / dx <= a->left + 1 || x / dx >= a->right - 1)) continue;
		double tw = 0.5 + 0.5 * sin(now * (1.5 + 3.5 * noise(i, 4)) + 6.28 * noise(i, 5));
		uint32_t sc = noise(i, 6) > 0.8 ? cyan : noise(i, 6) > 0.6 ? magenta : fg;
		c[y * W + x] = rgb_mix(bg, sc, (0.25 + 0.55 * tw) * (1.0 - (double)y / hz));
	}
	/* the mountains: the bands, about the middle, a silhouette */
	uint32_t hill = rgb_mix(bg, violet, 0.10);
	for (int x = 0; x < W; x++) {
		double d = fabs(x - W / 2.0) / (W / 2.0);
		double h = (m->levels ? strip_road_bar(m->rx.band, d) : 0) * hz * 0.40 * (1.0 - 0.6 * d * d) + 1;
		for (int y = hz - (int)h; y < hz; y++)
			if (y >= 0) c[y * W + x] = hill;
	}
	/* the horizon, hot under the sun */
	for (int x = 0; x < W; x++) {
		double d = (x - sx) / W;
		c[hz * W + x] = rgb_mix(magenta, rgb_mix(magenta, fg, 0.6), exp(-d * d * 40.0));
	}
	/* the lake: the sky mirrored, darker and bluer; the sun's broken column;
	 * glints drifting in, as many as it is loud */
	double drift = now * 5.0;
	for (int y = hz + 1; y < H; y++) {
		int my = 2 * hz - y;
		double u = (double)(y - hz) / (H - 1 - hz); /* 0 far .. 1 near */
		for (int x = 0; x < W; x++)
			c[y * W + x] = my >= 0 ? rgb_mix(bg, rgb_mix(c[my * W + x], cyan, 0.12), 0.55 - 0.25 * u) : bg;
		if (sy - r < hz) {
			double up = (hz - (sy - r)) / (2 * r);
			double w = r * (0.45 + 0.75 * u) * (up > 1 ? 1 : up);
			uint32_t sun = rgb_mix(rgb_mix(orange, magenta, 0.35 + 0.4 * u), fg, 0.15);
			sun = rgb_mix(bg, sun, 0.55 + 0.45 * (1 - u));
			for (int x = (int)(sx - w); x <= (int)(sx + w); x++)
				if (x >= 0 && x < W && noise(y, (int)(x / 3.0 + drift * (0.5 + u))) > 0.45 - 0.2 * u)
					c[y * W + x] = sun;
		}
		int glints = 3 + (int)(level * 9);
		for (int i = 0; i < glints; i++) {
			int x = (int)fmod(noise(y, i) * W + drift * (2.0 + 6.0 * u) * (noise(i, y) - 0.5), W);
			if (x < 0) x += W;
			uint32_t gc = rgb_mix(bg, noise(i, y + 3) > 0.5 ? cyan : magenta, 0.35 + 0.5 * u);
			for (int j = 0; j < 2 + (int)(u * 6) && x + j < W; j++) c[y * W + x + j] = gc;
		}
	}
	/* the title, big, outlined dark so the sun can pass behind it; letters
	 * not yet settled after a track change are noise */
	if (a->big && !m->quiet) {
		int sc = a->sc;
		for (int pass = 0; pass < 2; pass++)
			for (int i = 0; i < a->big; i++) {
				const unsigned char *g = font5x7((unsigned char)a->title[i]);
				int settled = m->decode >= STRIP_SETTLED || strip_settled((size_t)i, m->decode);
				int cx = 4 + (FONT_W + FONT_GAP) * sc * i;
				for (int gy = 0; gy < FONT_H; gy++)
					for (int gx = 0; gx < FONT_W; gx++) {
						int on = settled ? g[gy] >> (FONT_W - 1 - gx) & 1
						                 : noise(i * 37 + gy * 5 + gx, (int)(m->decode * 15)) > 0.6;
						if (!on || (!settled && pass == 0)) continue;
						for (int oy = pass ? 0 : -1; oy <= sc - (pass ? 1 : 0); oy++)
							for (int ox = pass ? 0 : -1; ox <= sc - (pass ? 1 : 0); ox++) {
								int X = cx + gx * sc + ox, Y = 3 + gy * sc + oy;
								if (X >= 0 && Y >= 0 && X < W && Y < hz)
									c[Y * W + X] = pass ? (settled ? fg : cyan) : bg;
							}
					}
			}
	}
	for (int i = 0; i < W * H; i++) c[i] = pixel(rgb_mix(bg, c[i], k));
	blit(n, c, W, H, 0, 0, how);
	free(c);
}

/* The stage's words: the title small where it is not drawn big, artist and
 * album under it on a veil, and the dashboard on the last row. */
static void stage_words(struct pen *p, const struct strip_model *m, unsigned rows, unsigned cols,
                        const struct stage_at *a)
{
	const struct theme *t = p->t;
	const struct tui_status *st = &m->st;
	p->veil = 1;
	p->age = m->decode < STRIP_SETTLED ? m->decode : -1;
	p->k = 0;
	if (!a->big) {
		p->y = 0;
		if (t->bold_title) ncplane_set_styles(p->n, NCSTYLE_BOLD);
		say(p, TH_FG, 2, st->title[0] ? st->title : st->uri, a->right - 1);
		ncplane_set_styles(p->n, NCSTYLE_NONE);
	}
	if (st->artist[0]) {
		p->y = a->words - 2;
		say(p, TH_VIOLET, 2, st->artist, a->right - 1);
	}
	p->age = -1;
	if (st->album[0]) {
		p->y = a->words - 1;
		say(p, TH_DIM, 2, st->album, a->right - 1);
	}
	/* what is next, at the right: the first brighter */
	if (m->nnext && a->right < (int)cols) {
		int x = a->right + 2;
		p->y = 0;
		say(p, TH_DIM, x, "next", (int)cols - 1);
		for (int i = 0; i < m->nnext && i + 1 < a->words; i++) {
			char num[16];
			snprintf(num, sizeof(num), "%d ", i + 1);
			p->y = i + 1;
			int at = say(p, TH_DIM, x, num, (int)cols - 1);
			if (!i && t->c[TH_DIM] != THEME_DEFAULT && t->c[TH_FG] != THEME_DEFAULT)
				p->fg = (int32_t)rgb_mix((uint32_t)t->c[TH_DIM], (uint32_t)t->c[TH_FG], 0.6);
			say(p, TH_DIM, at, m->next[i], (int)cols - 1);
			p->fg = -1;
		}
	}
	p->veil = 0;

	/* the dashboard */
	int y = (int)rows - 1, paused = st->state == TUI_PAUSED;
	p->y = y;
	if (paused && t->c[TH_ORANGE] != THEME_DEFAULT && t->c[TH_DIM] != THEME_DEFAULT)
		p->fg = (int32_t)rgb_mix((uint32_t)t->c[TH_DIM], (uint32_t)t->c[TH_ORANGE], m->breath);
	say(p, paused ? TH_ORANGE : TH_MAGENTA, 1, paused ? "‖" : "▶", 2);
	p->fg = -1;
	char now[16], dur[16];
	strip_clock(now, sizeof(now), m->pos);
	int live = st->duration <= 0;
	if (live) snprintf(dur, sizeof(dur), "live");
	else strip_clock(dur, sizeof(dur), st->duration);
	int x = say(p, TH_CYAN, 3, now, (int)cols) + 1;
	int meter = m->levels ? 3 : 0;
	int end = (int)cols - text_cols(dur) - 2 - meter;
	if (end - x >= 4) {
		int w = end - x, head = live ? -1 : (int)(m->pos / st->duration * w);
		for (int i = 0; i < w; i++) {
			int played = i < head;
			p->fg = played ? (int32_t)strip_sky(t, (i + 0.5) / w)
			               : (int32_t)rgb_mix((uint32_t)t->c[TH_BG], (uint32_t)t->c[TH_DIM], 0.8);
			if (i == head) p->fg = (int32_t)t->c[TH_FG];
			say(p, TH_DIM, x + i, played ? "━" : i == head ? "╸" : "─", x + i + 1);
		}
		p->fg = -1;
	}
	say(p, live ? TH_GREEN : TH_DIM, (int)cols - text_cols(dur) - 1 - meter, dur, (int)cols - meter);
	if (meter) {
		static const char *const H[9] = { " ", "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█" };
		say(p, TH_CYAN, (int)cols - 2, H[strip_meter_eighths(m->lv.peak[0])], (int)cols - 1);
		say(p, TH_MAGENTA, (int)cols - 1, H[strip_meter_eighths(m->lv.peak[1])], (int)cols);
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
	free(s->glow);
	free(s->bars);
	free(s->peak);
	free(s->hold);
	free(s);
}

static void draw(struct strip *s, const struct strip_model *m, const struct theme *t)
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
	if (look == STRIP_STAGE) {
		int going = m->daemon && !m->msg[0] && !m->refused[0] &&
		            (m->st.state == TUI_PLAYING || m->st.state == TUI_PAUSED);
		ncplane_erase(s->text);
		ncplane_set_styles(s->text, NCSTYLE_NONE);
		ncplane_set_bg_alpha(s->text, NCALPHA_TRANSPARENT);
		struct pen p = { .n = s->text, .t = t, .age = -1, .fg = -1 };
		if (!going) { /* a dark stage and the one-row words: stopped, gone, refused */
			plain_ground(s->base, t, rows, cols);
			row(&p, m, cols, 0);
			return;
		}
		int dy, dx;
		dots(ncplane_notcurses(s->base), &dy, &dx);
		struct stage_at a = stage_at(m, rows, cols, dy == 4 && dx == 2);
		stage_ground(m, t, s->base, rows, cols, &a);
		stage_words(&p, m, rows, cols, &a);
		return;
	}
	if (look == STRIP_HORIZON) {
		horizon_ground(m, t, s->base, rows, cols);
		ncplane_erase(s->text);
		ncplane_set_styles(s->text, NCSTYLE_NONE);
		ncplane_set_bg_alpha(s->text, NCALPHA_TRANSPARENT);
		struct pen p = { .n = s->text, .t = t, .age = -1, .fg = -1 };
		row(&p, m, cols, 0);
		return;
	}
	if (look == STRIP_ROAD) {
		road_ground(s, m, t, rows, cols);
		ncplane_erase(s->text);
		ncplane_set_styles(s->text, NCSTYLE_NONE);
		ncplane_set_bg_alpha(s->text, NCALPHA_TRANSPARENT);
		struct pen p = { .n = s->text, .t = t, .age = -1, .fg = -1, .y = (int)rows - 1 };
		row(&p, m, cols, 0);
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

void strip_draw(struct strip *s, const struct strip_model *m, const struct theme *t)
{
	draw(s, m, t);
	if (m->quiet) ncplane_erase(s->text); /* the look alone, under the queue's glass */
}
