/* SPDX-License-Identifier: LGPL-2.1-or-later */
/** @file overlay.c
 * @brief The queue over the strip: keys, a window on the list, drawing. */

#include "overlay.h"
#include "strip.h"
#include <ctype.h>
#include <dirent.h>
#include <notcurses/notcurses.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
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
	const char *home = getenv("HOME");
	if (!home || !home[0]) {
		struct passwd *pw = getpwuid(getuid());
		home = pw && pw->pw_dir ? pw->pw_dir : "/";
	}
	snprintf(o->home, sizeof(o->home), "%s", home);
	return o;
}

static void unmatch(struct overlay *o)
{
	for (size_t i = 0; i < o->nmatch; i++) free(o->match[i]);
	free(o->match);
	o->match = NULL;
	o->nmatch = o->more = 0;
}

void overlay_free(struct overlay *o)
{
	if (!o) return;
	unmatch(o);
	tui_list_free(&o->q);
	tui_list_free(&o->found);
	ncplane_destroy(o->n);
	free(o);
}

/* J and K: the selected item one down or up, the selection with it;
 * N: it right after the item playing, where the daemon puts it. */
static enum overlay_did move(struct overlay *o, char **argv, int by, int next)
{
	size_t n = o->q.n, s = o->sel;
	if (!n) return OVERLAY_TAKEN;
	size_t to;
	if (next) {
		long cur = -1;
		for (size_t i = 0; i < n; i++)
			if (o->q.v[i].cursor) cur = (long)i;
		if (cur < 0) to = 0;
		else if ((long)s == cur) return OVERLAY_TAKEN; /* it is playing: already there */
		else to = (long)s < cur ? (size_t)cur : (size_t)cur + 1; /* from above, the list closes up behind it */
		if (to == s) return OVERLAY_TAKEN;
	} else {
		if ((by < 0 && !s) || (by > 0 && s + 1 >= n)) return OVERLAY_TAKEN;
		to = by < 0 ? s - 1 : s + 1;
	}
	snprintf(o->pos, sizeof(o->pos), "%ld", o->q.v[s].pos);
	snprintf(o->to, sizeof(o->to), "%s", next ? "next" : "");
	if (!next) snprintf(o->to, sizeof(o->to), "%ld", o->q.v[to].pos);
	argv[0] = "queue";
	argv[1] = "mv";
	argv[2] = o->pos;
	argv[3] = o->to;
	argv[4] = NULL;
	o->sel = to; /* the selection goes with it; the list is read again */
	return OVERLAY_COMMAND;
}

/* ------------------------------------------------------------------ paths */

int overlay_expand(const char *in, const char *home, char *out, size_t n)
{
	/* scheme: and on is a URI; a path that might look like one can start ./ */
	const char *c = in;
	if (isalpha((unsigned char)*c)) {
		while (isalnum((unsigned char)*c) || *c == '+' || *c == '.' || *c == '-') c++;
		if (*c == ':') {
			snprintf(out, n, "%s", in);
			return 0;
		}
	}
	const char *sep = home[0] && home[strlen(home) - 1] == '/' ? "" : "/";
	if (in[0] == '/') snprintf(out, n, "%s", in);
	else if (in[0] == '~' && (in[1] == '/' || !in[1])) snprintf(out, n, "%s%s%s", home, sep, in[1] ? in + 2 : "");
	else snprintf(out, n, "%s%s%s", home, sep, in);
	return 1;
}

static int by_name(const void *a, const void *b)
{
	const char *x = *(char *const *)a, *y = *(char *const *)b;
	size_t lx = strlen(x), ly = strlen(y);
	int dx = lx && x[lx - 1] == '/', dy = ly && y[ly - 1] == '/';
	if (dx != dy) return dy - dx; /* directories first */
	int c = strcasecmp(x, y);
	return c ? c : strcmp(x, y);
}

#define MATCHES 2000

