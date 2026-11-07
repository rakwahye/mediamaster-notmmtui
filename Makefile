# SPDX-License-Identifier: LGPL-2.1-or-later
#
# notmmtui: a strip of a player for the mediamaster daemon.
#
#   make                 build/notmmtui
#   make test            unit tests; install and uninstall into a scratch root;
#                        a smoke run against the installed daemon, if there is one
#   make SAN=asan test   same under AddressSanitizer   -> build/asan/
#   make SAN=tsan test   same under ThreadSanitizer    -> build/tsan/
#   make check-licenses  every source file carries an SPDX line
#   make install         $(DESTDIR)$(BINDIR)/notmmtui; BINDIR = $(PREFIX)/bin,
#                        PREFIX = /usr/local unless given
#   make uninstall       removes it again
#   make clean           removes build/
#
# Builds against the installed mediamaster SDK (pkg-config mediamaster:
# mm_client.h, where the daemon listens and what its version reply says),
# so install mediamaster first. notmmtui compiles in the SDK's ABI number
# and applies the SDK's rule to the daemon it finds (MM_ABI_FITS in
# mm_plugin.h): a daemon with a newer minor is fine, another major is
# refused until notmmtui is rebuilt, which mm-update does.
#
# Needs notcurses-core >= 3.0 with headers; python3 for make test.

CC      ?= cc
PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/bin
VERSION := 0.1.0

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

PKG_CONFIG ?= pkg-config
ifeq ($(shell command -v $(PKG_CONFIG)),)
  $(error $(PKG_CONFIG) not found: install pkgconf (or pkg-config))
endif

# pkg-config on some distributions (Arch) does not search /usr/local, where
# mediamaster installs by default. The daemon in PATH says where its SDK is.
MM_BIN    := $(shell command -v mediamaster 2>/dev/null)
MM_PC_DIR := $(if $(MM_BIN),$(abspath $(dir $(MM_BIN))../lib/pkgconfig))
MM_PC     := PKG_CONFIG_PATH="$(PKG_CONFIG_PATH)$(if $(MM_PC_DIR),:$(MM_PC_DIR))" $(PKG_CONFIG)
MM_CFLAGS := $(shell $(MM_PC) --cflags mediamaster 2>/dev/null)
ifeq ($(MM_CFLAGS),)
  $(error mediamaster is not installed, or not in PATH: pkg-config cannot find mediamaster.pc. \
          Run make install in the mediamaster repository first, or set PKG_CONFIG_PATH to its lib/pkgconfig)
endif
MM_ABI     := $(shell $(MM_PC) --variable=abi mediamaster)
MM_INCDIR  := $(patsubst -I%,%,$(filter -I%,$(MM_CFLAGS)))
MM_HEADERS := $(MM_INCDIR)/mm_client.h $(MM_INCDIR)/mm_plugin.h

# Notcurses: the core library only (no ffmpeg/OIIO image backend). A
# source install goes under /usr/local, which not every pkg-config
# searches (Arch's does not), so look there too when it is not found.
NC_PC     := notcurses-core
NC_PCPATH := $(PKG_CONFIG_PATH)
ifneq ($(shell $(PKG_CONFIG) --exists $(NC_PC) 2>/dev/null && echo 1),1)
  NC_PCPATH := $(PKG_CONFIG_PATH)$(if $(PKG_CONFIG_PATH),:)/usr/local/lib/pkgconfig:/usr/local/lib64/pkgconfig
endif
PC := PKG_CONFIG_PATH='$(NC_PCPATH)' $(PKG_CONFIG)
ifneq ($(shell $(PC) --atleast-version=3.0 $(NC_PC) 2>/dev/null && echo 1),1)
  $(error $(NC_PC) >= 3.0 not found by $(PKG_CONFIG) (searched its own path and /usr/local; \
          found: $(or $(shell $(PC) --modversion $(NC_PC) 2>/dev/null),nothing)). \
          Install notcurses with its headers, or set PKG_CONFIG_PATH to the directory of $(NC_PC).pc)
endif

# A client, like mmctl: C11, plain C99 style. The ABI it is built for is
# MM_ABI from the SDK header; every object depends on the SDK's headers,
# so a reinstalled SDK rebuilds it.
TUI_CFLAGS := -std=c11 $(WARN) $(OPT) $(SAN_FLAGS) -D_GNU_SOURCE -Isrc $(MM_CFLAGS) \
              -DTUI_VERSION=\"$(VERSION)\" $(shell $(PC) --cflags $(NC_PC))
