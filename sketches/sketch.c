/* SPDX-License-Identifier: LGPL-2.1-or-later */
/** @file sketch.c
 * @brief notmmtui look sketches: five short strips, rendered by Notcurses
 * and dumped cell by cell (glyph, fg, bg) after each frame so they can be
 * rasterised to images. Audio is synthetic; nothing talks to a daemon.
 *
 *   sketch <concept> <frames> <dumpfile>     size = the terminal's */

#define _GNU_SOURCE
#include <locale.h>
#include <math.h>
#include <notcurses/notcurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ palette */

struct rgb { float r, g, b; };

static const struct rgb BG     = { 0x05, 0x06, 0x0e };
static const struct rgb FG     = { 0xe6, 0xf0, 0xff };
static const struct rgb CYAN   = { 0x19, 0xd3, 0xff };
static const struct rgb MAGENTA= { 0xff, 0x2b, 0xd6 };
static const struct rgb VIOLET = { 0x8a, 0x3c, 0xff };
static const struct rgb ORANGE = { 0xff, 0x7a, 0x1a };
static const struct rgb DIM    = { 0x3a, 0x3f, 0x5c };
static const struct rgb WHITE  = { 0xff, 0xff, 0xff };

static struct rgb mix(struct rgb a, struct rgb b, float t)
{
	if (t < 0) t = 0;
	if (t > 1) t = 1;
	struct rgb o = { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t };
	return o;
}

static struct rgb scale(struct rgb a, float k)
{
	struct rgb o = { a.r * k, a.g * k, a.b * k };
	if (o.r > 255) o.r = 255;
	if (o.g > 255) o.g = 255;
	if (o.b > 255) o.b = 255;
	return o;
}

/* The sky in the reference: sunset orange at the left edge, magenta,
 * violet, then electric cyan. */
static struct rgb sky(float t)
{
	if (t < 0.18f) return mix(ORANGE, MAGENTA, t / 0.18f);
	if (t < 0.55f) return mix(MAGENTA, VIOLET, (t - 0.18f) / 0.37f);
	return mix(VIOLET, CYAN, (t - 0.55f) / 0.45f);
}

static uint32_t px(struct rgb c, unsigned a)
{
	/* ncvisual_from_rgba wants bytes R,G,B,A in memory. */
	return (uint32_t)(unsigned)c.r | (uint32_t)(unsigned)c.g << 8 |
	       (uint32_t)(unsigned)c.b << 16 | (uint32_t)a << 24;
}

static void fg(struct ncplane *n, struct rgb c) { ncplane_set_fg_rgb8(n, (unsigned)c.r, (unsigned)c.g, (unsigned)c.b); }
static void bg(struct ncplane *n, struct rgb c) { ncplane_set_bg_rgb8(n, (unsigned)c.r, (unsigned)c.g, (unsigned)c.b); }

static float luma(struct rgb c) { return (0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b) / 255.0f; }


/* Track-change transition: each letter is noise until its moment, then
 * settles, left to right. `age` is frames since the track changed. */
static const char *NOISE[] = { "\u2596", "\u2597", "\u2598", "\u259a", "\u259e", "\u2591", "\u2592",
                               "/", "\\", "<", ">", "#", "%", "*", "+", "=" };

static const char *decode_glyph(const char *src, size_t i, int age, char *one)
{
	if (age < 0 || (float)age >= 2.0f + (float)i * 0.55f) {
		one[0] = src[i];
		one[1] = '\0';
		return one;
	}
	return src[i] == ' ' ? " " : NOISE[(i * 7 + (size_t)age * 3) % (sizeof(NOISE) / sizeof(NOISE[0]))];
}

static size_t u8len(unsigned char c) { return c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4; }

/* Put `line` at (y, x), decoding its ASCII for the first frames after a
 * track change; colour from col(x) once settled, cyan while noise.
 * @returns cells used. */
