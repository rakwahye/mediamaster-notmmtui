/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Keys to commands, without a terminal. */
#include "keys.h"
#include "check.h"
#include <notcurses/notcurses.h>
#include <string.h>

static int is(uint32_t id, unsigned mods, const char *want0, const char *want1)
{
	ncinput ni;
	memset(&ni, 0, sizeof(ni));
	ni.id = id;
	ni.modifiers = mods;
	char *argv[KEYS_ARGV_MAX], buf[32];
	int argc = keys_command(id, &ni, argv, buf);
	if (!want0) return argc == 0;
	if (argc != (want1 ? 2 : 1) || strcmp(argv[0], want0) || argv[argc]) return 0;
	return !want1 || !strcmp(argv[1], want1);
}

static int map(void)
{
	CHECK(is(' ', 0, "toggle", NULL), "space toggles");
	CHECK(is('n', 0, "next", NULL) && is('p', 0, "prev", NULL), "n, p");
	CHECK(is('s', 0, "stop", NULL), "s stops");
	CHECK(is(NCKEY_LEFT, 0, "seek", "-5") && is(NCKEY_RIGHT, 0, "seek", "+5"), "arrows seek 5 s");
	CHECK(is(NCKEY_LEFT, NCKEY_MOD_SHIFT, "seek", "-30") && is(NCKEY_RIGHT, NCKEY_MOD_SHIFT, "seek", "+30"),
	      "shifted arrows seek 30 s");
	CHECK(is('+', 0, "vol", "+5") && is('=', 0, "vol", "+5") && is('-', 0, "vol", "-5"), "volume");
	CHECK(is('x', 0, NULL, NULL) && is('q', 0, NULL, NULL), "other keys are not commands");
	CHECK(is('n', NCKEY_MOD_CTRL, NULL, NULL) && is(' ', NCKEY_MOD_ALT, NULL, NULL), "not with ctrl or alt");
	return 0;
}

int main(void)
{
	RUN(map);
	CHECK_PASS("test-keys");
}
