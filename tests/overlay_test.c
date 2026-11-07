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
	RUN(expand);
	RUN(adding);
	RUN(window);
	CHECK_PASS("test-overlay");
}