static int put_decoded(struct ncplane *n, int y, int x, const char *line, int age, struct rgb (*col)(int x))
{
	size_t i = 0;
	int cx = x;
	char one[8];
	while (line[i]) {
		size_t l = u8len((unsigned char)line[i]);
		const char *g;
		int noise = 0;
		if (l == 1) {
			g = decode_glyph(line, i, age, one);
			noise = g != one;
		} else {
			memcpy(one, line + i, l);
			one[l] = 0;
			g = one;
		}
		fg(n, noise ? CYAN : col(cx));
		ncplane_putstr_yx(n, y, cx, g);
		i += l;
		cx++;
	}
	return cx - x;
}

static struct rgb white_at(int x) { (void)x; return FG; }

/* The GIF plays: a track change at frame 0, and a pause near the end. */
#define PAUSE_AT 34
static int paused(int f) { return f >= PAUSE_AT; }
static float breath(int f) { return 0.55f + 0.45f * cosf((float)(f - PAUSE_AT) * 0.45f); }

/* ------------------------------------------------------------------ fake audio */

static float hash(int i)
{
	unsigned x = (unsigned)i * 2654435761u;
	x ^= x >> 15;
	x *= 2246822519u;
	x ^= x >> 13;
	return (float)(x & 0xffff) / 65535.0f;
}

/* 0..1 per band, with a kick on the low end every 8 frames. */
static float band(int b, int nb, int f)
{
	float t = (float)f / 15.0f;
	float pos = (float)b / (float)nb;
	float env = 0.80f - 0.40f * pos;
	float kick = expf(-(float)(f % 8) * 0.55f) * (pos < 0.25f ? 1.0f - pos * 3 : 0.15f);
	float wob = 0.5f + 0.5f * sinf(t * (2.0f + 7.0f * hash(b)) + 6.28f * hash(b + 99));
	float v = env * (0.15f + 0.75f * wob * wob) * (0.55f + 0.45f * hash(b * 7 + f / 2)) + 0.6f * kick;
	if (paused(f)) v *= expf(-(float)(f - PAUSE_AT) * 0.5f);
	return v > 1 ? 1 : v;
}

static float wave(float x, int f)
{
	float t = (float)f / 15.0f;
	float k = 0.55f + 0.45f * expf(-(float)(f % 8) * 0.4f);
	float s = 0.7f * sinf(x * 12.0f + t * 5.0f) + 0.3f * sinf(x * 31.0f - t * 9.0f);
	return k * s * (0.6f + 0.4f * sinf(x * 5.0f - t * 3.0f));
}

/* ------------------------------------------------------------------ track */

static const char *TITLE  = "Time";
static const char *ARTIST = "Pink Floyd";
static const float DUR    = 413.0f;

static float progress(int f) { return 0.20f + 0.0025f * (float)(paused(f) ? PAUSE_AT : f); }

static void clock_str(char *b, size_t n, float s)
{
	snprintf(b, n, "%d:%02d", (int)s / 60, (int)s % 60);
}

/* ------------------------------------------------------------------ 1. powerline
 * One row. The whole row is the progress bar: the sky gradient, lit where
 * played and dark ahead, the playhead to an eighth of a cell with a
 * left-block glyph. Text takes its colour from what is under each cell.
 * Track change: the title decodes in. Paused: the lit part breathes. */

static struct rgb pl_under[512];
static struct rgb pl_ink(int x) { return luma(pl_under[x]) > 0.62f ? BG : FG; }

