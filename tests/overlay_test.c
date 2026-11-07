/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* The queue overlay's keys, reading and window, without a terminal. */
#include "overlay.h"
#include "check.h"
#include <notcurses/notcurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const char *Q = "1\t/home/u/music/Osc/two.flac\tTab Title\tOsc\tLP: One\t0:03\n"
                       "2\tsine:440:2\t-\t-\t-\t-\n"
                       ">3\tremote:track:4ptH\tTime\tPink Floyd\tThe Dark Side of the Moon\t6:53\n"
                       "4\tsine:220:30\t-\t-\t-\t-\n";

static char *argv[OVERLAY_ARGV];

static enum overlay_did key(struct overlay *o, uint32_t id, const char *utf8, unsigned mods)
{
	ncinput ni;
	memset(&ni, 0, sizeof(ni));
	ni.id = id;
	ni.modifiers = mods;
	if (utf8) snprintf(ni.utf8, sizeof(ni.utf8), "%s", utf8);
	memset(argv, 0, sizeof(argv));
	return overlay_key(o, id, &ni, argv);
}

static int is(const char *a0, const char *a1, const char *a2)
{
	return argv[0] && !strcmp(argv[0], a0) && argv[1] && !strcmp(argv[1], a1) && argv[2] && !strcmp(argv[2], a2) &&
	       !argv[3];
}

static int is4(const char *a0, const char *a1, const char *a2, const char *a3)
{
	return argv[0] && !strcmp(argv[0], a0) && argv[1] && !strcmp(argv[1], a1) && argv[2] && !strcmp(argv[2], a2) &&
	       argv[3] && !strcmp(argv[3], a3) && !argv[4];
}

static int moves(void)
{
	struct overlay o;
	memset(&o, 0, sizeof(o));
	key(&o, NCKEY_TAB, NULL, 0);
	overlay_set(&o, Q, 1); /* four, the third playing, selected */
	CHECK(key(&o, 'J', "J", NCKEY_MOD_SHIFT) == OVERLAY_COMMAND && is4("queue", "mv", "3", "4") && o.sel == 3,
	      "J: one down, the selection with it");
	CHECK(key(&o, 'J', "J", NCKEY_MOD_SHIFT) == OVERLAY_TAKEN && o.sel == 3, "not past the last");
	CHECK(key(&o, 'K', "K", NCKEY_MOD_SHIFT) == OVERLAY_COMMAND && is4("queue", "mv", "4", "3") && o.sel == 2,
	      "K: one up");
	CHECK(key(&o, NCKEY_UP, NULL, NCKEY_MOD_SHIFT) == OVERLAY_COMMAND && is4("queue", "mv", "3", "2") && o.sel == 1,
	      "shift+up too");
	CHECK(key(&o, NCKEY_DOWN, NULL, NCKEY_MOD_SHIFT) == OVERLAY_COMMAND && is4("queue", "mv", "2", "3"),
	      "shift+down too");
	o.sel = 0;
	CHECK(key(&o, 'K', "K", NCKEY_MOD_SHIFT) == OVERLAY_TAKEN && o.sel == 0, "not above the first");
	CHECK(key(&o, NCKEY_DOWN, NULL, 0) == OVERLAY_TAKEN && o.sel == 1, "down without shift only selects");

	/* N: next after the item playing (the third) */
	o.sel = 3;
	CHECK(key(&o, 'N', "N", NCKEY_MOD_SHIFT) == OVERLAY_TAKEN && o.sel == 3, "N on what is already next: nothing");
	char q5[512];
	snprintf(q5, sizeof(q5), "%s5\tsine:330:30\t-\t-\t-\t-\n", Q);
	overlay_set(&o, q5, 0);
	o.sel = 4;
	CHECK(key(&o, 'N', "N", NCKEY_MOD_SHIFT) == OVERLAY_COMMAND && is4("queue", "mv", "5", "next") && o.sel == 3,
	      "N from below: right after the playing one");
	o.sel = 0;
	CHECK(key(&o, 'N', "N", NCKEY_MOD_SHIFT) == OVERLAY_COMMAND && is4("queue", "mv", "1", "next") && o.sel == 2,
	      "N from above: the list closes up behind it, so it lands where the playing one was");
	o.sel = 2;
	CHECK(key(&o, 'N', "N", NCKEY_MOD_SHIFT) == OVERLAY_TAKEN, "N on the item playing: nothing to do");
	overlay_set(&o, "1\ta\t-\t-\t-\t-\n2\tb\t-\t-\t-\t-\n", 0);
	o.sel = 1;
	CHECK(key(&o, 'N', "N", NCKEY_MOD_SHIFT) == OVERLAY_COMMAND && is4("queue", "mv", "2", "next") && o.sel == 0,
	      "nothing playing: next is the top");
	o.sel = 0;
	CHECK(key(&o, 'N', "N", NCKEY_MOD_SHIFT) == OVERLAY_TAKEN, "already the top");
	CHECK(key(&o, 'n', "n", 0) == OVERLAY_NOT_MINE, "n is still the strip's: the next track");
	tui_list_free(&o.q);
	return 0;
}

