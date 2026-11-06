/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Envelopes and the beat, from made-up levels lines. */
#include "react.h"
#include "check.h"
#include <string.h>

/* A line: peaks pk, the lowest third of the bands at lo, the rest at hi. */
static struct tui_levels line(uint8_t pk, uint8_t lo, uint8_t hi)
{
	struct tui_levels lv;
	lv.peak[0] = lv.peak[1] = pk;
	for (int k = 0; k < TUI_BANDS; k++) lv.band[k] = k < TUI_BANDS / 3 ? lo : hi;
	return lv;
}

/* 20 lines a second for s seconds, stepping 15 frames a second between. */
static void play(struct react *r, struct tui_levels lv, double s)
{
	for (double t = 0; t < s; t += 0.05) {
		react_feed(r, &lv);
		react_step(r, 0.05);
	}
}

static int follow(void)
{
	struct react r;
	react_reset(&r);
	struct tui_levels loud = line(0xff, 0xff, 0x40), quiet = line(0, 0, 0);
	react_feed(&r, &loud);
	react_step(&r, 0.05);
	CHECK(r.bass > 0.9f && r.level > 0.9f, "a loud line: up within a line");
	CHECK(r.treble == 0.0f, "quiet treble stays at rest (under the floor)");
	react_feed(&r, &quiet);
	react_step(&r, 0.05);
	CHECK(r.bass > 0.5f, "down more slowly than up");
	for (int i = 0; i < 20; i++) react_step(&r, 0.05); /* no more lines: silence was said once */
	CHECK(r.bass < 0.01f && r.level < 0.01f, "and to rest within a second, with no more lines");

	struct react a, b;
	react_reset(&a);
	react_reset(&b);
	react_feed(&a, &loud);
	react_feed(&b, &loud);
	react_step(&a, 0.1);
	react_step(&b, 0.05);
	react_step(&b, 0.05);
	CHECK(a.bass > b.bass - 1e-4f && a.bass < b.bass + 1e-4f, "one step of 0.1 s is two of 0.05: time-based");
	return 0;
}

static int beat(void)
{
	struct react r;
	react_reset(&r);
	play(&r, line(0x60, 0x80, 0x80), 0.4); /* steady, from silence */
	CHECK(r.beat < 0.05f, "a steady bass starting flashes once at most");
	play(&r, line(0x60, 0x80, 0x80), 1.6);
	CHECK(r.beat < 0.05f, "a steady bass is no beat");
	struct tui_levels kick = line(0xff, 0xff, 0x80);
	react_feed(&r, &kick);
	CHECK(r.beat == 1.0f, "a jump well over the average is");
	react_step(&r, 0.3);
	CHECK(r.beat < 0.05f, "and it decays within a third of a second");
	react_reset(&r);
	play(&r, line(0x20, 0x50, 0x50), 2.0);
	struct tui_levels soft = line(0x30, 0x60, 0x50);
	react_feed(&r, &soft);
	CHECK(r.beat == 0.0f, "a jump that is still quiet is not");
	return 0;
}

int main(void)
{
	RUN(follow);
	RUN(beat);
	CHECK_PASS("test-react");
}