static void powerline(struct notcurses *nc, struct ncplane *std, struct ncplane *txt, int f)
{
	(void)nc;
	unsigned rows, cols;
	ncplane_dim_yx(std, &rows, &cols);
	float p = progress(f) * (float)cols;
	int pc = (int)p, pe = (int)((p - (float)pc) * 8.0f);
	float lit = paused(f) ? 0.25f + 0.40f * breath(f) : 0.62f;
	
	ncplane_erase(std);
	for (unsigned x = 0; x < cols; x++) {
		float t = (float)x / (float)(cols - 1);
		struct rgb on = scale(sky(t), lit), off = scale(sky(t), 0.12f);
		float d = (float)x - p;
		if (!paused(f)) on = mix(on, WHITE, expf(-d * d / 3.0f) * 0.55f);
		if ((int)x < pc) { bg(std, on); ncplane_putstr_yx(std, 0, (int)x, " "); pl_under[x] = on; }
		else if ((int)x == pc && pe) {
			static const char *L[8] = { " ", "▏", "▎", "▍", "▌", "▋", "▊", "▉" };
			fg(std, on); bg(std, off); ncplane_putstr_yx(std, 0, (int)x, L[pe]);
			pl_under[x] = pe >= 4 ? on : off;
		} else { bg(std, off); ncplane_putstr_yx(std, 0, (int)x, " "); pl_under[x] = off; }
	}
	ncplane_erase(txt);
	ncplane_set_bg_alpha(txt, NCALPHA_TRANSPARENT);
	char line[96];
	snprintf(line, sizeof(line), "%s %s — %s", paused(f) ? "‖" : "▶", TITLE, ARTIST);
	ncplane_set_styles(txt, NCSTYLE_BOLD);
	put_decoded(txt, 0, 1, line, f, pl_ink);
	ncplane_set_styles(txt, NCSTYLE_NONE);
	char now[16], dur[16], right[48];
	clock_str(now, sizeof(now), progress(f) * DUR);
	clock_str(dur, sizeof(dur), DUR);
	snprintf(right, sizeof(right), "%s / %s   vol 80 ", now, dur);
	int rx = (int)(cols - strlen(right));
	for (int i = 0; right[i]; i++) {
		fg(txt, pl_ink(rx + i));
		ncplane_putnstr_yx(txt, 0, rx + i, 1, right + i);
	}
}

/* ------------------------------------------------------------------ 2. wires
 * Three rows of braille: pylons and a sagging line between them, like the
 * reference. The line is the progress bar; a comet runs along it to the
 * playhead and throws sparks. Title floats above the sag. */

static void wires(struct notcurses *nc, struct ncplane *std, struct ncplane *txt, int f)
{
	unsigned rows, cols;
	ncplane_dim_yx(std, &rows, &cols);
	int W = (int)cols * 2, H = (int)rows * 4;
	uint32_t *pix = calloc((size_t)W * H, 4);
	int span = 52; /* dots between pylons */
	float head = progress(f) * (float)W;
	for (int x = 0; x < W; x++) {
		int px0 = (x / span) * span;
		float u = (float)(x - px0) / (float)span;   /* 0..1 across a span */
		float sag = 4.0f * u * (1.0f - u);           /* parabola, 1 at mid */
		float t = (float)x / (float)(W - 1);
		for (int k = 0; k < 2; k++) {                /* two wires */
			float yy = 1.0f + (float)k * 2.0f + sag * (float)(H - 6);
			int y = (int)(yy + 0.5f);
			if (y < 0 || y >= H) continue;
			float d = (float)x - head;
			struct rgb c;
			if (d > 0) c = scale(sky(t), 0.28f);
			else c = mix(scale(sky(t), paused(f) ? 0.4f + 0.6f * breath(f) : 1.0f), WHITE,
			             paused(f) ? 0.0f : expf(d / 6.0f) * 0.9f);
			pix[y * W + x] = px(c, 255);
		}
		if (x % span == 0) { /* a pylon: mast and two arms */
			for (int y = 0; y < H; y++) pix[y * W + x] = px(scale(DIM, 1.6f), 255);
			for (int dx = -2; dx <= 2; dx++)
				if (x + dx >= 0 && x + dx < W) {
					pix[1 * W + x + dx] = px(scale(DIM, 1.9f), 255);
					pix[3 * W + x + dx] = px(scale(DIM, 1.9f), 255);
				}
		}
	}
	/* sparks around the head */
	for (int i = 0; i < (paused(f) ? 0 : 14); i++) {
		float a = hash(f * 31 + i) * 6.283f, r = hash(f * 17 + i * 5) * 7.0f;
		int sx = (int)(head + cosf(a) * r), hx = (int)head % span;
		float u = (float)hx / (float)span;
		int sy = (int)(1.0f + 4.0f * u * (1.0f - u) * (float)(H - 6) + sinf(a) * r * 0.5f);
		if (sx >= 0 && sx < W && sy >= 0 && sy < H)
			pix[sy * W + sx] = px(mix(CYAN, WHITE, hash(i + f)), 255);
	}
	struct ncvisual *v = ncvisual_from_rgba(pix, H, W * 4, W);
	struct ncvisual_options vo = { .n = std, .blitter = NCBLIT_BRAILLE, .scaling = NCSCALE_NONE };
	ncplane_erase(std);
	ncvisual_blit(nc, v, &vo);
	ncvisual_destroy(v);
	free(pix);

	ncplane_erase(txt);
	ncplane_set_bg_alpha(txt, NCALPHA_TRANSPARENT);
	fg(txt, FG);
	ncplane_set_styles(txt, NCSTYLE_BOLD);
	ncplane_putstr_yx(txt, 0, 4, TITLE);
	ncplane_set_styles(txt, NCSTYLE_NONE);
	fg(txt, scale(MAGENTA, 1.0f));
	ncplane_printf(txt, "  %s", ARTIST);
	char now[16], dur[16];
	clock_str(now, sizeof(now), progress(f) * DUR);
	clock_str(dur, sizeof(dur), DUR);
	fg(txt, CYAN);
	ncplane_printf_yx(txt, 0, (int)cols - 14, "%s / %s", now, dur);
}