static int opening(void)
{
	struct overlay o;
	memset(&o, 0, sizeof(o));
	CHECK(key(&o, 'j', "j", 0) == OVERLAY_NOT_MINE && !o.open, "closed: j is not its");
	CHECK(key(&o, ' ', " ", 0) == OVERLAY_NOT_MINE, "nor space");
	CHECK(key(&o, NCKEY_TAB, NULL, 0) == OVERLAY_OPENED && o.open, "tab opens it");
	overlay_set(&o, Q, 1);
	CHECK(o.q.n == 4 && o.sel == 2, "first read: the item playing selected");
	CHECK(key(&o, ' ', " ", 0) == OVERLAY_NOT_MINE, "open: space is still the strip's");
	CHECK(key(&o, 'n', "n", 0) == OVERLAY_NOT_MINE, "and n");
	CHECK(key(&o, 'j', "j", NCKEY_MOD_CTRL) == OVERLAY_NOT_MINE, "a ctrl key is not its");
	CHECK(key(&o, 'q', "q", 0) == OVERLAY_CLOSED && !o.open, "q closes it");
	CHECK(key(&o, NCKEY_TAB, NULL, 0) == OVERLAY_OPENED, "tab again");
	CHECK(key(&o, NCKEY_ESC, NULL, 0) == OVERLAY_CLOSED, "esc closes it");
	CHECK(key(&o, NCKEY_TAB, NULL, 0) == OVERLAY_OPENED && key(&o, NCKEY_TAB, NULL, 0) == OVERLAY_CLOSED,
	      "tab closes it too");
	tui_list_free(&o.q);
	return 0;
}

static int moving(void)
{
	struct overlay o;
	memset(&o, 0, sizeof(o));
	key(&o, NCKEY_TAB, NULL, 0);
	overlay_set(&o, Q, 1);
	CHECK(key(&o, 'j', "j", 0) == OVERLAY_TAKEN && o.sel == 3, "j: down");
	key(&o, NCKEY_DOWN, NULL, 0);
	CHECK(o.sel == 3, "not past the last");
	key(&o, 'k', "k", 0);
	key(&o, NCKEY_UP, NULL, 0);
	CHECK(o.sel == 1, "k and up: up");
	key(&o, 'g', "g", 0);
	CHECK(o.sel == 0, "g: the first");
	key(&o, 'k', "k", 0);
	CHECK(o.sel == 0, "not before it");
	key(&o, 'G', "G", 0);
	CHECK(o.sel == 3, "G: the last");

	CHECK(key(&o, NCKEY_ENTER, NULL, 0) == OVERLAY_COMMAND && is("queue", "play", "4"), "enter plays it, by position");
	key(&o, 'k', "k", 0);
	CHECK(key(&o, 'd', "d", 0) == OVERLAY_COMMAND && is("queue", "rm", "3"), "d removes it");

	overlay_set(&o, "1\ta\t-\t-\t-\t-\n>2\tb\t-\t-\t-\t-\n", 0);
	CHECK(o.sel == 1, "read again shorter: held to the list");
	overlay_set(&o, "queue is empty\n", 0);
	CHECK(o.q.n == 0 && o.sel == 0, "empty");
	CHECK(key(&o, NCKEY_ENTER, NULL, 0) == OVERLAY_TAKEN && key(&o, 'd', "d", 0) == OVERLAY_TAKEN,
	      "nothing to play or remove: no command");
	overlay_set(&o, NULL, 0);
	CHECK(o.q.n == 0, "no reply: empty");
	tui_list_free(&o.q);
	return 0;
}