void overlay_scan(struct overlay *o)
{
	unmatch(o);
	char whole[4096];
	o->uri = !overlay_expand(o->input, o->home, whole, sizeof(whole));
	if (o->uri) return;
	/* the directory: up to the last /; what follows is the start of a name */
	char *slash = strrchr(whole, '/');
	const char *start = slash ? slash + 1 : whole;
	char dir[4096];
	snprintf(dir, sizeof(dir), "%.*s", slash ? (int)(slash - whole) + 1 : 0, whole);
	DIR *d = opendir(dir[0] ? dir : "/");
	if (!d) return;
	size_t len = strlen(start), cap = 0;
	struct dirent *e;
	while ((e = readdir(d))) {
		const char *name = e->d_name;
		if (!strcmp(name, ".") || !strcmp(name, "..")) continue;
		if (name[0] == '.' && start[0] != '.') continue; /* hidden, unless asked for */
		if (strncmp(name, start, len)) continue;
		if (o->nmatch == MATCHES) {
			o->more++;
			continue;
		}
		char full[4096 + 256];
		struct stat st;
		snprintf(full, sizeof(full), "%s%s", dir, name);
		int isdir = stat(full, &st) == 0 && S_ISDIR(st.st_mode);
		if (o->nmatch == cap) {
			size_t nc = cap ? cap * 2 : 32;
			char **m = realloc(o->match, nc * sizeof(*m));
			if (!m) break;
			o->match = m;
			cap = nc;
		}
		size_t nl = strlen(name);
		char *m = malloc(nl + 2);
		if (!m) break;
		memcpy(m, name, nl);
		m[nl] = isdir ? '/' : '\0';
		m[nl + 1] = '\0';
		o->match[o->nmatch++] = m;
	}
	closedir(d);
	if (o->nmatch) qsort(o->match, o->nmatch, sizeof(*o->match), by_name);
}

/* The typed part of the line's last name: where completing writes. */
static char *typed_name(struct overlay *o)
{
	char *slash = strrchr(o->input, '/');
	if (slash) return slash + 1;
	if (o->input[0] == '~' && !o->input[1]) return o->input + 1; /* "~": nothing of a name yet */
	return o->input;
}

/* Tab: as far as every match agrees; one match whole. */
static void complete(struct overlay *o)
{
	if (o->uri || !o->nmatch) return;
	if (!strcmp(o->input, "~")) { /* ~ alone means ~/ */
		snprintf(o->input, sizeof(o->input), "~/");
		overlay_scan(o);
		if (!o->nmatch) return;
	}
	size_t lcp = strlen(o->match[0]);
	for (size_t i = 1; i < o->nmatch; i++) {
		size_t k = 0;
		while (k < lcp && o->match[i][k] == o->match[0][k]) k++;
		lcp = k;
	}
	char *name = typed_name(o);
	size_t at = (size_t)(name - o->input);
	if (lcp <= strlen(name) || at + lcp >= sizeof(o->input)) return;
	memcpy(name, o->match[0], lcp);
	name[lcp] = '\0';
	overlay_scan(o);
}

