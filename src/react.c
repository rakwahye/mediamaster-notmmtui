/* SPDX-License-Identifier: LGPL-2.1-or-later */
/** @file react.c
 * @brief Band envelopes and a beat, from levels lines. */

#include "react.h"
#include <math.h>
#include <string.h>

#define TAU_ATTACK  0.021 /* s: 0.55 a frame at 60 fps */
#define TAU_RELEASE 0.130 /* s: 0.12 a frame at 60 fps */
#define TAU_BEAT    0.084 /* s: x0.82 a frame at 60 fps */
#define AVG_KEEP    0.94  /* the bass average, per line (0.98 a frame at 60 fps) */
#define BEAT_RATIO  1.4   /* bass this far over its average is a beat... */
#define BEAT_FLOOR  0.15  /* ...if it is this loud at all */
#define BAND_FLOOR  0.35  /* band values (0 at -72 dBFS) under this are rest */
#define METER_DB    48.0

void react_reset(struct react *r) { memset(r, 0, sizeof(*r)); }

/* A band from the daemon (0..255, -72..0 dBFS) as 0..1 above the floor
 * where a quiet room sits. */
static float band01(uint8_t v)
{
	float f = (v / 255.0f - BAND_FLOOR) / (1.0f - BAND_FLOOR);
	return f < 0 ? 0 : f > 1 ? 1 : f;
}

static float mean(const uint8_t *b, int from, int to)
{
	float s = 0;
	for (int k = from; k < to; k++) s += band01(b[k]);
	return s / (float)(to - from);
}

void react_feed(struct react *r, const struct tui_levels *lv)
{
	for (int k = 0; k < TUI_BANDS; k++) r->t_band[k] = band01(lv->band[k]);
	r->t_bass = mean(lv->band, 0, TUI_BANDS / 3);
	r->t_mid = mean(lv->band, TUI_BANDS / 3, 2 * TUI_BANDS / 3);
	r->t_treble = mean(lv->band, 2 * TUI_BANDS / 3, TUI_BANDS);
	uint8_t pk = lv->peak[0] > lv->peak[1] ? lv->peak[0] : lv->peak[1];
	float db = pk ? 20.0f * log10f(pk / 255.0f) : -METER_DB;
	float lvl = (db + (float)METER_DB) / (float)METER_DB;
	r->t_level = lvl < 0 ? 0 : lvl > 1 ? 1 : lvl;

	/* a beat: bass well over where it has been lately, and rising now,
	 * so a steady tone flashes once rather than until the average
	 * catches up with it */
	if (r->t_bass > r->bass_avg * BEAT_RATIO && r->t_bass > BEAT_FLOOR &&
	    r->t_bass > r->bass_last)
		r->beat = 1.0f;
	r->bass_last = r->t_bass;
	r->bass_avg = r->bass_avg * AVG_KEEP + r->t_bass * (1.0f - AVG_KEEP);
}

static float follow(float env, float target, double dt)
{
	double tau = target > env ? TAU_ATTACK : TAU_RELEASE;
	return env + (target - env) * (float)(1.0 - exp(-dt / tau));
}

void react_step(struct react *r, double dt)
{
	if (dt <= 0) return;
	r->bass = follow(r->bass, r->t_bass, dt);
	r->mid = follow(r->mid, r->t_mid, dt);
	r->treble = follow(r->treble, r->t_treble, dt);
	r->level = follow(r->level, r->t_level, dt);
	for (int k = 0; k < TUI_BANDS; k++) r->band[k] = follow(r->band[k], r->t_band[k], dt);
	r->beat *= (float)exp(-dt / TAU_BEAT);
}