/* ------------------------------------------------------------------ 3. road
 * Spectrum in eighth blocks, 16 levels in two rows, over a wet-road
 * reflection: the bars mirrored into the third row, dimmed, drawn with
 * lower blocks and fg/bg swapped so they hang down from the horizon. */

static const char *EIGHTHS[9] = { " ", "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█" };

static void road(struct notcurses *nc, struct ncplane *std, struct ncplane *txt, int f)
{
	(void)nc;
	unsigned rows, cols;
	ncplane_dim_yx(std, &rows, &cols);
	ncplane_erase(std);
	int up = 2; /* rows of bars */
	float p = progress(f);
	static float peak[512];
	for (unsigned x = 0; x < cols; x++) {
		float t = (float)x / (float)(cols - 1);
		float v = band((int)x, (int)cols, f);
		int lv = (int)(v * (float)(up * 8) + 0.5f);
		peak[x] = paused(f) ? 0 : (float)lv > peak[x] ? (float)lv : peak[x] - 1.0f;
		struct rgb base = sky(t);
		if (t > p) base = mix(base, DIM, 0.55f);
		for (int r = 0; r < up; r++) {   /* r = 0 is the bottom bar row */
			int e = lv - r * 8;
			e = e < 0 ? 0 : e > 8 ? 8 : e;
			struct rgb c = mix(base, WHITE, r == up - 1 && e > 0 && e < 8 ? 0.25f : 0.0f);
			fg(std, c);
			bg(std, BG);
			ncplane_putstr_yx(std, up - 1 - r, (int)x, EIGHTHS[e]);
		}
		/* peak hold: a thin cap that falls slowly, where the bar is not */
		int pk = (int)(peak[x] + 0.5f), pr = (pk - 1) / 8;
		if (pk > lv && pk > 0 && pr != (lv ? (lv - 1) / 8 : -1) && pr < up) {
			fg(std, mix(base, WHITE, 0.6f));
			ncplane_putstr_yx(std, up - 1 - pr, (int)x, (pk - 1) % 8 >= 4 ? "\u2594" : "\u2581");
		}
		/* reflection: row `up` mirrors the bottom bar row, dimmed. The top
		 * e/8 of the cell is ink: lower-block (8-e)/8 in bg colour over an
		 * ink background. */
		int e = lv > 8 ? 8 : lv;
		e = e * 3 / 4; /* a reflection is shorter */
		struct rgb rc = scale(base, 0.30f);
		struct rgb wet = mix(BG, scale(sky(t), 0.10f), 0.6f);
		if (e == 0) { fg(std, wet); bg(std, wet); ncplane_putstr_yx(std, up, (int)x, " "); }
		else { fg(std, wet); bg(std, rc); ncplane_putstr_yx(std, up, (int)x, EIGHTHS[8 - e]); }
		/* a ripple in the water */
		if (((int)x + f) % 11 == 0) { fg(std, scale(CYAN, 0.35f)); ncplane_putstr_yx(std, up, (int)x, "─"); }
	}
	/* the playhead: a cyan horizon tick */
	int hx = (int)(p * (float)(cols - 1));
	fg(std, WHITE);
	bg(std, CYAN);
	ncplane_putstr_yx(std, up, hx, "▁");

	ncplane_erase(txt);
	ncplane_set_bg_alpha(txt, NCALPHA_TRANSPARENT);
	ncplane_set_fg_alpha(txt, NCALPHA_HIGHCONTRAST);
	fg(txt, FG);
	char now[16];
	clock_str(now, sizeof(now), p * DUR);
	ncplane_printf_yx(txt, 0, 1, " %s \u00b7 %s ", TITLE, ARTIST);
	ncplane_printf_yx(txt, 0, (int)cols - 7, " %s ", now);
}

