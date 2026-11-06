/* SPDX-License-Identifier: LGPL-2.1-or-later */
/** @file theme.h
 * @brief Colours by role.
 *
 * "powerlines", from the reference picture: black sky, electric cyan and
 * magenta, violet between them, a sunset orange and an acid green.
 * "plain" leaves every colour to the terminal. A theme file holds
 *
 *     # a comment, on a line of its own
 *     magenta = #ff2bd6
 *     bg = default
 *
 * one role a line; roles it does not name keep powerlines' colours. */

#ifndef TUI_THEME_H
#define TUI_THEME_H

#include <stddef.h>
#include <stdint.h>

enum theme_role {
	TH_BG,      /* everything */
	TH_FG,      /* body text, the title */
	TH_CYAN,    /* time played, things that move */
	TH_MAGENTA, /* playing */
	TH_VIOLET,  /* the artist, in between */
	TH_ORANGE,  /* paused, errors */
	TH_GREEN,   /* good news */
	TH_DIM,     /* what matters less: duration, separators, stopped */
	TH_N
};

#define THEME_DEFAULT (-1) /* the terminal's own colour */

struct theme {
	int32_t c[TH_N]; /* 0xRRGGBB, or THEME_DEFAULT */
	int bold_title;
};

extern const struct theme theme_powerlines;
extern const struct theme theme_plain;

/** "powerlines", "plain", or a file path into *t.
 * @returns 0, or -1 with the reason in err. */
int theme_load(const char *name, struct theme *t, char *err, size_t errlen);

/** A theme file's text over *t (which it starts from).
 * @returns 0, or -1 with the first bad line in err. */
int theme_parse(const char *text, struct theme *t, char *err, size_t errlen);

#endif
