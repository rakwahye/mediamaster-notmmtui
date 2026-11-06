/* SPDX-License-Identifier: LGPL-2.1-or-later */
/** @file react.h
 * @brief What the looks move with: envelopes of the daemon's levels.
 *
 * The daemon sends `watch levels` lines 20 times a second while there is
 * sound, and one silent line when it stops. From each line come targets:
 * bass, mid and treble (the lower, middle and upper thirds of its 32
 * bands), level (the louder channel's peak on the meter's 48 dB scale)
 * and a beat (bass jumping well above its own running average). Between
 * lines, and after the last one, the envelopes move toward those targets
 * in time, fast up and slow down, so what is drawn at any frame rate
 * glides and falls back to rest on its own.
 *
 * The followers and the beat test are after the band envelopes of
 * rakwahye's visualizer_isf.cpp (LGPL-2.1-or-later), made time-based:
 * its per-frame attack 0.55, release 0.12 and beat decay 0.82 at 60
 * frames a second are the time constants here. */

#ifndef TUI_REACT_H
#define TUI_REACT_H

#include "reply.h"

struct react {
	/* what is drawn: 0..1, moving toward the targets */
	float bass, mid, treble, level, beat;
	/* the last line's */
	float t_bass, t_mid, t_treble, t_level;
	float bass_avg, bass_last; /* bass's slow running average, for the beat */
};

/** All at rest. */
void react_reset(struct react *r);

/** A levels line: new targets, and the beat test. */
void react_feed(struct react *r, const struct tui_levels *lv);

/** dt s later: the envelopes move toward their targets, the beat decays. */
void react_step(struct react *r, double dt);

#endif