/* ------------------------------------------------------------------ 4. plasma
 * One row, two pixels tall (upper half block: fg = top, bg = bottom).
 * Plasma behind what has played; ahead is dark with faint dots. The text
 * plane's background is BG at NCALPHA_BLEND, so Notcurses mixes a dark
 * veil into the plasma under the letters and they read without a box. */

static struct rgb plasma_at(float x, float y, float t)
{
	float u = x / 4.0f, w = y * 2.2f;
	float v = sinf(u + t * 1.9f) + sinf((u * 0.7f + w) - t * 1.3f) +
	          sinf(sqrtf(u * u * 0.08f + w * w + 1.0f) * 2.0f - t * 2.6f) + sinf(u * 0.33f - w + t);
	v = (v + 4.0f) / 8.0f;
	return scale(sky(fmodf(v * 1.6f + t * 0.04f, 1.0f)), 0.45f + 0.65f * v);
}

static void plasma(struct notcurses *nc, struct ncplane *std, struct ncplane *txt, int f)
{
	(void)nc;
	unsigned rows, cols;
	ncplane_dim_yx(std, &rows, &cols);
	float p = progress(f) * (float)cols;
	float t = (float)(paused(f) ? PAUSE_AT : f) / 15.0f;
	float k = paused(f) ? 0.4f + 0.6f * breath(f) : 1.0f;
	ncplane_erase(std);
	for (unsigned y = 0; y < rows; y++)
		for (unsigned x = 0; x < cols; x++) {
			if ((float)x > p) {
				bg(std, BG);
				fg(std, scale(DIM, 0.9f));
				ncplane_putstr_yx(std, (int)y, (int)x, x % 3 ? " " : "\u00b7");
				continue;
			}
			fg(std, scale(plasma_at((float)x, (float)y * 2.0f, t), k));
			bg(std, scale(plasma_at((float)x, (float)y * 2.0f + 1.0f, t), k));
			ncplane_putstr_yx(std, (int)y, (int)x, "▀");
		}
	ncplane_erase(txt);
	char line[64];
	snprintf(line, sizeof(line), " %s · %s ", TITLE, ARTIST);
	ncplane_set_bg_alpha(txt, NCALPHA_BLEND);
	bg(txt, BG);
	ncplane_set_styles(txt, NCSTYLE_BOLD);
	put_decoded(txt, 0, 2, line, f, white_at);
	ncplane_set_styles(txt, NCSTYLE_NONE);
	ncplane_set_bg_alpha(txt, NCALPHA_TRANSPARENT);
	fg(txt, scale(VIOLET, 1.25f));
	char now[16], dur[16];
	clock_str(now, sizeof(now), progress(f) * DUR);
	clock_str(dur, sizeof(dur), DUR);
	ncplane_printf_yx(txt, 0, (int)cols - 14, "%s / %s", now, dur);
}

/* ------------------------------------------------------------------ 5. scope
 * Two rows. Left: title over artist, and a 2-row VU in eighths. Right: an
 * oscilloscope in octants (4x2 dots a cell) with phosphor persistence:
 * older traces fade to violet. */

