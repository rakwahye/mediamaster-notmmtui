/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Reply readers against replies captured from a real daemon
 * (tests/fixtures), and a few made by hand for the edges. */
#include "reply.h"
#include "check.h"
#include <mediamaster/mm_client.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *s_dir;

/* The fixture's text; static, valid until the next call. */
static const char *fx(const char *name)
{
	static char buf[16384];
	char path[512];
	snprintf(path, sizeof(path), "%s/%s", s_dir, name);
	FILE *f = fopen(path, "r");
	size_t n = f ? fread(buf, 1, sizeof(buf) - 1, f) : 0;
	if (f) fclose(f);
	buf[n] = '\0';
	return buf;
}

static int status(void)
{
	struct tui_status st;
	CHECK(tui_status_read(fx("status-playing-file.txt"), &st) == 0, "a status reply reads");
	CHECK(st.state == TUI_PLAYING && st.item == 1, "state and item");
	CHECK(!strcmp(st.uri, "/home/u/music/Osc/two.flac") && !strcmp(st.source, "ffmpeg"), "uri and source");
	CHECK(!strcmp(st.title, "Tab\tTitle"), "a tag value is taken as it is, tab and all");
	CHECK(!strcmp(st.artist, "Osc") && !strcmp(st.album, "LP: One"), "a colon inside a value stays");
	CHECK(fabs(st.position - 0.46) < 1e-9 && fabs(st.duration - 3.0) < 1e-9, "position and duration in s");
	CHECK(st.seekable && st.volume == 100 && !st.next[0], "seekable, volume; next '-' is empty");

	CHECK(tui_status_read(fx("status-paused.txt"), &st) == 0 && st.state == TUI_PAUSED, "paused");
	CHECK(tui_status_read(fx("status-live.txt"), &st) == 0 && st.duration == 0 && !st.seekable &&
	      !st.title[0] && !strcmp(st.uri, "sine:440"), "live: no duration, no tags");
	CHECK(tui_status_read(fx("status-stopped.txt"), &st) == 0 && st.state == TUI_STOPPED && !st.uri[0] &&
	      !st.item, "stopped");
	CHECK(tui_status_read("unknown verb: status\n", &st) == -1, "not a status reply");
	CHECK(tui_status_read("", &st) == -1, "nothing is not a status");
	return 0;
}

static int queue(void)
{
	struct tui_list l = { 0 };
	CHECK(tui_list_queue(&l, fx("queue.txt")) == 0 && l.n == 3, "queue list: three rows");
	CHECK(l.v[0].cursor && l.v[0].pos == 1 && !strcmp(l.v[0].uri, "/home/u/music/Osc/two.flac"), "cursor row");
	CHECK(!l.v[1].cursor && l.v[1].pos == 2 && !strcmp(l.v[1].uri, "sine:440:2") && !l.v[1].title[0],
	      "plain rows have no title");

	CHECK(tui_list_queue(&l, fx("queue-l.txt")) == 0 && l.n == 3, "queue list -l: three rows");
	CHECK(l.v[0].cursor && !strcmp(l.v[0].title, "Tab Title") && !strcmp(l.v[0].duration, "0:03"), "-l cursor row");
	CHECK(!strcmp(l.v[1].uri, "sine:440:2") && !l.v[1].title[0] && !l.v[1].duration[0], "-l unknown fields");
	CHECK(l.v[2].pos == 3 && !strcmp(l.v[2].artist, "Pink Floyd") &&
	      !strcmp(l.v[2].album, "The Dark Side of the Moon"), "-l labelled row");

	CHECK(tui_list_queue(&l, fx("queue-empty.txt")) == 0 && l.n == 0, "empty queue");
	CHECK(tui_list_queue(&l, "  1  a b c\n...\n") == 0 && l.n == 1 && l.more && !strcmp(l.v[0].uri, "a b c"),
	      "a URI with spaces; a cut list says so");
	CHECK(tui_list_queue(&l, "  1  ok\nnonsense\n  x  y\n") == 2 && l.n == 1, "unreadable lines counted, skipped");
	tui_list_free(&l);
	return 0;
}

static int items(void)
{
	struct tui_list l = { 0 };
	CHECK(tui_list_items(&l, fx("search.txt")) == 0 && l.n == 2, "search: two rows");
	CHECK(l.v[1].kind == TUI_TRACK && !strcmp(l.v[1].title, "Tab Title") && !strcmp(l.v[1].album, "LP: One") &&
	      !strcmp(l.v[1].duration, "0:03"), "a track row");
	CHECK(!l.v[0].album[0], "'-' is empty");
	CHECK(tui_list_items(&l, fx("browse-top.txt")) == 0 && l.n == 2 && l.v[0].kind == TUI_LIST &&
	      !strcmp(l.v[0].uri, "lib:artists") && !strcmp(l.v[0].title, "Artists"), "browse: list rows");
	CHECK(tui_list_items(&l, fx("search-none.txt")) == 0 && l.n == 0, "no results");
	CHECK(tui_list_items(&l, "empty\n") == 0 && l.n == 0, "an empty list");
	CHECK(tui_list_items(&l, "track\tx\tX\t-\t-\t-\n... more than 1 items, rest not shown\n") == 0 && l.n == 1 &&
	      l.more, "a cut browse says so");
	CHECK(tui_list_items(&l, "radio\tx\tX\t-\t-\t-\nlist\t\tNo URI\t-\t-\t-\nnope\n") == 3 && l.n == 0,
	      "unknown kinds, missing URIs, untabbed lines skipped");
	tui_list_free(&l);
	CHECK(!l.text && !l.v && !l.n, "free leaves an empty list");
	return 0;
}

