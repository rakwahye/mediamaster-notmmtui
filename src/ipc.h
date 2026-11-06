/* SPDX-License-Identifier: LGPL-2.1-or-later */
/** @file ipc.h
 * @brief Talking to the daemon, as mmctl does: one command a connection.
 *
 * Frame: "<verb> argc=N\n" then N NUL-terminated words (arg0 is the
 * verb). The daemon answers "ok ...", "err ..." or raw lines, then
 * closes. Socket: $MM_SOCK, else the daemon's default (mm_paths_socket). */

#ifndef TUI_IPC_H
#define TUI_IPC_H

#include <stddef.h>

enum tui_reply {
	TUI_NO_DAEMON = -1, /* nothing answers on the socket */
	TUI_OK        = 0,  /* "ok ..." (prefix stripped) or raw lines */
	TUI_ERR       = 1   /* "err ..." (prefix stripped), or a broken reply */
};

/** Send argv[0..argc-1], read the whole reply into `reply` (cut to fit,
 * one trailing newline trimmed from ok/err replies).
 * @returns enum tui_reply. */
int tui_call(int argc, char *const *argv, char *reply, size_t cap);

#endif