static uint32_t *scopebuf;

static void scope(struct notcurses *nc, struct ncplane *std, struct ncplane *txt, int f)
{
	unsigned rows, cols;
	ncplane_dim_yx(std, &rows, &cols);
	int left = 34;
	int W = ((int)cols - left) * 2, H = (int)rows * 4;
	if (!scopebuf) scopebuf = calloc((size_t)W * H, 4);
	/* phosphor: older traces sink toward dim violet, then go out. The
	 * blitter treats low alpha as transparent, so fade the colour. */
	for (int i = 0; i < W * H && !paused(f); i++) {
		uint32_t q = scopebuf[i];
		if (!(q >> 24)) continue;
		struct rgb c = { (float)(q & 255), (float)(q >> 8 & 255), (float)(q >> 16 & 255) };
		c = scale(mix(c, VIOLET, 0.6f), 0.30f);
		scopebuf[i] = luma(c) < 0.09f ? 0 : px(c, 255);
	}
	int prev = -1;
	for (int x = 0; x < W && !paused(f); x++) {
		float s = wave((float)x / (float)W, f);
		int y = (int)((1.0f - (s + 1.0f) * 0.5f) * (float)(H - 1) + 0.5f);
		if (y < 0) y = 0;
		if (y >= H) y = H - 1;
		int a = prev < 0 ? y : prev, b = y;
		if (a > b) { int k = a; a = b; b = k; }
		for (int yy = a; yy <= b; yy++)
			scopebuf[yy * W + x] = px(mix(MAGENTA, CYAN, (float)x / (float)W), 255);
		prev = y;
	}
	ncplane_erase(std);
	struct ncvisual *v = ncvisual_from_rgba(scopebuf, H, W * 4, W);
	struct ncvisual_options vo = { .n = std, .blitter = NCBLIT_4x2, .scaling = NCSCALE_NONE,
	                               .x = left, .flags = NCVISUAL_OPTION_NODEGRADE };
	ncvisual_blit(nc, v, &vo);
	ncvisual_destroy(v);

	/* VU: 2 cols x 2 rows of eighths = 16 levels, L and R */
	for (int ch = 0; ch < 2; ch++) {
		float lv = band(ch * 3, 40, f);
		int e = (int)(lv * 16 + 0.5f);
		for (int r = 0; r < 2; r++) {
			int k = e - r * 8;
			k = k < 0 ? 0 : k > 8 ? 8 : k;
			fg(std, r ? MAGENTA : CYAN);
			bg(std, scale(DIM, 0.35f));
			ncplane_putstr_yx(std, 1 - r, left - 4 + ch, EIGHTHS[k]);
		}
	}
	ncplane_erase(txt);
	ncplane_set_bg_alpha(txt, NCALPHA_TRANSPARENT);
	fg(txt, MAGENTA);
	ncplane_putstr_yx(txt, 0, 1, "▶ ");
	fg(txt, FG);
	ncplane_set_styles(txt, NCSTYLE_BOLD);
	ncplane_putstr(txt, TITLE);
	ncplane_set_styles(txt, NCSTYLE_NONE);
	char now[16], dur[16];
	clock_str(now, sizeof(now), progress(f) * DUR);
	clock_str(dur, sizeof(dur), DUR);
	fg(txt, CYAN);
	ncplane_printf_yx(txt, 0, 20, "%5s", now);
	fg(txt, scale(VIOLET, 1.3f));
	ncplane_printf_yx(txt, 1, 3, "%s", ARTIST);
	fg(txt, DIM);
	ncplane_printf_yx(txt, 1, 20, "%5s", dur);
}

/* ------------------------------------------------------------------ 6. horizon
 * Four rows: the reference's road. Row 0 is sky, dark, glowing where the
 * vanishing point sits; rows 1-3 are a perspective grid in octants that
 * rushes toward you on every kick. The vanishing point is the playhead. */