static char home[256];

static void touch(const char *rel)
{
	char p[512];
	snprintf(p, sizeof(p), "%s/%s", home, rel);
	size_t n = strlen(p);
	if (p[n - 1] == '/') {
		p[n - 1] = '\0';
		mkdir(p, 0700);
		return;
	}
	FILE *f = fopen(p, "w");
	if (f) fclose(f);
}

static void typed(struct overlay *o, const char *s)
{
	for (; *s; s++) {
		char u[2] = { *s, 0 };
		key(o, (unsigned char)*s, u, 0);
	}
}

static int adding(void)
{
	snprintf(home, sizeof(home), "/tmp/notmmtui-test.XXXXXX");
	CHECK(mkdtemp(home), "a home to look in");
	touch("Music/");
	touch("Music/Osc/");
	touch("Music/Osc/one.flac");
	touch("Music/Osc/two.flac");
	touch("Music/Osc/twins.flac");
	touch("Mail/");
	touch("mix.flac");
	touch(".hidden");

	struct overlay o;
	memset(&o, 0, sizeof(o));
	snprintf(o.home, sizeof(o.home), "%s", home);
	key(&o, NCKEY_TAB, NULL, 0);
	overlay_set(&o, Q, 1);
	CHECK(key(&o, 'a', "a", 0) == OVERLAY_TAKEN && o.adding, "a: the add line");
	CHECK(o.nmatch == 3 && !strcmp(o.match[0], "Mail/") && !strcmp(o.match[1], "Music/") &&
	      !strcmp(o.match[2], "mix.flac"), "empty: home, directories first, nothing hidden");

	typed(&o, "M");
	CHECK(o.nmatch == 2, "as you type, what starts with it");
	key(&o, NCKEY_TAB, NULL, 0);
	CHECK(!strcmp(o.input, "M"), "tab: two ways on, no further than they agree");
	typed(&o, "u");
	key(&o, NCKEY_TAB, NULL, 0);
	CHECK(!strcmp(o.input, "Music/") && o.nmatch == 1, "one way: completed, a directory with its /");
	key(&o, NCKEY_TAB, NULL, 0);
	CHECK(!strcmp(o.input, "Music/Osc/"), "and on");
	typed(&o, "t");
	key(&o, NCKEY_TAB, NULL, 0);
	CHECK(!strcmp(o.input, "Music/Osc/tw"), "as far as two and twins agree");
	typed(&o, "o");
	char want[512];
	snprintf(want, sizeof(want), "%s/Music/Osc/two.flac", home);
	CHECK(key(&o, NCKEY_ENTER, NULL, 0) == OVERLAY_COMMAND && is("queue", "add", want) && !o.adding,
	      "enter: no such file, but one match: added, whole");

	key(&o, 'a', "a", 0);
	typed(&o, "~/Music/Osc");
	CHECK(key(&o, NCKEY_ENTER, NULL, 0) == OVERLAY_TAKEN && !strcmp(o.input, "~/Music/Osc/") && o.nmatch == 3,
	      "enter on a directory goes into it; ~ is home");
	typed(&o, "one.flac");
	snprintf(want, sizeof(want), "%s/Music/Osc/one.flac", home);
	CHECK(key(&o, NCKEY_ENTER, NULL, 0) == OVERLAY_COMMAND && is("queue", "add", want), "a file: added, whole");

	key(&o, 'a', "a", 0);
	typed(&o, "Ma");
	CHECK(key(&o, NCKEY_ENTER, NULL, 0) == OVERLAY_TAKEN && !strcmp(o.input, "Mail/"),
	      "enter on the start of one directory: into it");
	key(&o, 'w', "w", NCKEY_MOD_CTRL);
	CHECK(o.input[0] == '\0', "ctrl+w: back a directory");
	typed(&o, "Music/Osc/");
	key(&o, 'w', "w", NCKEY_MOD_CTRL);
	CHECK(!strcmp(o.input, "Music/"), "one at a time");
	key(&o, 'u', "u", NCKEY_MOD_CTRL);
	CHECK(o.input[0] == '\0' && o.adding, "ctrl+u: the line cleared, still up");
	key(&o, '~', "~", NCKEY_MOD_CTRL);
	CHECK(!strcmp(o.input, "~"), "a ~ behind ctrl is a ~");
	key(&o, NCKEY_TAB, NULL, 0);
	CHECK(!strcmp(o.input, "~/"), "~ and tab: ~/");
	key(&o, 'x', "x", NCKEY_MOD_ALT);
	key(&o, '1', "1", NCKEY_MOD_CTRL);
	CHECK(!strcmp(o.input, "~/"), "ctrl or alt with a letter or digit types nothing");
	typed(&o, ".h");
	CHECK(o.nmatch == 1 && !strcmp(o.match[0], ".hidden"), "hidden when asked for");
	key(&o, 'u', "u", NCKEY_MOD_CTRL);
	typed(&o, "nothing");
	CHECK(o.nmatch == 0, "nothing starts with that");

	key(&o, 'u', "u", NCKEY_MOD_CTRL);
	typed(&o, "sine:440:9");
	CHECK(o.uri && o.nmatch == 0, "a URI: no paths");
	CHECK(key(&o, NCKEY_ENTER, NULL, 0) == OVERLAY_COMMAND && is("queue", "add", "sine:440:9"), "added as typed");

	key(&o, 'a', "a", 0);
	typed(&o, "sq");
	key(&o, NCKEY_BACKSPACE, NULL, 0);
	key(&o, 0x7f, NULL, 0);
	key(&o, NCKEY_LEFT, NULL, 0);
	CHECK(o.input[0] == '\0', "backspace, delete; an arrow types nothing");
	key(&o, 0xe9, "\xc3\xa9", 0);
	key(&o, NCKEY_BACKSPACE, NULL, 0);
	CHECK(o.input[0] == '\0', "a whole letter at a time");
	CHECK(key(&o, NCKEY_ENTER, NULL, 0) == OVERLAY_TAKEN && o.adding, "nothing typed: nothing added");
	typed(&o, "q");
	CHECK(o.open && !strcmp(o.input, "q"), "q is typed, not closing");
	CHECK(key(&o, NCKEY_ESC, NULL, 0) == OVERLAY_TAKEN && !o.adding && o.open && !o.nmatch,
	      "esc leaves the line, not the queue");
	key(&o, 'a', "a", 0);
	for (int i = 0; i < 400; i++) key(&o, 'z', "z", 0);
	CHECK(strlen(o.input) < OVERLAY_INPUT, "never past the end");
	key(&o, NCKEY_ESC, NULL, 0);
	tui_list_free(&o.q);

	char cmd[300];
	snprintf(cmd, sizeof(cmd), "rm -rf '%s'", home);
	CHECK(system(cmd) == 0, "the home cleared away");
	return 0;
}

