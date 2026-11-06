/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* The strip's arithmetic: the playhead, the sky, the ink. */
#include "strip.h"
#include "check.h"
#include <string.h>

static int bar(void)
{
	CHECK(strip_bar(100, 0, 100) == 0, "start");
	CHECK(strip_bar(100, 50, 100) == 400, "half way: 50 cells, in eighths");
	CHECK(strip_bar(60, 15, 30) == 240, "half of 60");
	CHECK(strip_bar(10, 1, 80) == 1, "an eighth of a cell");
	CHECK(strip_bar(100, 200, 100) == 800 && strip_bar(100, -5, 100) == 0, "clamped");
	CHECK(strip_bar(100, 10, 0) == -1, "live: none");
	CHECK(strip_bar(0, 10, 100) == -1, "no columns: none");
	return 0;
}

static int sky(void)
{
	const struct theme *t = &theme_powerlines;
	CHECK(strip_sky(t, 0) == (uint32_t)t->c[TH_ORANGE], "orange at the left");
	CHECK(strip_sky(t, 0.18) == (uint32_t)t->c[TH_MAGENTA], "then magenta");
	CHECK(strip_sky(t, 0.55) == (uint32_t)t->c[TH_VIOLET], "then violet");
	CHECK(strip_sky(t, 1) == (uint32_t)t->c[TH_CYAN], "cyan at the right");
	return 0;
}

static int ink(void)
{
	const struct theme *t = &theme_powerlines;
	CHECK(strip_ink(t, 0x101020) == (uint32_t)t->c[TH_FG], "light ink on dark");
	CHECK(strip_ink(t, 0xf0f0ff) == (uint32_t)t->c[TH_BG], "dark ink on bright");
	CHECK(strip_ink(t, 0x9e1b84) == (uint32_t)t->c[TH_FG], "light ink on lit magenta");
	return 0;
}

int main(void)
{
	RUN(bar);
	RUN(sky);
	RUN(ink);
	CHECK_PASS("test-strip");
}
