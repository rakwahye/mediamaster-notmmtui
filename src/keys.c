/* SPDX-License-Identifier: LGPL-2.1-or-later */
/** @file keys.c
 * @brief The transport keys. */

#include "keys.h"
#include <notcurses/notcurses.h>
#include <stdio.h>

#define SEEK_S 5
#define SEEK_BIG_S 30
#define VOL_STEP 5

static int one(char **argv, const char *verb)
{
	argv[0] = (char *)verb;
	argv[1] = NULL;
	return 1;
}

static int two(char **argv, const char *verb, char *buf, char sign, int v)
{
	snprintf(buf, 32, "%c%d", sign, v);
	argv[0] = (char *)verb;
	argv[1] = buf;
	argv[2] = NULL;
	return 2;
}

int keys_command(uint32_t id, const struct ncinput *ni, char **argv, char *buf)
{
	int shift = ncinput_shift_p(ni);
	if (ncinput_ctrl_p(ni) || ncinput_alt_p(ni)) return 0;
	switch (id) {
	case ' ': return one(argv, "toggle");
	case 'n': return one(argv, "next");
	case 'p': return one(argv, "prev");
	case 's': return one(argv, "stop");
	case NCKEY_LEFT: return two(argv, "seek", buf, '-', shift ? SEEK_BIG_S : SEEK_S);
	case NCKEY_RIGHT: return two(argv, "seek", buf, '+', shift ? SEEK_BIG_S : SEEK_S);
	case '+': case '=': return two(argv, "vol", buf, '+', VOL_STEP);
	case '-': return two(argv, "vol", buf, '-', VOL_STEP);
	default: return 0;
	}
}