static int expand(void)
{
	char out[256];
	CHECK(overlay_expand("~/a", "/h", out, sizeof(out)) == 1 && !strcmp(out, "/h/a"), "~/ is home");
	CHECK(overlay_expand("~", "/h", out, sizeof(out)) == 1 && !strcmp(out, "/h/"), "~ alone too");
	CHECK(overlay_expand("a/b", "/h/", out, sizeof(out)) == 1 && !strcmp(out, "/h/a/b"), "relative: from home");
	CHECK(overlay_expand("", "/h", out, sizeof(out)) == 1 && !strcmp(out, "/h/"), "nothing: home");
	CHECK(overlay_expand("/x/y", "/h", out, sizeof(out)) == 1 && !strcmp(out, "/x/y"), "whole: as it is");
	CHECK(overlay_expand("sine:440:9", "/h", out, sizeof(out)) == 0 && !strcmp(out, "sine:440:9"), "a URI");
	CHECK(overlay_expand("http://x/y", "/h", out, sizeof(out)) == 0, "with //");
	CHECK(overlay_expand("./a:b.flac", "/h", out, sizeof(out)) == 1 && !strcmp(out, "/h/./a:b.flac"),
	      "a name with : starting ./ is a path");
	CHECK(overlay_expand("~x", "/h", out, sizeof(out)) == 1 && !strcmp(out, "/h/~x"), "~x: a name in home");
	return 0;
}