static void horizon(struct notcurses *nc, struct ncplane *std, struct ncplane *txt, int f)
{
	unsigned rows, cols;
	ncplane_dim_yx(std, &rows, &cols);
	float p = progress(f);
	int W = (int)cols * 2, H = ((int)rows - 1) * 4;
	float vx = p * (float)W;
	ncplane_erase(std);

	/* sky row: glow around the vanishing point, half blocks for 2 px */
	for (unsigned x = 0; x < cols; x++) {
		float d = ((float)x * 2.0f - vx) / (float)W;
		float g = expf(-d * d * 60.0f);
		struct rgb top = mix(BG, scale(VIOLET, 0.5f), g * 0.7f);
		struct rgb low = mix(mix(BG, scale(MAGENTA, 0.7f), g), ORANGE, g * g * 0.6f);
		fg(std, top);
		bg(std, low);
		ncplane_putstr_yx(std, 0, (int)x, "▀");
	}

	/* grid */
	static float travel;
	if (!paused(f)) travel += 0.035f + 0.09f * expf(-(float)(f % 8) * 0.6f);
	uint32_t *pix = calloc((size_t)W * H, 4);
	struct rgb road = { 0x07, 0x09, 0x18 };
	for (int i = 0; i < W * H; i++) pix[i] = px(road, 255);
	/* the road: a trapezoid from the vanishing point, half width `rw`
	 * at the bottom row; dark land either side */
	float rw = 70.0f;
	for (int i = 0; i < 5; i++) { /* cross ties: depth d, y grows with d^2 */
		float d = fmodf((float)i / 5.0f + travel, 1.0f);
		int y = 1 + (int)(d * d * (float)(H - 2) + 0.5f);
		float u = (float)y / (float)(H - 1), half = rw * u;
		struct rgb c = mix(scale(VIOLET, 0.5f), scale(CYAN, 0.85f), u);
		for (int x = (int)(vx - half); x <= (int)(vx + half); x++)
			if (x >= 0 && x < W) pix[y * W + x] = px(c, 255);
	}
	for (int k = -4; k <= 4; k++) { /* rails: each row joined to the last */
		float bx = vx + rw * (float)k / 4.0f;
		int lx = (int)vx;
		for (int y = 1; y < H; y++) {
			float u = (float)y / (float)(H - 1);
			int x = (int)(vx + (bx - vx) * u + 0.5f);
			int a = lx < x ? lx : x, b = lx < x ? x : lx;
			if (b - a > 1) a = lx < x ? lx + 1 : x;
			struct rgb c = (k == -4 || k == 4) ? mix(MAGENTA, WHITE, 0.15f) : mix(scale(MAGENTA, 0.7f), CYAN, u * 0.8f);
			for (int xx = a; xx <= b; xx++)
				if (xx >= 0 && xx < W) pix[y * W + xx] = px(c, 255);
			lx = x;
		}
	}
	/* land: low hills either side, a dim violet contour */
	for (int x = 0; x < W; x++) {
		float h = 2.0f + 1.6f * sinf((float)x * 0.07f) + 0.8f * sinf((float)x * 0.19f + 1.0f);
		int y = (int)h;
		float u = (float)y / (float)(H - 1);
		if (fabsf((float)x - vx) > rw * u + 2.0f && y > 0 && y < H)
			pix[y * W + x] = px(scale(VIOLET, 0.55f), 255);
	}
	for (int x = 0; x < W; x++) { /* horizon, hot at the vanishing point */
		float d = ((float)x - vx) / (float)W;
		pix[x] = px(mix(scale(VIOLET, 0.35f), mix(MAGENTA, WHITE, 0.5f), expf(-d * d * 30.0f)), 255);
	}
	struct ncvisual *v = ncvisual_from_rgba(pix, H, W * 4, W);
	struct ncvisual_options vo = { .n = std, .blitter = NCBLIT_4x2, .scaling = NCSCALE_NONE, .y = 1,
	                               .flags = NCVISUAL_OPTION_NODEGRADE };
	ncvisual_blit(nc, v, &vo);
	ncvisual_destroy(v);
	free(pix);

	ncplane_erase(txt);
	ncplane_set_bg_alpha(txt, NCALPHA_TRANSPARENT);
	char line[64];
	snprintf(line, sizeof(line), "%s %s · %s", paused(f) ? "‖" : "▶", TITLE, ARTIST);
	ncplane_set_styles(txt, NCSTYLE_BOLD);
	put_decoded(txt, 0, 1, line, f, white_at);
	ncplane_set_styles(txt, NCSTYLE_NONE);
	char now[16], dur[16];
	clock_str(now, sizeof(now), p * DUR);
	clock_str(dur, sizeof(dur), DUR);
	fg(txt, CYAN);
	ncplane_printf_yx(txt, 0, (int)cols - 14, "%s / %s", now, dur);
}

