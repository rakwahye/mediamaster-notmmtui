/* SPDX-License-Identifier: LGPL-2.1-or-later */
/** @file theme.c
 * @brief The built-in themes and theme files. */

#include "theme.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const struct theme theme_powerlines = {
	.c = {
		[TH_BG] = 0x05060e, [TH_FG] = 0xe6f0ff, [TH_CYAN] = 0x19d3ff, [TH_MAGENTA] = 0xff2bd6,
		[TH_VIOLET] = 0x8a3cff, [TH_ORANGE] = 0xff7a1a, [TH_GREEN] = 0xc6ff3a, [TH_DIM] = 0x3a3f5c,
	},
	.bold_title = 1,
};

const struct theme theme_plain = {
	.c = { THEME_DEFAULT, THEME_DEFAULT, THEME_DEFAULT, THEME_DEFAULT,
	       THEME_DEFAULT, THEME_DEFAULT, THEME_DEFAULT, THEME_DEFAULT },
	.bold_title = 1,
};

static const char *const ROLE[TH_N] = {
	[TH_BG] = "bg", [TH_FG] = "fg", [TH_CYAN] = "cyan", [TH_MAGENTA] = "magenta",
	[TH_VIOLET] = "violet", [TH_ORANGE] = "orange", [TH_GREEN] = "green", [TH_DIM] = "dim",
};

static char *trim(char *s)
{
	while (isspace((unsigned char)*s)) s++;
	size_t n = strlen(s);
	while (n && isspace((unsigned char)s[n - 1])) s[--n] = '\0';
	return s;
}

int theme_parse(const char *text, struct theme *t, char *err, size_t errlen)
{
	char line[256];
	int no = 0;
	for (const char *l = text; *l; ) {
		const char *nl = strchr(l, '\n');
		size_t len = nl ? (size_t)(nl - l) : strlen(l);
		no++;
		snprintf(line, sizeof(line), "%.*s", (int)(len < sizeof(line) ? len : sizeof(line) - 1), l);
		l += len + (nl ? 1 : 0);
		char *s = trim(line);
		if (!*s || *s == '#') continue; /* blank, or a comment line */
		char *eq = strchr(s, '=');
		if (!eq) goto bad;
		*eq = '\0';
		char *key = trim(s), *val = trim(eq + 1);
		int role = -1;
		for (int r = 0; r < TH_N; r++)
			if (!strcmp(key, ROLE[r])) role = r;
		if (role < 0) goto bad;
		if (!strcmp(val, "default")) {
			t->c[role] = THEME_DEFAULT;
			continue;
		}
		char *end;
		if (val[0] != '#' || strlen(val) != 7) goto bad;
		long v = strtol(val + 1, &end, 16);
		if (*end) goto bad;
		t->c[role] = (int32_t)v;
	}
	return 0;
bad:
	snprintf(err, errlen, "line %d: want <role> = #rrggbb or default, roles: "
	         "bg fg cyan magenta violet orange green dim", no);
	return -1;
}

int theme_load(const char *name, struct theme *t, char *err, size_t errlen)
{
	if (!strcmp(name, "powerlines")) { *t = theme_powerlines; return 0; }
	if (!strcmp(name, "plain")) { *t = theme_plain; return 0; }
	FILE *f = fopen(name, "r");
	if (!f) {
		snprintf(err, errlen, "%s: no such theme or file (built in: powerlines, plain)", name);
		return -1;
	}
	char text[8192];
	size_t n = fread(text, 1, sizeof(text) - 1, f);
	fclose(f);
	text[n] = '\0';
	*t = theme_powerlines;
	char why[160];
	if (theme_parse(text, t, why, sizeof(why)) != 0) {
		snprintf(err, errlen, "%s: %s", name, why);
		return -1;
	}
	return 0;
}
