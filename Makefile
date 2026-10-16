# mdt - Markdown reader for the terminal
# Plain make build (no cmake required).  `make -j` and run ./mdt file.md
#
# Requires: a C11 compiler, a C++17 compiler, POSIX. Everything else is
# vendored (stb, QuickJS) or embedded at build time.

CXX      ?= g++
CC       ?= gcc
PREFIX   ?= /usr/local
BUILD    ?= build
VERSION  ?= $(shell cat VERSION 2>/dev/null || echo 0.0.0)

OPT      ?= -O2
CXXFLAGS ?= -std=c++17 $(OPT) -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare \
            -Isrc -Ivendor -DMDT_VERSION="\"$(VERSION)\""
CXXFLAGS += -MMD -MP          # track header dependencies (see DEPS below)
CFLAGS   ?= -std=c11 $(OPT) -w -funsigned-char -D_GNU_SOURCE -Ivendor/quickjs \
            -DCONFIG_VERSION="\"2025-09-13\""
LDLIBS   ?= -lm -lpthread -ldl

# ------------------------------------------------------------------ static ---
#   make STATIC=1        fully static binary (no glibc version to satisfy at
#                        all): this is what the Linux release artefact uses.
# Static glibc cannot load libcurl (it wants NSS/dlopen), so STATIC=1 skips the
# libcurl probe and remote pictures go through the `curl` binary, as they
# already do in the published build.  CURL=1 overrides that if you have a
# complete set of static libraries.
ifeq ($(STATIC),1)
  LDLIBS := $(filter-out -ldl,$(LDLIBS))
  LDLIBS += -static -static-libgcc -static-libstdc++
  NO_CURL := 1
endif
ifneq ($(CURL),)
  NO_CURL :=
endif

# What the compiler targets, not what it is called: inside MSYS2 the MinGW
# compiler is plain `g++`, so looking at $(CXX) alone would send a MSYSTEM=
# MINGW64 shell down the Linux path.
TARGET   := $(shell $(CXX) -dumpmachine 2>/dev/null)

# ------------------------------------------------------- cross compilation ---
# Windows (MinGW-w64):
#   make CXX=x86_64-w64-mingw32-g++ CC=x86_64-w64-mingw32-gcc
#   make CXX=i686-w64-mingw32-g++   CC=i686-w64-mingw32-gcc     (32 bit)
#   (MSYS2, MSYSTEM=MINGW64/UCRT64/CLANG64: just `make`)
WINDOWS :=
ifneq ($(findstring mingw,$(CXX)$(TARGET)),)
  WINDOWS  := 1
  # keep cross artefacts inside build/ so `make clean` and tooling find them
  BUILD    := $(if $(filter build,$(BUILD)),build/win,$(BUILD))
  WIN_DEFS := -DWIN32_LEAN_AND_MEAN -D_WIN32_WINNT=0x0601 -DNOMINMAX -DMDT_WINDOWS
  CXXFLAGS += $(WIN_DEFS)
  CFLAGS   += $(WIN_DEFS)
  # static libgcc/libstdc++/winpthread so the .exe has no MinGW DLL deps
  # -lpsapi: process_memory_counters, used by --diag's memory line
  LDLIBS   := -lm -lws2_32 -lwininet -lpsapi -static -static-libgcc -static-libstdc++
endif

# -------------------------------------------- MSYS2, MSYSTEM=MSYS (POSIX) ----
# In the MSYS shell gcc reports a POSIX-on-Windows target - one contributor's
# MSYS2 gcc 15 says "x86_64-pc-cygwin" where older ones said "x86_64-pc-msys",
# and Cygwin's own gcc says the same - so both names select this branch.  A
# win32 target (mingw) was already handled above and never reaches here.
#
# The build needs three things the Linux line does not:
#   * the MSYS headers hide POSIX declarations under -std=c++17 / -std=c11,
#     because those define __STRICT_ANSI__ (strdup, realpath, fileno... all
#     vanish - and so the build fails).  GNU mode puts them back;
#   * iconv is libconv there, not libiconv;
#   * -lpthread / -ldl are stubs that some installs do not ship as files.
# The libraries are probed instead of assumed, so a toolchain that spells one
# of them differently does not break the build.
ifneq ($(findstring msys,$(TARGET))$(findstring cygwin,$(TARGET)),)
  MSYS     := 1
  CXXFLAGS := $(patsubst -std=c++17,-std=gnu++17,$(CXXFLAGS))
  CFLAGS   := $(patsubst -std=c11,-std=gnu11,$(CFLAGS))
  MSYS_LDLIBS := $(shell for lib in pthread dl conv; do \
      printf 'int main(){return 0;}\n' | $(CXX) -x c++ - -l$$lib -o /dev/null \
        >/dev/null 2>&1 && printf '%s ' -l$$lib; done)
  LDLIBS   := -lm $(MSYS_LDLIBS)
endif

# macOS does not export iconv from libc; it lives in its own library.  (Windows
# and Linux need nothing here: MinGW has MultiByteToWideChar, glibc has iconv.)
ifneq ($(findstring Darwin,$(shell uname -s)),)
  LDLIBS += -liconv