/* ------------------------------------------------------------------ main */

typedef void (*concept_fn)(struct notcurses *, struct ncplane *, struct ncplane *, int);

static void dump(struct notcurses *nc, FILE *o, unsigned rows, unsigned cols)
{
	for (unsigned y = 0; y < rows; y++)
		for (unsigned x = 0; x < cols; x++) {
			uint16_t style;
			uint64_t ch;
			char *egc = notcurses_at_yx(nc, y, x, &style, &ch);
			uint32_t fc = ncchannels_fchannel(ch), bc = ncchannels_bchannel(ch);
			fprintf(o, "%s\t%06x\t%06x\t%d\t%d\t%u\n", egc && *egc ? egc : " ",
			        ncchannel_rgb(fc), ncchannel_rgb(bc),
			        ncchannel_default_p(fc), ncchannel_default_p(bc), style);
			free(egc);
		}
	fputs("--\n", o);
}

int main(int argc, char **argv)
{
	if (argc != 4) return 2;
	static const struct { const char *name; concept_fn fn; } C[] = {
		{ "powerline", powerline }, { "wires", wires }, { "road", road },
		{ "plasma", plasma }, { "scope", scope }, { "horizon", horizon },
	};
	concept_fn fn = NULL;
	for (size_t i = 0; i < sizeof(C) / sizeof(C[0]); i++)
		if (!strcmp(argv[1], C[i].name)) fn = C[i].fn;
	if (!fn) return 2;
	int frames = atoi(argv[2]);
	FILE *o = fopen(argv[3], "w");
	if (!o) return 1;

	setlocale(LC_ALL, "");
	struct notcurses_options opts = {
		.flags = NCOPTION_SUPPRESS_BANNERS | NCOPTION_NO_ALTERNATE_SCREEN | NCOPTION_DRAIN_INPUT |
		         NCOPTION_NO_QUIT_SIGHANDLERS | NCOPTION_INHIBIT_SETLOCALE,
		.loglevel = NCLOGLEVEL_SILENT,
	};
	struct notcurses *nc = notcurses_core_init(&opts, NULL);
	if (!nc) return 1;
	struct ncplane *std = notcurses_stdplane(nc);
	unsigned rows, cols;
	ncplane_dim_yx(std, &rows, &cols);
	uint64_t base = 0;
	ncchannels_set_bg_rgb8(&base, (unsigned)BG.r, (unsigned)BG.g, (unsigned)BG.b);
	ncchannels_set_fg_rgb8(&base, (unsigned)FG.r, (unsigned)FG.g, (unsigned)FG.b);
	ncplane_set_base(std, " ", 0, base);
	ncplane_set_channels(std, base);
	struct ncplane_options po = { .rows = rows, .cols = cols };
	struct ncplane *txt = ncplane_create(std, &po);
	uint64_t clear = 0;
	ncchannels_set_bg_alpha(&clear, NCALPHA_TRANSPARENT);
	ncchannels_set_fg_alpha(&clear, NCALPHA_TRANSPARENT);
	ncplane_set_base(txt, "", 0, clear);

	fprintf(o, "%u %u %d %s\n", rows, cols, notcurses_canoctant(nc), argv[1]);
	for (int f = 0; f < frames; f++) {
		fn(nc, std, txt, f);
		notcurses_render(nc);
		dump(nc, o, rows, cols);
	}
	fclose(o);
	notcurses_stop(nc);
	return 0;
}
