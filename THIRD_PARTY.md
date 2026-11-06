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
