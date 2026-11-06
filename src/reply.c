/* SPDX-License-Identifier: LGPL-2.1-or-later */
/** @file reply.c
 * @brief Reply text into structs. */

#include "reply.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ status */

static void put(char *dst, size_t cap, const char *v, size_t n)
{
	if (n == 1 && v[0] == '-') n = 0;
	if (n >= cap) n = cap - 1;
	memcpy(dst, v, n);
	dst[n] = '\0';
}

int tui_status_read(const char *text, struct tui_status *st)
{
	memset(st, 0, sizeof(*st));
	int seen_state = 0;
	for (const char *l = text; *l; ) {
		const char *nl = strchr(l, '\n');
		size_t len = nl ? (size_t)(nl - l) : strlen(l);
		const char *sep = memchr(l, ':', len);
		if (sep && sep + 1 < l + len && sep[1] == ' ') {
			size_t kl = (size_t)(sep - l);
			const char *v = sep + 2;
			size_t vl = len - kl - 2;
			char num[64];
			put(num, sizeof(num), v, vl);
#define KEY(k) (kl == sizeof(k) - 1 && !memcmp(l, k, kl))
			if (KEY("state")) {
				seen_state = 1;
				st->state = !strcmp(num, "playing") ? TUI_PLAYING : !strcmp(num, "paused") ? TUI_PAUSED : TUI_STOPPED;
			}
			else if (KEY("item")) st->item = strtoul(num, NULL, 10);
			else if (KEY("uri")) put(st->uri, sizeof(st->uri), v, vl);
			else if (KEY("source")) put(st->source, sizeof(st->source), v, vl);
			else if (KEY("title")) put(st->title, sizeof(st->title), v, vl);
			else if (KEY("artist")) put(st->artist, sizeof(st->artist), v, vl);
			else if (KEY("album")) put(st->album, sizeof(st->album), v, vl);
			else if (KEY("position")) st->position = strtod(num, NULL);
			else if (KEY("duration")) st->duration = strtod(num, NULL);
			else if (KEY("seekable")) st->seekable = !strcmp(num, "yes");
			else if (KEY("volume")) st->volume = atoi(num);
			else if (KEY("next")) put(st->next, sizeof(st->next), v, vl);
#undef KEY
		}
		l += len + (nl ? 1 : 0);
	}
	return seen_state ? 0 : -1;
}

/* ------------------------------------------------------------------ lists */

void tui_list_free(struct tui_list *l)
{
	free(l->text);
	free(l->v);
	memset(l, 0, sizeof(*l));
}

static const char *field(char **cur)
{
	char *s = *cur, *t = strchr(s, '\t');
	if (t) { *t = '\0'; *cur = t + 1; }
	else *cur = s + strlen(s);
	return strcmp(s, "-") ? s : "";
}

/* Split the copy into lines, give each to `row`; it returns 0 for a row,
 * 1 for a line to pass over quietly, -1 for one it cannot read. */
static int each_line(struct tui_list *l, const char *text, int (*row)(char *, struct tui_item *, struct tui_list *))
{
	tui_list_free(l);
	size_t lines = 1;
	for (const char *c = text; *c; c++) lines += *c == '\n';
	l->text = strdup(text);
	l->v = calloc(lines, sizeof(*l->v));
	if (!l->text || !l->v) {
		tui_list_free(l);
		return -1;
	}
	int bad = 0;
	for (char *s = l->text; *s; ) {
		char *nl = strchr(s, '\n');
		if (nl) *nl = '\0';
		if (*s) {
			struct tui_item it = { 0 };
			int r = row(s, &it, l);
			if (r == 0) l->v[l->n++] = it;
			else if (r < 0) bad++;
		}
		if (!nl) break;
		s = nl + 1;
	}
	return bad;
}

static int item_row(char *s, struct tui_item *it, struct tui_list *l)
{
	if (!strcmp(s, "empty") || !strcmp(s, "no results")) return 1;
	if (!strncmp(s, "... ", 4)) { l->more = 1; return 1; }
	if (!strchr(s, '\t')) return -1;
	const char *kind = field(&s);
	if (!strcmp(kind, "track")) it->kind = TUI_TRACK;
	else if (!strcmp(kind, "list")) it->kind = TUI_LIST;
	else return -1;
	it->uri = field(&s);
	it->title = field(&s);
	it->artist = field(&s);
	it->album = field(&s);
	it->duration = field(&s);
	return it->uri[0] ? 0 : -1;
}

int tui_list_items(struct tui_list *l, const char *text) { return each_line(l, text, item_row); }

static int queue_row(char *s, struct tui_item *it, struct tui_list *l)
{
	if (!strcmp(s, "queue is empty")) return 1;
	if (!strcmp(s, "...")) { l->more = 1; return 1; }
	it->kind = TUI_TRACK;
	if (*s == '>') { it->cursor = 1; s++; }
	while (*s == ' ') s++;
	char *end;
	it->pos = strtol(s, &end, 10);
	if (end == s || it->pos < 1) return -1;
	s = end;
	it->title = it->artist = it->album = it->duration = "";
	if (*s == '\t') { /* -l */
		s++;
		it->uri = field(&s);
		it->title = field(&s);
		it->artist = field(&s);
		it->album = field(&s);
		it->duration = field(&s);
	} else { /* "> 1  uri": two spaces, then the URI as it is */
		if (strncmp(s, "  ", 2)) return -1;
		it->uri = s + 2;
	}
	return it->uri[0] ? 0 : -1;
}

int tui_list_queue(struct tui_list *l, const char *text) { return each_line(l, text, queue_row); }

/* ------------------------------------------------------------------ watch */

static int hex2(const char *s, uint8_t *out)
{
	unsigned v;
	char b[3] = { s[0], s[0] ? s[1] : 0, 0 };
	if (!b[0] || !b[1] || sscanf(b, "%2x", &v) != 1 || strspn(b, "0123456789abcdef") != 2) return -1;
	*out = (uint8_t)v;
	return 0;
}

static unsigned levels(const char *s, struct tui_levels *lv)
{
	struct tui_levels t;
	if (hex2(s, &t.peak[0]) || s[2] != ' ' || hex2(s + 3, &t.peak[1]) || s[5] != ' ') return 0;
	s += 6;
	for (int k = 0; k < TUI_BANDS; k++, s += 2)
		if (hex2(s, &t.band[k])) return 0;
	if (*s && *s != '\n') return 0;
	if (lv) *lv = t;
	return TUI_EV_LEVELS;
}

unsigned tui_watch_line(const char *line, struct tui_levels *lv)
{
	static const struct { const char *name; unsigned bit; } N[] = {
		{ "state", TUI_EV_STATE }, { "track", TUI_EV_TRACK }, { "volume", TUI_EV_VOLUME },
		{ "seek", TUI_EV_SEEK }, { "queue", TUI_EV_QUEUE }, { "meta", TUI_EV_META },
	};
	if (!strncmp(line, "levels ", 7)) return levels(line + 7, lv);
	if (!strncmp(line, "ok ", 3) || !strncmp(line, "err ", 4)) return 0;
	unsigned ev = 0;
	for (const char *w = line; *w && *w != '\n'; ) {
		size_t n = strcspn(w, " \n");
		for (size_t i = 0; i < sizeof(N) / sizeof(N[0]); i++)
			if (strlen(N[i].name) == n && !memcmp(w, N[i].name, n)) ev |= N[i].bit;
		w += n;
		while (*w == ' ') w++;
	}
	return ev;
}
