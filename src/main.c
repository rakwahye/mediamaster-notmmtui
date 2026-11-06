/* SPDX-License-Identifier: LGPL-2.1-or-later */
/** @file main.c
 * @brief notmmtui: a strip of a player for the mediamaster daemon.
 *
 *   notmmtui --version   this client's version and the daemon's */

#include "ipc.h"
#include <stdio.h>
#include <string.h>

static void usage(FILE *f)
{
	fputs("usage: notmmtui [--version]\n", f);
}

static int version(void)
{
	char *argv[] = { "version", NULL };
	char reply[256];
	printf("notmmtui %s\n", TUI_VERSION);
	int rc = tui_call(1, argv, reply, sizeof(reply));
	if (rc == TUI_NO_DAEMON) {
		puts("daemon: not running");
		return 1;
	}
	printf("daemon: %s\n", reply);
	return rc == TUI_OK ? 0 : 1;
}

int main(int argc, char **argv)
{
	if (argc == 2 && !strcmp(argv[1], "--version")) return version();
	if (argc == 2 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
		usage(stdout);
		return 0;
	}
	usage(stderr);
	return 2;
}
