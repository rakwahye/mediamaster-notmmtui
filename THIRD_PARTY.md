<!-- SPDX-License-Identifier: LGPL-2.1-or-later -->
# Third-party code and services

Every external component notmmtui links, vendors, spawns or calls, with
its license. Updated in the same commit that adds a dependency.
`make check-licenses` enforces an SPDX line on our own files.

Project license: LGPL-2.1-or-later (see LICENSE).

## Submodules

| Component | Pinned | License | Used for |
|---|---|---|---|
| [mediamaster](https://github.com/rakwahye/mediamaster) | `mediamaster/` | LGPL-2.1-or-later | `src/host/paths.c` compiled in (where the socket is); `tests/check.h`; the daemon for `make test` |

## Linked

| Component | License | Since | Notes |
|---|---|---|---|
| glibc | LGPL-2.1-or-later | initial | |
| [Notcurses](https://github.com/dankamongmen/notcurses) (`notcurses-core`) | Apache-2.0 (licence read from its COPYRIGHT, v3.0.17) | first screen | Dynamically linked, never copied. Apache-2.0 is compatible with LGPL-3.0, so the combination is distributable under the "or later" option of our licence; not under LGPL-2.1 alone |
