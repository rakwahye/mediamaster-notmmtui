# SPDX-License-Identifier: LGPL-2.1-or-later
#
# notmmtui: a strip of a player for the mediamaster daemon.
#
#   make                 build/notmmtui
#   make test            unit tests, then a smoke run against the pinned daemon
#   make SAN=asan test   same under AddressSanitizer   -> build/asan/
#   make SAN=tsan test   same under ThreadSanitizer    -> build/tsan/
#   make check-licenses  every source file carries an SPDX line
#   make install PREFIX=/usr/local
#
# mediamaster/ is a submodule pinned to a commit and fetched by `make`
# when missing. From it come src/host/paths.c (where the socket is, as
# mmctl finds it) and tests/check.h; `make test` runs its daemon.
#
# Needs notcurses-core >= 3.0 with headers; python3 for make test.
#
#   make MM=~/src/mediamaster        build against another host checkout
#   git -C mediamaster checkout <rev> && git add mediamaster
#                                     move the pin

CC      ?= cc
MM      ?= mediamaster
PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/bin
VERSION := 0.1.0

fetch = $(if $(wildcard $(1)/$(2)),,$(shell git submodule update --init $(1) >&2))
ifeq ($(MM),mediamaster)
$(call fetch,$(MM),src/host/paths.c)
endif

SAN ?=
ifeq ($(SAN),)
  SAN_FLAGS :=
  OPT       := -O2 -g
  BUILD     := build
else ifeq ($(SAN),asan)
  SAN_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer
  OPT       := -O1 -g
  BUILD     := build/asan
else ifeq ($(SAN),tsan)
  SAN_FLAGS := -fsanitize=thread -fno-omit-frame-pointer
  OPT       := -O1 -g
  BUILD     := build/tsan
else
  $(error SAN must be empty, asan or tsan)
endif

WARN := -Wall -Wextra -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat=2

# Notcurses: the core library only (no ffmpeg/OIIO image backend).
NC_PC := notcurses-core
ifneq ($(shell pkg-config --atleast-version=3.0 $(NC_PC) && echo 1),1)
  $(error $(NC_PC) >= 3.0 not found: install notcurses with its headers)
endif

# A client, like mmctl: C11, plain C99 style.
TUI_CFLAGS := -std=c11 $(WARN) $(OPT) $(SAN_FLAGS) -D_GNU_SOURCE -Isrc -I$(MM)/src/host \
              -DTUI_VERSION=\"$(VERSION)\" $(shell pkg-config --cflags $(NC_PC))
TUI_LIBS   := $(SAN_FLAGS) $(shell pkg-config --libs $(NC_PC))

SRC  := $(wildcard src/*.c)
OBJ  := $(patsubst src/%.c,$(BUILD)/obj/%.o,$(SRC)) $(BUILD)/obj/mm/paths.o
HDRS := $(wildcard src/*.h)

.PHONY: all test unit smoke check-licenses install clean
all: $(BUILD)/notmmtui

$(BUILD)/notmmtui: $(OBJ)
	$(CC) $^ $(TUI_LIBS) -o $@

$(BUILD)/obj/%.o: src/%.c $(HDRS) Makefile
	@mkdir -p $(@D)
	$(CC) $(TUI_CFLAGS) -c $< -o $@

$(BUILD)/obj/mm/paths.o: $(MM)/src/host/paths.c $(MM)/src/host/paths.h Makefile
	@mkdir -p $(@D)
	$(CC) $(TUI_CFLAGS) -c $< -o $@

# ------------------------------------------------------------------ tests

TEST_CFLAGS := $(TUI_CFLAGS) -I$(MM)/tests
T := $(BUILD)/tests

$(T)/test-reply: tests/reply_test.c src/reply.c src/reply.h $(MM)/tests/check.h Makefile
	@mkdir -p $(@D)
	$(CC) $(TEST_CFLAGS) $(filter %.c,$^) $(TUI_LIBS) -lm -o $@

$(T)/test-keys: tests/keys_test.c src/keys.c src/keys.h $(MM)/tests/check.h Makefile
	@mkdir -p $(@D)
	$(CC) $(TEST_CFLAGS) $(filter %.c,$^) $(TUI_LIBS) -o $@

TESTS := $(T)/test-reply $(T)/test-keys

unit: $(TESTS)
	@$(T)/test-reply tests/fixtures
	@$(T)/test-keys

# The daemon is the pinned host's own plain build; only this client is
# built with SAN. Notcurses gives its input thread an alternate signal
# stack of its own, which ASan would unmap as its own when the thread
# ends; ASan keeps out of signal stacks here.
smoke: all
	@$(MAKE) --no-print-directory -C $(MM) build/mediamaster build/mmctl >/dev/null
	@ASAN_OPTIONS=use_sigaltstack=0 sh tests/smoke.sh $(MM)/build $(BUILD)

test: unit
	@$(MAKE) --no-print-directory smoke

check-licenses:
	@missing=$$(grep -L 'SPDX-License-Identifier' $$(git ls-files -co --exclude-standard \
		'*.c' '*.h' '*.sh' '*.py' Makefile) 2>/dev/null); \
	if [ -n "$$missing" ]; then echo "missing SPDX line:"; echo "$$missing"; exit 1; fi; \
	echo "check-licenses: ok"

install: all
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(BUILD)/notmmtui $(DESTDIR)$(BINDIR)

clean:
	rm -rf build
