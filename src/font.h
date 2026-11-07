/* SPDX-License-Identifier: LGPL-2.1-or-later */
/** @file font.h
 * @brief A 5x7 font for drawing words big, in dots.
 *
 * Capitals, digits, the space and a little punctuation; a small letter is
 * drawn as its capital. Each glyph is 7 rows, top to bottom, of 5 bits,
 * bit 4 the left column. */
#ifndef TUI_FONT_H
#define TUI_FONT_H

/** Columns and rows of a glyph, and the dots between letters. */
#define FONT_W   5
#define FONT_H   7
#define FONT_GAP 1

/** The rows of `c`'s glyph, or NULL if the font has none. */
const unsigned char *font5x7(unsigned c);

#endif
