/* SPDX-License-Identifier: LGPL-2.1-or-later */
/** @file keys.h
 * @brief Keys to daemon commands.
 *
 *     space  toggle          n p    next, prev
 *     ← →    seek ∓5 s       ⇧← ⇧→  seek ∓30 s
 *     + -    volume ±5       s      stop
 *
 * ("=" counts as "+", for keyboards where + needs shift.) */

#ifndef TUI_KEYS_H
#define TUI_KEYS_H

#include <stdint.h>

struct ncinput;

#define KEYS_ARGV_MAX 3

/** The command for key `id` (as notcurses_get returned it, with `ni`).
 * Fills argv (NULL-terminated) with words that live in `buf` (32 bytes)
 * or are constants. @returns argc, or 0 if the key is not a command. */
int keys_command(uint32_t id, const struct ncinput *ni, char **argv, char *buf);

#endif
