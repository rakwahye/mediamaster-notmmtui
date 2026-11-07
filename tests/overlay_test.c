/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* The queue overlay's keys, reading and window, without a terminal. */
#include "overlay.h"
#include "check.h"
#include <notcurses/notcurses.h>
#include <string.h>

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

static int adding(void)
{
	struct overlay o;
	memset(&o, 0, sizeof(o));
	key(&o, NCKEY_TAB, NULL, 0);
	overlay_set(&o, Q, 1);
	CHECK(key(&o, 'a', "a", 0) == OVERLAY_TAKEN && o.adding, "a: the add line");
	key(&o, 's', "s", 0);
	key(&o, 'q', "q", 0); /* typed, not closing */
	key(&o, ' ', " ", 0);
	key(&o, 0xe9, "\xc3\xa9", 0);
	CHECK(o.open && !strcmp(o.input, "sq \xc3\xa9"), "every key is the line's");
	key(&o, NCKEY_BACKSPACE, NULL, 0);
	CHECK(!strcmp(o.input, "sq "), "backspace: a whole letter");
	key(&o, 0x7f, NULL, 0);
	key(&o, NCKEY_LEFT, NULL, 0); /* no text */
	CHECK(!strcmp(o.input, "sq"), "delete too; an arrow types nothing");
	CHECK(key(&o, NCKEY_ENTER, NULL, 0) == OVERLAY_COMMAND && is("queue", "add", "sq") && !o.adding,
	      "enter adds it, as one word");
	key(&o, 'a', "a", 0);
	CHECK(o.input[0] == '\0', "a fresh line");
	CHECK(key(&o, NCKEY_ENTER, NULL, 0) == OVERLAY_TAKEN && !o.adding, "nothing typed: nothing added");
	key(&o, 'a', "a", 0);
	key(&o, 'x', "x", 0);
	CHECK(key(&o, NCKEY_ESC, NULL, 0) == OVERLAY_TAKEN && !o.adding && o.open, "esc leaves the line, not the queue");
	for (int i = 0; i < 400; i++) key(&o, 'z', "z", 0);
	CHECK(strlen(o.input) < OVERLAY_INPUT, "never past the end");
	tui_list_free(&o.q);
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
	RUN(adding);
	RUN(window);
	CHECK_PASS("test-overlay");
}