/* ctrl+w: back over a trailing / and the name before it, or a word. */
static void rubout(char *s)
{
	size_t n = strlen(s);
	while (n && (s[n - 1] == '/' || s[n - 1] == ' ')) n--;
	while (n && s[n - 1] != '/' && s[n - 1] != ' ') n--;
	s[n] = '\0';
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

static enum overlay_did add(struct overlay *o, char **argv, const char *word)
{
	o->adding = 0;
	unmatch(o);
	argv[0] = "queue";
	argv[1] = "add";
	argv[2] = (char *)word;
	argv[3] = NULL;
	return OVERLAY_COMMAND;
}

/* A line's editing keys: ctrl+u clears it, ctrl+w takes back a word or
 * a directory, backspace a letter; a key types what it says (a ~ behind
 * ctrl, say), but ctrl or alt with a letter or a digit is a chord, not
 * text. @returns 1 if the line changed. */
static int edit(char *buf, size_t cap, uint32_t id, const struct ncinput *ni)
{
	size_t len = strlen(buf);
	int ctrl = ni && ncinput_ctrl_p(ni), alt = ni && ncinput_alt_p(ni);
	if (ctrl && (id == 'u' || id == 'U')) {
		buf[0] = '\0';
	} else if (ctrl && (id == 'w' || id == 'W')) {
		rubout(buf);
	} else if (id == NCKEY_BACKSPACE || id == 0x7f || id == 0x08) {
		while (len && (buf[len - 1] & 0xc0) == 0x80) len--; /* a whole letter */
		if (len) len--;
		buf[len] = '\0';
	} else if (id >= 0x20 && !nckey_synthesized_p(id) && ni && ni->utf8[0] &&
	           !((ctrl || alt) && id < 0x80 && isalnum((int)id))) {
		size_t n = strlen(ni->utf8);
		if (len + n >= cap) return 0;
		memcpy(buf + len, ni->utf8, n + 1);
	} else {
		return 0;
	}
	return 1;
}

/* ⏎ on the add: line: a URI as typed; a directory goes into it; a file is
 * added whole; a path to nothing but the start of one match takes it. */
static enum overlay_did enter(struct overlay *o, char **argv)
{
	if (!o->input[0]) return OVERLAY_TAKEN;
	if (!overlay_expand(o->input, o->home, o->path, sizeof(o->path))) return add(o, argv, o->input);
	struct stat st;
	size_t len = strlen(o->input);
	if (stat(o->path, &st) == 0) {
		if (!S_ISDIR(st.st_mode)) return add(o, argv, o->path);
		if (o->input[len - 1] != '/' && len + 1 < sizeof(o->input)) memcpy(o->input + len, "/", 2);
		overlay_scan(o);
		return OVERLAY_TAKEN;
	}
	if (o->nmatch == 1) {
		char *name = typed_name(o);
		if ((size_t)(name - o->input) + strlen(o->match[0]) < sizeof(o->input)) {
			strcpy(name, o->match[0]);
			size_t l = strlen(o->input);
			if (o->input[l - 1] == '/') { /* a directory: go into it */
				overlay_scan(o);
				return OVERLAY_TAKEN;
			}
			overlay_expand(o->input, o->home, o->path, sizeof(o->path));
			return add(o, argv, o->path);
		}
	}
	return add(o, argv, o->path); /* the daemon says what is wrong with it */
}

/* The add: line's keys: every key is the line's while it is up. */
static enum overlay_did adding(struct overlay *o, uint32_t id, const struct ncinput *ni, char **argv)
{
	if (id == NCKEY_ESC) {
		o->adding = 0;
		unmatch(o);
		return OVERLAY_TAKEN;
	}
	if (id == NCKEY_ENTER || id == '\r' || id == '\n') return enter(o, argv);
	if (id == NCKEY_TAB || id == '\t') {
		complete(o);
		return OVERLAY_TAKEN;
	}
	if (edit(o->input, sizeof(o->input), id, ni)) overlay_scan(o);
	return OVERLAY_TAKEN;
}

/* ------------------------------------------------------------------ search */

static const char *dash(const char *s) { return s && s[0] ? s : "-"; }

void overlay_line(const struct tui_item *it, char *out, size_t n)
{
	snprintf(out, n, "%s\t%s\t%s\t%s\t%s\t%s", it->kind == TUI_LIST ? "list" : "track", it->uri, dash(it->title),
	         dash(it->artist), dash(it->album), dash(it->duration));
}

void overlay_found(struct overlay *o, const char *reply)
{
	tui_list_free(&o->found);
	memset(&o->found, 0, sizeof(o->found));
	if (reply && tui_list_items(&o->found, reply) < 0) {
		tui_list_free(&o->found);
		memset(&o->found, 0, sizeof(o->found));
	}
	o->searched = 1;
	if (o->fsel >= o->found.n) o->fsel = o->found.n ? o->found.n - 1 : 0;
}

void overlay_note(struct overlay *o, const char *s)
{
	snprintf(o->note, sizeof(o->note), "%s", s ? s : "");
	size_t n = strlen(o->note);
	while (n && (o->note[n - 1] == '\n' || o->note[n - 1] == ' ')) o->note[--n] = '\0';
}

static enum overlay_did words2(char **argv, const char *a, const char *b)
{
	argv[0] = (char *)a;
	argv[1] = (char *)b;
	argv[2] = NULL;
	return OVERLAY_FETCH;
}

/* The search, or the list we are in, asked again. */
static enum overlay_did refetch(struct overlay *o, char **argv)
{
	if (!o->depth) return words2(argv, "search", o->query);
	return words2(argv, "browse", o->trail[o->depth - 1].uri);
}

static enum overlay_did to_queue(struct overlay *o)
{
	o->search = o->typing = 0;
	return OVERLAY_OPENED; /* read the queue */
}

/* The search: line's keys. */
static enum overlay_did typing(struct overlay *o, uint32_t id, const struct ncinput *ni, char **argv)
{
	if (id == NCKEY_ESC) {
		o->typing = 0;
		if (o->searched || o->depth) return OVERLAY_TAKEN; /* back to the results */
		o->open = o->search = 0;
		return OVERLAY_CLOSED;
	}
	if (id == NCKEY_ENTER || id == '\r' || id == '\n') {
		if (!o->query[0]) return OVERLAY_TAKEN;
		o->typing = 0;
		o->depth = 0;
		o->fsel = o->ftop = 0;
		return words2(argv, "search", o->query);
	}
	if (id == NCKEY_TAB || id == '\t') return to_queue(o);
	if (edit(o->query, sizeof(o->query), id, ni)) o->searched = 0;
	return OVERLAY_TAKEN;
}

/* The results' keys. */
static enum overlay_did results(struct overlay *o, uint32_t id, const struct ncinput *ni, char **argv)
{
	size_t n = o->found.n;
	if (ni && (ncinput_ctrl_p(ni) || ncinput_alt_p(ni))) return OVERLAY_NOT_MINE;
	switch (id) {
	case NCKEY_ESC: case 'q':
		o->open = o->search = 0;
		return OVERLAY_CLOSED;
	case NCKEY_TAB:
		return to_queue(o);
	case '/':
		o->typing = 1;
		return OVERLAY_TAKEN;
	case 'j': case NCKEY_DOWN:
		if (o->fsel + 1 < n) o->fsel++;
		return OVERLAY_TAKEN;
	case 'k': case NCKEY_UP:
		if (o->fsel) o->fsel--;
		return OVERLAY_TAKEN;
	case 'g': case NCKEY_HOME:
		o->fsel = 0;
		return OVERLAY_TAKEN;
	case 'G': case NCKEY_END:
		o->fsel = n ? n - 1 : 0;
		return OVERLAY_TAKEN;
	case NCKEY_BACKSPACE: case 0x7f: case 0x08: case 'h': case NCKEY_LEFT:
		if (!o->depth) {
			o->typing = 1;
			return OVERLAY_TAKEN;
		}
		o->depth--;
		o->fsel = o->trail[o->depth].sel;
		return refetch(o, argv);
	case NCKEY_ENTER: case '\r': case '\n': case 'l': case NCKEY_RIGHT: {
		if (!n) return OVERLAY_TAKEN;
		const struct tui_item *it = &o->found.v[o->fsel];
		if (it->kind == TUI_LIST) {
			if (o->depth == OVERLAY_DEPTH) return OVERLAY_TAKEN;
			snprintf(o->trail[o->depth].uri, sizeof(o->trail[o->depth].uri), "%s", it->uri);
			strip_item_name(it, o->trail[o->depth].name, sizeof(o->trail[o->depth].name));
			o->trail[o->depth].sel = o->fsel;
			o->depth++;
			o->fsel = o->ftop = 0;
			return words2(argv, "browse", o->trail[o->depth - 1].uri);
		}
		if (id == 'l' || id == NCKEY_RIGHT) return OVERLAY_TAKEN; /* into lists only */
		snprintf(o->line, sizeof(o->line), "%s", it->uri);
		words2(argv, "play", o->line);
		return OVERLAY_COMMAND;
	}
	case 'a':
		if (!n) return OVERLAY_TAKEN;
		overlay_line(&o->found.v[o->fsel], o->line, sizeof(o->line));
		argv[0] = "queue";
		argv[1] = "add";
		argv[2] = o->line;
		argv[3] = NULL;
		return OVERLAY_COMMAND;
	default:
		return OVERLAY_NOT_MINE;
	}
}

enum overlay_did overlay_key(struct overlay *o, uint32_t id, const struct ncinput *ni, char **argv)
{
	o->note[0] = '\0';
	if (!o->open && id == '/' && !(ni && (ncinput_ctrl_p(ni) || ncinput_alt_p(ni)))) {
		o->open = o->search = o->typing = 1;
		o->adding = 0;
		return OVERLAY_TAKEN;
	}
	if (o->open && o->search) return o->typing ? typing(o, id, ni, argv) : results(o, id, ni, argv);
	if (o->open && o->adding) return adding(o, id, ni, argv); /* before modifiers: its keys may have them */
	if (ni && (ncinput_ctrl_p(ni) || ncinput_alt_p(ni))) return OVERLAY_NOT_MINE;
	if (!o->open) {
		if (id != NCKEY_TAB) return OVERLAY_NOT_MINE;
		o->open = 1;
		o->adding = o->search = 0;
		return OVERLAY_OPENED;
	}
	size_t n = o->q.n;
	int shift = ni && ncinput_shift_p(ni);
	if (id == 'J' || (shift && (id == 'j' || id == NCKEY_DOWN))) return move(o, argv, 1, 0);
	if (id == 'K' || (shift && (id == 'k' || id == NCKEY_UP))) return move(o, argv, -1, 0);
	if (id == 'N' || (shift && id == 'n')) return move(o, argv, 0, 1);
	switch (id) {
	case NCKEY_TAB: case NCKEY_ESC: case 'q':
		o->open = 0;
		o->adding = 0;
		unmatch(o);
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
		overlay_scan(o); /* an empty line is home */
		return OVERLAY_TAKEN;
	case '/':
		o->search = o->typing = 1;
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

/* A result: ▸ for a list, its name, artist, album when there is room,
 * duration at the right; one label, the duration another. */
static void result(struct overlay *o, const struct theme *t, size_t i, int y, unsigned cols)
{
	struct ncplane *n = o->n;
	const struct tui_item *it = &o->found.v[i];
	int sel = i == o->fsel, end = (int)cols - 1;
	uint32_t under = sel ? bar_tint(t) : tint(t);
	if (sel) bar(n, t, y, cols);
	int dw = it->duration[0] ? cols_of(it->duration) + 2 : 0;
	if (dw) {
		char d[40];
		snprintf(d, sizeof(d), " %s", it->duration);
		ink(n, sel ? role(t, TH_FG) : role(t, TH_DIM), under);
		put(n, y, end - dw + 1, d, end);
	}
	char name[256];
	strip_item_name(it, name, sizeof(name));
	ink(n, role(t, TH_CYAN), under);
	int x = put(n, y, 1, it->kind == TUI_LIST ? "▸ " : "  ", end);
	ink(n, role(t, TH_FG), under);
	x = put(n, y, x, name, end - dw);
	if (it->artist[0] && x + 4 < end - dw) {
		ink(n, sel ? role(t, TH_FG) : role(t, TH_DIM), under);
		x = put(n, y, x, " — ", end - dw);
		ink(n, sel ? role(t, TH_FG) : role(t, TH_VIOLET), under);
		x = put(n, y, x, it->artist, end - dw);
	}
	if (it->kind == TUI_TRACK && it->album[0] && x + 8 < end - dw) {
		ink(n, sel ? role(t, TH_FG) : role(t, TH_DIM), under);
		x = put(n, y, x, " · ", end - dw);
		put(n, y, x, it->album, end - dw);
	}
}

/* The line being typed into, `label` before it, at row y. */
static void typed_line(struct overlay *o, const struct theme *t, int y, const char *label, const char *text,
                       unsigned cols)
{
	struct ncplane *n = o->n;
	int end = (int)cols - 1;
	uint32_t glass = tint(t);
	ink(n, role(t, TH_CYAN), glass);
	int x = put(n, y, 1, label, end);
	ink(n, role(t, TH_FG), glass);
	const char *s = text; /* the end of what is typed stays in view */
	while (cols_of(s) > end - x - 1 && *s) {
		s++;
		while ((*s & 0xc0) == 0x80) s++;
	}
	x = put(n, y, x, s, end);
	ink(n, role(t, TH_CYAN), glass);
	ncplane_putstr_yx(n, y, x, "▏");
}

static void draw_search(struct overlay *o, const struct theme *t, const char *msg, unsigned rows, unsigned cols)
{
	struct ncplane *n = o->n;
	uint32_t glass = tint(t);
	int end = (int)cols - 1, head = rows >= 4, line = o->typing && rows >= 2;
	int list = (int)rows - head - line, y = 0;
	if (rows == 1 && o->typing) list = 0;
	if (head) {
		ink(n, role(t, TH_MAGENTA), glass);
		int x = put(n, 0, 1, "search ", end);
		if (o->query[0] && !o->typing) {
			ink(n, role(t, TH_CYAN), glass);
			x = put(n, 0, x, o->query, end);
			for (int d = 0; d < o->depth; d++) {
				ink(n, role(t, TH_DIM), glass);
				x = put(n, 0, x, " › ", end);
				ink(n, role(t, TH_CYAN), glass);
				x = put(n, 0, x, o->trail[d].name, end);
			}
			if (o->found.n) {
				char count[48];
				snprintf(count, sizeof(count), "  %zu/%zu%s", o->fsel + 1, o->found.n, o->found.more ? "+" : "");
				ink(n, role(t, TH_DIM), glass);
				x = put(n, 0, x, count, end);
			}
		}
		x += 3;
		const char *keys = o->typing ? "⏎ searches · esc" : "j k select · ⏎ play · a queue · ⌫ back · / search · tab queue";
		int err = msg && msg[0];
		if (err) keys = msg;
		else if (o->note[0]) keys = o->note;
		int kw = cols_of(keys);
		ink(n, err ? role(t, TH_ORANGE) : o->note[0] ? role(t, TH_GREEN) : role(t, TH_DIM), glass);
		put(n, 0, end - kw > x ? end - kw : x, keys, end);
		y = 1;
	}
	if (list > 0) {
		if (!o->found.n) {
			ink(n, role(t, TH_DIM), glass);
			put(n, y, 3, !o->searched ? "type what to look for, ⏎ to look" : o->depth ? "nothing in it" : "nothing found",
			    end);
		} else {
			o->ftop = overlay_top(o->fsel, o->ftop, o->found.n, list);
			for (int i = 0; i < list && o->ftop + (size_t)i < o->found.n; i++)
				result(o, t, o->ftop + (size_t)i, y + i, cols);
		}
	}
	if (o->typing) typed_line(o, t, (int)rows - 1, "search: ", o->query, cols);
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
	if (o->search) {
		draw_search(o, t, msg, rows, cols);
		return;
	}
	uint32_t glass = tint(t);
	int head = rows >= 4, line = o->adding && rows >= 2;
	int list = (int)rows - head - line, y = 0, end = (int)cols - 1;
	char count[48];
	snprintf(count, sizeof(count), "%zu/%zu%s", o->q.n ? o->sel + 1 : 0, o->q.n, o->q.more ? "+" : "");

	if (head && o->adding) { /* where the line is looking, and its keys */
		char where[OVERLAY_INPUT + 8];
		const char *slash = strrchr(o->input, '/');
		if (o->uri) snprintf(where, sizeof(where), "a URI");
		else if (slash) snprintf(where, sizeof(where), "%.*s", (int)(slash - o->input) + 1, o->input);
		else snprintf(where, sizeof(where), "~/");
		ink(n, role(t, TH_MAGENTA), glass);
		int x = put(n, 0, 1, "add ", end);
		ink(n, role(t, TH_CYAN), glass);
		x = put(n, 0, x, where, end) + 3;
		const char *keys = msg && msg[0] ? msg : "tab completes · ⏎ adds or goes in · esc";
		int kw = cols_of(keys);
		ink(n, msg && msg[0] ? role(t, TH_ORANGE) : role(t, TH_DIM), glass);
		put(n, 0, end - kw > x ? end - kw : x, keys, end);
		y = 1;
	} else if (head) {
		ink(n, role(t, TH_MAGENTA), glass);
		int x = put(n, 0, 1, "queue ", end);
		ink(n, role(t, TH_DIM), glass);
		x = put(n, 0, x, count, end) + 3;
		const char *keys = msg && msg[0] ? msg : o->note[0] ? o->note
		                 : "j k select · J K move · N next · ⏎ play · a add · d remove · / search · tab close";
		int kw = cols_of(keys);
		ink(n, msg && msg[0] ? role(t, TH_ORANGE) : o->note[0] ? role(t, TH_GREEN) : role(t, TH_DIM), glass);
		put(n, 0, end - kw > x ? end - kw : x, keys, end);
		y = 1;
	}
	if (list > 0 && line) { /* what the path could go on to, directories first */
		ink(n, role(t, TH_DIM), glass);
		if (o->uri) {
			put(n, y, 3, "⏎ adds it as it is", end);
		} else if (!o->nmatch) {
			put(n, y, 3, "nothing here starts with that", end);
		} else {
			size_t room = (size_t)list, shown = o->nmatch < room ? o->nmatch : room;
			int over = o->nmatch + o->more > shown;
			if (over) shown = room - 1;
			for (size_t i = 0; i < shown; i++) {
				size_t l = strlen(o->match[i]);
				ink(n, l && o->match[i][l - 1] == '/' ? role(t, TH_CYAN) : role(t, TH_FG), glass);
				put(n, y + (int)i, 3, o->match[i], end);
			}
			if (over) {
				char more[48];
				snprintf(more, sizeof(more), "… and %zu more", o->nmatch + o->more - shown);
				ink(n, role(t, TH_DIM), glass);
				put(n, y + (int)shown, 3, more, end);
			}
		}
	} else if (list > 0) {
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
		if (rows == 1) ncplane_erase(n);
		typed_line(o, t, (int)rows - 1, "add: ", o->input, cols);
	}
}