endif

# MSYS (and Cygwin) gcc appends .exe by itself, so name it here as well: every
# later rule in this file, and `make install`, has to point at the real file.
BIN      := $(BUILD)/mdt$(if $(WINDOWS),.exe,$(if $(MSYS),.exe,))

CPP_SRC := src/main.cpp src/app.cpp src/render.cpp src/render_draw.cpp src/md.cpp \
           src/term.cpp src/platform.cpp src/image.cpp src/svg.cpp src/math.cpp \
           src/font.cpp src/util.cpp src/encoding.cpp src/i18n.cpp
C_SRC   := vendor/quickjs/quickjs.c vendor/quickjs/libregexp.c vendor/quickjs/libunicode.c \
           vendor/quickjs/cutils.c vendor/quickjs/xsum.c vendor/quickjs/quickjs-libc.c
GEN_SRC := src/generated/assets_js.cpp

CPP_OBJ := $(patsubst %.cpp,$(BUILD)/%.o,$(CPP_SRC))
DEPS    := $(CPP_OBJ:.o=.d)
C_OBJ   := $(patsubst %.c,$(BUILD)/%.o,$(C_SRC))
GEN_OBJ := $(patsubst %.cpp,$(BUILD)/%.o,$(GEN_SRC))
OBJ     := $(CPP_OBJ) $(C_OBJ) $(GEN_OBJ)

# optional: link against libcurl for remote images if pkg-config knows it
# (skipped for cross builds: a host libcurl cannot be linked into a PE binary)
ifndef WINDOWS
ifneq ($(NO_CURL),1)
  CURL_CFLAGS := $(shell pkg-config --cflags libcurl 2>/dev/null)
  CURL_LIBS   := $(shell pkg-config --libs libcurl 2>/dev/null)
  ifneq ($(CURL_LIBS),)
    CXXFLAGS += -DMDT_HAVE_CURL=1 $(CURL_CFLAGS)
    LDLIBS   += $(CURL_LIBS)
  endif
endif
endif

.PHONY: all clean assets test install

all: $(BIN)

$(BIN): $(OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(OPT) -o $@ $(OBJ) $(LDLIBS)

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Re-embed the JS bundle (only needed when assets/js changes)
assets:
	python3 tools/build_assets.py

$(GEN_SRC): assets/js/mathjax-tex-svg.js tools/build_assets.py
	python3 tools/build_assets.py

# developer tools (svg / maths raster checks)
# (the sources without main.cpp: the tool brings its own)
mem-report: $(BIN)
	$(CXX) $(CXXFLAGS) -o $(BUILD)/mem-report tools/mem_report.cpp \
	  $(filter-out $(BUILD)/src/main.o,$(OBJ)) $(OPT) $(LDLIBS)

test: $(BIN)
	$(CXX) $(CXXFLAGS) -w -o $(BUILD)/test_svg tools/test_svg.cpp src/svg.cpp src/image.cpp src/util.cpp $(OPT) -lm
	$(CXX) $(CXXFLAGS) -o $(BUILD)/test_math tools/test_math.cpp $(OBJ) $(OPT) $(LDLIBS)

install: $(BIN)
	install -D -m 0755 $(BIN) $(DESTDIR)$(PREFIX)/bin/$(notdir $(BIN))
	install -D -m 0644 docs/mdt.1 $(DESTDIR)$(PREFIX)/share/man/man1/mdt.1
	install -D -m 0644 completions/mdt.bash \
	  $(DESTDIR)$(PREFIX)/share/bash-completion/completions/mdt
	install -D -m 0644 completions/_mdt \
	  $(DESTDIR)$(PREFIX)/share/zsh/site-functions/_mdt
	install -D -m 0644 completions/mdt.fish \
	  $(DESTDIR)$(PREFIX)/share/fish/vendor_completions.d/mdt.fish

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(notdir $(BIN))
	rm -f $(DESTDIR)$(PREFIX)/share/man/man1/mdt.1
	rm -f $(DESTDIR)$(PREFIX)/share/bash-completion/completions/mdt
	rm -f $(DESTDIR)$(PREFIX)/share/zsh/site-functions/_mdt
	rm -f $(DESTDIR)$(PREFIX)/share/fish/vendor_completions.d/mdt.fish

clean:
	rm -rf build dist

# convenience: build both the host binary and the Windows cross build
all-platforms: windows32
	$(MAKE) BUILD=build
	$(MAKE) BUILD=build/win CXX=x86_64-w64-mingw32-g++ CC=x86_64-w64-mingw32-gcc

windows32:
	$(MAKE) BUILD=build/win32 CXX=i686-w64-mingw32-g++ CC=i686-w64-mingw32-gcc

.PHONY: all-platforms windows windows32
windows:
	$(MAKE) BUILD=build/win CXX=x86_64-w64-mingw32-g++ CC=x86_64-w64-mingw32-gcc

# header dependencies written by -MMD: without these a change to a .h file only
# rebuilds the .cpp that directly includes it, leaving other objects compiled
# against the old struct layout (that mismatch shows up as a heap corruption).
-include $(DEPS)