static const char *FOUND = "list\tlib:album/osc/lp-one\tLP: One\tOsc\t-\t-\n"
                           "track\t/home/u/music/Osc/ten.flac\tTen\tOsc\t-\t0:02\n"
                           "track\t/home/u/music/Osc/two.flac\tTab Title\tOsc\tLP: One\t0:03\n";

static int is2(const char *a0, const char *a1) { return argv[0] && !strcmp(argv[0], a0) && argv[1] && !strcmp(argv[1], a1) && !argv[2]; }

static int searching(void)
{
	struct overlay o;
	memset(&o, 0, sizeof(o));
	CHECK(key(&o, '/', "/", 0) == OVERLAY_TAKEN && o.open && o.search && o.typing, "/ from closed: search, typing");
	CHECK(key(&o, NCKEY_ENTER, NULL, 0) == OVERLAY_TAKEN && o.typing, "nothing typed: nothing asked");
	typed(&o, "osc");
	CHECK(key(&o, 'q', "q", 0) == OVERLAY_TAKEN && !strcmp(o.query, "oscq"), "q is typed");
	key(&o, NCKEY_BACKSPACE, NULL, 0);
	CHECK(key(&o, NCKEY_ENTER, NULL, 0) == OVERLAY_FETCH && is2("search", "osc") && !o.typing, "enter: search");
	overlay_found(&o, FOUND);
	CHECK(o.found.n == 3 && o.fsel == 0 && o.searched, "the results");

	CHECK(key(&o, 'j', "j", 0) == OVERLAY_TAKEN && o.fsel == 1, "j selects");
	CHECK(key(&o, NCKEY_ENTER, NULL, 0) == OVERLAY_COMMAND && is2("play", "/home/u/music/Osc/ten.flac"),
	      "enter on a track: play it by its URI");
	CHECK(key(&o, 'a', "a", 0) == OVERLAY_COMMAND && argv[0] && !strcmp(argv[0], "queue") && !strcmp(argv[1], "add") &&
	      !strcmp(argv[2], "track\t/home/u/music/Osc/ten.flac\tTen\tOsc\t-\t0:02") && !argv[3],
	      "a: queue it by its line");
	overlay_note(&o, "added at 7\n");
	CHECK(!strcmp(o.note, "added at 7"), "the daemon's word, kept");
	key(&o, 'k', "k", 0);
	CHECK(!o.note[0] && o.fsel == 0, "until the next key");

	CHECK(key(&o, NCKEY_ENTER, NULL, 0) == OVERLAY_FETCH && is2("browse", "lib:album/osc/lp-one") && o.depth == 1 &&
	      !strcmp(o.trail[0].name, "LP: One") && o.fsel == 0, "enter on a list: into it");
	overlay_found(&o, "track\t/x/one.flac\tOne\tOsc\tLP: One\t0:04\n");
	CHECK(key(&o, 'l', "l", 0) == OVERLAY_TAKEN, "l on a track: only lists go in");
	o.fsel = 0;
	key(&o, 'j', "j", 0);
	CHECK(o.fsel == 0, "not past the last");
	o.depth = 1;
	o.trail[0].sel = 0;
	CHECK(key(&o, NCKEY_BACKSPACE, NULL, 0) == OVERLAY_FETCH && is2("search", "osc") && o.depth == 0 && o.fsel == 0,
	      "back out: the search asked again, the selection where it was");
	overlay_found(&o, FOUND);
	o.fsel = 2;
	key(&o, 'k', "k", 0);
	key(&o, 'k', "k", 0);
	key(&o, NCKEY_ENTER, NULL, 0); /* into the list from the top */
	CHECK(o.depth == 1 && o.trail[0].sel == 0, "the selection to come back to");
	CHECK(key(&o, NCKEY_LEFT, NULL, 0) == OVERLAY_FETCH && o.depth == 0, "left is back too");
	overlay_found(&o, FOUND);
	CHECK(key(&o, NCKEY_BACKSPACE, NULL, 0) == OVERLAY_TAKEN && o.typing, "back at the top: the line");
	CHECK(key(&o, NCKEY_ESC, NULL, 0) == OVERLAY_TAKEN && !o.typing && o.open, "esc on the line: the results");
	CHECK(key(&o, '/', "/", 0) == OVERLAY_TAKEN && o.typing && !strcmp(o.query, "osc"), "/ : the line, as it was");
	typed(&o, "x");
	CHECK(!o.searched, "an edited query is not searched yet");
	CHECK(key(&o, NCKEY_ESC, NULL, 0) == OVERLAY_CLOSED && !o.open, "esc with nothing searched: closed");

	key(&o, '/', "/", 0);
	CHECK(key(&o, NCKEY_TAB, NULL, 0) == OVERLAY_OPENED && !o.search && o.open, "tab on the line: the queue");
	CHECK(key(&o, '/', "/", 0) == OVERLAY_TAKEN && o.search && o.typing, "/ from the queue: search");
	key(&o, NCKEY_ENTER, NULL, 0);
	overlay_found(&o, FOUND);
	CHECK(key(&o, NCKEY_TAB, NULL, 0) == OVERLAY_OPENED && !o.search, "tab on the results: the queue");
	CHECK(key(&o, NCKEY_TAB, NULL, 0) == OVERLAY_CLOSED, "and tab closes it");
	CHECK(key(&o, NCKEY_TAB, NULL, 0) == OVERLAY_OPENED && !o.search, "tab from closed: the queue, not search");
	key(&o, '/', "/", 0);
	key(&o, NCKEY_ESC, NULL, 0); /* the results again */
	CHECK(key(&o, ' ', " ", 0) == OVERLAY_NOT_MINE && key(&o, 'n', "n", 0) == OVERLAY_NOT_MINE,
	      "on the results, space and n are the strip's");
	CHECK(key(&o, 'q', "q", 0) == OVERLAY_CLOSED && !o.search, "q closes");

	tui_list_free(&o.found);
	memset(&o, 0, sizeof(o));
	key(&o, '/', "/", 0);
	typed(&o, "deep");
	key(&o, NCKEY_ENTER, NULL, 0);
	for (int d = 0; d < OVERLAY_DEPTH; d++) {
		overlay_found(&o, FOUND);
		o.fsel = 0;
		CHECK(key(&o, NCKEY_ENTER, NULL, 0) == OVERLAY_FETCH, "in and in");
	}
	overlay_found(&o, FOUND);
	o.fsel = 0;
	CHECK(o.depth == OVERLAY_DEPTH && key(&o, NCKEY_ENTER, NULL, 0) == OVERLAY_TAKEN, "but no deeper than the trail");
	overlay_found(&o, NULL);
	CHECK(o.found.n == 0 && key(&o, 'a', "a", 0) == OVERLAY_TAKEN && key(&o, NCKEY_ENTER, NULL, 0) == OVERLAY_TAKEN,
	      "no results: nothing to play or queue");
	tui_list_free(&o.found);

	struct tui_item it = { .kind = TUI_LIST, .uri = "u", .title = "", .artist = "A", .album = "", .duration = "" };
	char line[128];
	overlay_line(&it, line, sizeof(line));
	CHECK(!strcmp(line, "list\tu\t-\tA\t-\t-"), "a line: empty fields as -, as the daemon prints them");
	return 0;
}

static int window(void)
{
	CHECK(overlay_top(2, 0, 10, 5) == 0, "in view: stays");
	CHECK(overlay_top(7, 0, 10, 5) == 3, "below: just enough to show it");
	CHECK(overlay_top(1, 3, 10, 5) == 1, "above: up to it");
	CHECK(overlay_top(9, 2, 10, 5) == 5, "the last: at the bottom");
	CHECK(overlay_top(4, 8, 10, 5) == 4, "no empty rows below while there is more above");
	CHECK(overlay_top(1, 0, 3, 5) == 0, "fewer than the rows: from the top");
	CHECK(overlay_top(0, 0, 0, 5) == 0 && overlay_top(3, 2, 10, 0) == 0, "nothing, or no room");
	CHECK(overlay_top(4, 0, 10, 1) == 4, "one row: the one selected");
	return 0;
}

int main(void)
{
	RUN(opening);
	RUN(moving);
	RUN(moves);
	RUN(expand);
	RUN(adding);
	RUN(searching);
	RUN(window);
	CHECK_PASS("test-overlay");
}
