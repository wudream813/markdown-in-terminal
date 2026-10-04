# mdt - Markdown reader for the terminal
# Plain make build (no cmake required).  `make -j` and run ./mdt file.md
#
# Requires: a C11 compiler, a C++17 compiler, POSIX. Everything else is
# vendored (stb, QuickJS) or embedded at build time.

CXX      ?= g++
CC       ?= gcc
BUILD    ?= build
BIN      := $(BUILD)/mdt
VERSION  ?= 0.1.0

OPT      ?= -O2
CXXFLAGS ?= -std=c++17 $(OPT) -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare \
            -Isrc -Ivendor -DMDT_VERSION="\"$(VERSION)\""
CFLAGS   ?= -std=c11 $(OPT) -w -funsigned-char -D_GNU_SOURCE -Ivendor/quickjs \
            -DCONFIG_VERSION="\"2025-09-13\""
LDLIBS   ?= -lm -lpthread -ldl

CPP_SRC := src/main.cpp src/app.cpp src/md.cpp src/render.cpp src/render_draw.cpp \
           src/term.cpp src/image.cpp src/svg.cpp src/math.cpp src/font.cpp src/util.cpp
C_SRC   := vendor/quickjs/quickjs.c vendor/quickjs/libregexp.c vendor/quickjs/libunicode.c \
           vendor/quickjs/cutils.c vendor/quickjs/xsum.c vendor/quickjs/quickjs-libc.c
GEN_SRC := src/generated/assets_js.cpp

CPP_OBJ := $(patsubst %.cpp,$(BUILD)/%.o,$(CPP_SRC))
C_OBJ   := $(patsubst %.c,$(BUILD)/%.o,$(C_SRC))
GEN_OBJ := $(patsubst %.cpp,$(BUILD)/%.o,$(GEN_SRC))
OBJ     := $(CPP_OBJ) $(C_OBJ) $(GEN_OBJ)

# optional: link against libcurl for remote images if pkg-config knows it
CURL_CFLAGS := $(shell pkg-config --cflags libcurl 2>/dev/null)
CURL_LIBS   := $(shell pkg-config --libs libcurl 2>/dev/null)
ifneq ($(CURL_LIBS),)
  CXXFLAGS += -DMDT_HAVE_CURL=1 $(CURL_CFLAGS)
  LDLIBS   += $(CURL_LIBS)
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
	install -D -m 0755 $(BIN) $(DESTDIR)$(PREFIX)/bin/mdt

clean:
	rm -rf $(BUILD)