static int watch(void)
{
	const char *w = fx("watch.txt");
	char line[256];
	unsigned want[] = { 0, TUI_EV_STATE | TUI_EV_TRACK | TUI_EV_QUEUE, TUI_EV_STATE };
	for (int i = 0; i < 3; i++) {
		size_t n = strcspn(w, "\n");
		snprintf(line, sizeof(line), "%.*s", (int)n, w);
		CHECK(tui_watch_line(line, NULL) == want[i], "watch lines from the daemon");
		w += n + 1;
	}
	CHECK(tui_watch_line("volume seek meta\n", NULL) == (TUI_EV_VOLUME | TUI_EV_SEEK | TUI_EV_META), "with a newline");
	CHECK(tui_watch_line("state sparkle", NULL) == TUI_EV_STATE, "unknown names are passed over");
	CHECK(tui_watch_line("err unknown event: x", NULL) == 0, "an err line is nothing");

	struct tui_levels lv;
	const char *L = fx("levels.txt");
	L += strcspn(L, "\n") + 1; /* past "ok watching levels" */
	CHECK(tui_watch_line(L, &lv) == TUI_EV_LEVELS, "a levels line from the daemon");
	CHECK(lv.peak[0] == 0x17 && lv.peak[1] == 0x17 && lv.band[0] == 0x34 && lv.band[11] == 0x5d && lv.band[12] == 0x65 &&
	      lv.band[31] == 0, "peaks and bands");
	CHECK(tui_watch_line("levels 00 00 0000000000000000000000000000000000000000000000000000000000000000", &lv) ==
	      TUI_EV_LEVELS && !lv.peak[0] && !lv.band[5], "silence");
	lv.peak[0] = 9;
	CHECK(tui_watch_line("levels 00 00 00", &lv) == 0 && lv.peak[0] == 9, "a short levels line is refused, lv untouched");
	CHECK(tui_watch_line("levels 0G 00 0000000000000000000000000000000000000000000000000000000000000000", &lv) == 0,
	      "not hex");
	CHECK(tui_watch_line("levels 00 00 0000000000000000000000000000000000000000000000000000000000000000ff", &lv) == 0,
	      "too many bands");
	return 0;
}

/* The version reply is read by the SDK's own reader; what notmmtui needs of
 * it, and the rule it then applies, as the daemon it was built for says. */
static int version(void)
{
	struct mm_version v;
	CHECK(mm_version_read("mediamaster 0.1.0 abi 3.0", &v) == 0 && !strcmp(v.name, "mediamaster") &&
	      !strcmp(v.version, "0.1.0") && v.abi == MM_ABI_MAKE(3, 0), "the daemon's version");
	CHECK(mm_version_read("mediamaster 0.2.0 abi 12.4\n", &v) == 0 && MM_ABI_MAJOR_OF(v.abi) == 12 &&
	      MM_ABI_MINOR_OF(v.abi) == 4, "a newline after");
	CHECK(mm_version_read("fake", &v) == -1, "not a version");
	CHECK(mm_version_read("mediamaster 0.1.0", &v) == -1, "no abi");
	CHECK(mm_version_read("mediamaster 0.1.0 api 3.0", &v) == -1, "not abi");
	CHECK(mm_version_read("mediamaster 0.1.0 abi 3.0 extra", &v) == -1, "something after");
	CHECK(mm_version_read("mediamaster 0.1.0 abi 3", &v) == -1, "a number without its point");
	CHECK(mm_version_read("mediamaster 0.1.0 abi 3.0.1", &v) == -1, "too many points");
	CHECK(MM_ABI_FITS(MM_ABI_MAKE(3, 0), MM_ABI_MAKE(3, 2)), "a daemon with a newer minor fits");
	CHECK(!MM_ABI_FITS(MM_ABI_MAKE(3, 2), MM_ABI_MAKE(3, 0)), "one with an older minor does not");
	CHECK(!MM_ABI_FITS(MM_ABI_MAKE(3, 0), MM_ABI_MAKE(4, 0)), "nor another major");
	return 0;
}

int main(int argc, char **argv)
{
	s_dir = argc > 1 ? argv[1] : "tests/fixtures";
	RUN(status);
	RUN(queue);
	RUN(items);
	RUN(watch);
	RUN(version);
	CHECK_PASS("test-reply");
}