TUI_LIBS   := $(SAN_FLAGS) $(shell $(PC) --libs $(NC_PC)) -lm

SRC  := $(wildcard src/*.c)
OBJ  := $(patsubst src/%.c,$(BUILD)/obj/%.o,$(SRC))
HDRS := $(wildcard src/*.h) $(MM_HEADERS)

.PHONY: all test unit smoke install-test check-licenses install uninstall clean
all: $(BUILD)/notmmtui

$(BUILD)/notmmtui: $(OBJ)
	$(CC) $^ $(TUI_LIBS) -o $@

$(BUILD)/obj/%.o: src/%.c $(HDRS) Makefile
	@mkdir -p $(@D)
	$(CC) $(TUI_CFLAGS) -c $< -o $@

# ------------------------------------------------------------------ tests

TEST_CFLAGS := $(TUI_CFLAGS) -Itests
T := $(BUILD)/tests

$(T)/test-reply: tests/reply_test.c src/reply.c src/reply.h tests/check.h Makefile
	@mkdir -p $(@D)
	$(CC) $(TEST_CFLAGS) $(filter %.c,$^) $(TUI_LIBS) -lm -o $@

$(T)/test-keys: tests/keys_test.c src/keys.c src/keys.h tests/check.h Makefile
	@mkdir -p $(@D)
	$(CC) $(TEST_CFLAGS) $(filter %.c,$^) $(TUI_LIBS) -o $@

$(T)/test-theme: tests/theme_test.c src/theme.c src/theme.h tests/check.h Makefile
	@mkdir -p $(@D)
	$(CC) $(TEST_CFLAGS) $(filter %.c,$^) $(TUI_LIBS) -o $@

$(T)/test-strip: tests/strip_test.c src/strip.c src/font.c src/reply.c src/theme.c src/ipc.c $(HDRS) tests/check.h Makefile
	@mkdir -p $(@D)
	$(CC) $(TEST_CFLAGS) $(filter %.c %.o,$^) $(TUI_LIBS) -o $@

$(T)/test-react: tests/react_test.c src/react.c src/react.h src/reply.h tests/check.h Makefile
	@mkdir -p $(@D)
	$(CC) $(TEST_CFLAGS) $(filter %.c,$^) $(TUI_LIBS) -o $@

$(T)/test-overlay: tests/overlay_test.c src/overlay.c src/strip.c src/font.c src/reply.c src/theme.c src/ipc.c $(HDRS) tests/check.h Makefile
	@mkdir -p $(@D)
	$(CC) $(TEST_CFLAGS) $(filter %.c %.o,$^) $(TUI_LIBS) -o $@

TESTS := $(T)/test-reply $(T)/test-keys $(T)/test-theme $(T)/test-strip $(T)/test-react $(T)/test-overlay

unit: $(TESTS)
	@$(T)/test-reply tests/fixtures
	@$(T)/test-keys
	@$(T)/test-theme
	@$(T)/test-strip
	@$(T)/test-react
	@$(T)/test-overlay

# Against the installed daemon, the one this is built for; skipped when
# none is in PATH. Only this client is built with SAN. Notcurses gives
# its input thread an alternate signal stack of its own, which ASan would
# unmap as its own when the thread ends; ASan keeps out of signal stacks
# here.
smoke: all
	@if [ -n "$(MM_BIN)" ]; then ASAN_OPTIONS=use_sigaltstack=0 sh tests/smoke.sh $(BUILD) $(MM_ABI); \
	else echo "smoke: skipped (no mediamaster in PATH)"; fi

# install and uninstall into a scratch root, as mm-update would run them
install-test: all
	@sh tests/install.sh $(MAKE) $(SAN)

test: unit
	@$(MAKE) --no-print-directory install-test
	@$(MAKE) --no-print-directory smoke

check-licenses:
	@missing=$$(grep -L 'SPDX-License-Identifier' $$(git ls-files -co --exclude-standard \
		'*.c' '*.h' '*.sh' '*.py' Makefile) 2>/dev/null); \
	if [ -n "$$missing" ]; then echo "missing SPDX line:"; echo "$$missing"; exit 1; fi; \
	echo "check-licenses: ok"

install: all
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(BUILD)/notmmtui $(DESTDIR)$(BINDIR)/notmmtui

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/notmmtui

clean:
	rm -rf build
