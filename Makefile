# mdt - Markdown reader for the terminal
# Plain make build (no cmake required).  `make -j` and run ./mdt file.md
#
# Requires: a C11 compiler, a C++17 compiler, POSIX. Everything else is
# vendored (stb, QuickJS) or embedded at build time.

CXX      ?= g++
CC       ?= gcc
BUILD    ?= build
VERSION  ?= 0.1.0

OPT      ?= -O2
CXXFLAGS ?= -std=c++17 $(OPT) -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare \
            -Isrc -Ivendor -DMDT_VERSION="\"$(VERSION)\""
CXXFLAGS += -MMD -MP          # track header dependencies (see DEPS below)
CFLAGS   ?= -std=c11 $(OPT) -w -funsigned-char -D_GNU_SOURCE -Ivendor/quickjs \
            -DCONFIG_VERSION="\"2025-09-13\""
LDLIBS   ?= -lm -lpthread -ldl

# ------------------------------------------------------- cross compilation ---
# Windows (MinGW-w64):
#   make CXX=x86_64-w64-mingw32-g++ CC=x86_64-w64-mingw32-gcc
#   make CXX=i686-w64-mingw32-g++   CC=i686-w64-mingw32-gcc     (32 bit)
WINDOWS :=
ifneq ($(findstring mingw,$(CXX)),)
  WINDOWS  := 1
  # keep cross artefacts inside build/ so `make clean` and tooling find them
  BUILD    := $(if $(filter build,$(BUILD)),build/win,$(BUILD))
  WIN_DEFS := -DWIN32_LEAN_AND_MEAN -D_WIN32_WINNT=0x0601 -DNOMINMAX -DMDT_WINDOWS
  CXXFLAGS += $(WIN_DEFS)
  CFLAGS   += $(WIN_DEFS)
  # static libgcc/libstdc++/winpthread so the .exe has no MinGW DLL deps
  LDLIBS   := -lm -lws2_32 -lwininet -static -static-libgcc -static-libstdc++
endif

BIN      := $(BUILD)/mdt$(if $(WINDOWS),.exe,)

CPP_SRC := src/main.cpp src/app.cpp src/render.cpp src/render_draw.cpp src/md.cpp \
           src/term.cpp src/platform.cpp src/image.cpp src/svg.cpp src/math.cpp \
           src/font.cpp src/util.cpp
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
  CURL_CFLAGS := $(shell pkg-config --cflags libcurl 2>/dev/null)
  CURL_LIBS   := $(shell pkg-config --libs libcurl 2>/dev/null)
  ifneq ($(CURL_LIBS),)
    CXXFLAGS += -DMDT_HAVE_CURL=1 $(CURL_CFLAGS)
    LDLIBS   += $(CURL_LIBS)
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
test: $(BIN)
	$(CXX) $(CXXFLAGS) -w -o $(BUILD)/test_svg tools/test_svg.cpp src/svg.cpp src/image.cpp src/util.cpp $(OPT) -lm
	$(CXX) $(CXXFLAGS) -o $(BUILD)/test_math tools/test_math.cpp $(OBJ) $(OPT) $(LDLIBS)

install: $(BIN)
	install -D -m 0755 $(BIN) $(DESTDIR)$(PREFIX)/bin/$(notdir $(BIN))

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
