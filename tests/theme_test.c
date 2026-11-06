/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Built-in themes and theme files. */
#include "theme.h"
#include "check.h"
#include <stdio.h>
#include <string.h>

static int builtins(void)
{
	struct theme t;
	char err[256];
	CHECK(theme_load("powerlines", &t, err, sizeof(err)) == 0 && t.c[TH_MAGENTA] == 0xff2bd6 &&
	      t.c[TH_BG] == 0x05060e && t.c[TH_CYAN] == 0x19d3ff, "powerlines");
	CHECK(theme_load("plain", &t, err, sizeof(err)) == 0, "plain");
	for (int r = 0; r < TH_N; r++) CHECK(t.c[r] == THEME_DEFAULT, "plain is the terminal's colours");
	CHECK(theme_load("/nonexistent/theme", &t, err, sizeof(err)) != 0 && strstr(err, "powerlines, plain"),
	      "an unknown name says what there is");
	return 0;
}

static int files(void)
{
	struct theme t = theme_powerlines;
	char err[256];
	CHECK(theme_parse("# mine\n\n  magenta = #00ff00 \nbg = default\n", &t, err, sizeof(err)) == 0,
	      "comments, blank lines, spaces");
	CHECK(t.c[TH_MAGENTA] == 0x00ff00 && t.c[TH_BG] == THEME_DEFAULT && t.c[TH_CYAN] == 0x19d3ff,
	      "named roles change, the rest stay");
	t = theme_powerlines;
	CHECK(theme_parse("cyan = #123\n", &t, err, sizeof(err)) != 0 && strstr(err, "line 1"), "short hex");
	CHECK(theme_parse("ok = #123456\n", &t, err, sizeof(err)) != 0, "unknown role");
	CHECK(theme_parse("fg #123456\n", &t, err, sizeof(err)) != 0, "no =");
	CHECK(theme_parse("\nfg = #12345g\n", &t, err, sizeof(err)) != 0 && strstr(err, "line 2"), "not hex, line counted");

	const char *path = "/tmp/notmmtui-theme-test";
	FILE *f = fopen(path, "w");
	CHECK(f != NULL, "temp file");
	fputs("violet = #abcdef\n", f);
	fclose(f);
	CHECK(theme_load(path, &t, err, sizeof(err)) == 0 && t.c[TH_VIOLET] == 0xabcdef && t.c[TH_ORANGE] == 0xff7a1a,
	      "a file over powerlines");
	remove(path);
	return 0;
}

int main(void)
{
	RUN(builtins);
	RUN(files);
	CHECK_PASS("test-theme");
}
