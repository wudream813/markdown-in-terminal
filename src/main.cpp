// main.cpp : argument parsing + entry point.
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

#include "app.h"
#include "platform.h"
#include "util.h"

#ifndef MDT_VERSION
#define MDT_VERSION "0.1.0"
#endif

using namespace mdt;

static bool arg_value(const std::string& arg, const char* name, std::string* out) {
  size_t n = strlen(name);
  if (arg.rfind(name, 0) == 0) {
    if (arg.size() > n && arg[n] == '=') { *out = arg.substr(n + 1); return true; }
  }
  return false;
}

// ---------------------------------------------------------------- checking ---
// Options used to be taken on trust: `--theme=bogus` quietly rendered with the
// default theme, and `--width=abc` became 0 (== "auto").  A typo in a script
// should say so, not silently change what is drawn.
static int usage_error(const std::string& msg) {
  fprintf(stderr, "mdt: %s\n\n", msg.c_str());
  print_usage();
  return 2;
}

static bool one_of(const std::string& v, std::initializer_list<const char*> allowed) {
  for (const char* a : allowed)
    if (v == a) return true;
  return false;
}

// Strict integer parse: whole string, no spaces, no trailing junk, in range.
static bool parse_int(const std::string& s, long lo, long hi, int* out) {
  if (s.empty()) return false;
  char* end = nullptr;
  errno = 0;
  long v = strtol(s.c_str(), &end, 10);
  if (errno != 0 || !end || *end != '\0') return false;
  if (v < lo || v > hi) return false;
  *out = (int)v;
  return true;
}

int main(int argc, char** argv) {
#ifndef _WIN32
  // `mdt --dump big.md | head` used to die from SIGPIPE (exit 141, which makes
  // a `set -o pipefail` script fail).  Ignoring it turns the closed pipe into
  // an ordinary write error at the very end of the dump: mdt exits 0 quietly,
  // the way a filter that was read from should.
  signal(SIGPIPE, SIG_IGN);
#endif
  plat::console_init();  // UTF-8 console, so Chinese file names print correctly
  // Windows hands us argv in the ANSI code page; take it from the wide command
  // line instead so a 中文 path stays intact.
  std::vector<std::string> av = plat::utf8_args(argc, argv);
  int ac = (int)av.size();
  AppOptions opts;
  std::vector<std::string> positional;
  bool no_more_flags = false;
  for (int i = 1; i < ac; i++) {
    std::string a = av[i];
    if (no_more_flags || a.empty() || a == "-" || a[0] != '-') {  // "-" = stdin
      positional.push_back(a);
      continue;
    }
    if (a == "--") { no_more_flags = true; continue; }
    std::string v;
    if (a == "-h" || a == "--help") { print_usage(); return 0; }
    if (a == "-v" || a == "--version") { printf("mdt %s\n", MDT_VERSION); return 0; }
    if (arg_value(a, "--gfx", &v) || (a == "--gfx" && i + 1 < ac && (v = av[++i], true))) {
      if (!one_of(v, {"auto", "kitty", "iterm2", "sixel", "none"}))
        return usage_error("--gfx: expected auto|kitty|iterm2|sixel|none, got '" + v + "'");
      opts.gfx = v;
      continue;
    }
    if (arg_value(a, "--math", &v)) {
      if (!one_of(v, {"katex", "unicode", "off"}))
        return usage_error("--math: expected katex|unicode|off, got '" + v + "'");
      opts.math = v;
      continue;
    }
    bool is_theme = false;
    if (a == "--theme" && i + 1 < ac) { opts.theme = av[++i]; is_theme = true; }
    else if (arg_value(a, "--theme", &v)) { opts.theme = v; is_theme = true; }
    if (is_theme) {
      if (!one_of(opts.theme, {"dark", "light", "nord", "monochrome"}))
        return usage_error("--theme: expected dark|light|nord|monochrome, got '" + opts.theme + "'");
      continue;
    }
    if (a == "--toc") { opts.toc = true; continue; }
    if (a == "--no-color") { opts.no_color = true; opts.theme = "monochrome"; continue; }
    if (a == "--line-numbers") { opts.show_line_numbers = true; continue; }
    if (a == "--syntax" ) { opts.syntax = true; continue; }
    if (a == "--no-syntax") { opts.syntax = false; continue; }
    if (a.rfind("--entities=", 0) == 0) {
      opts.entities = a.substr(11);
      if (!one_of(opts.entities, {"off", "force"}))
        return usage_error("--entities: expected off|force, got '" + opts.entities + "'");
      continue;
    }
    if (a.rfind("--escapes=", 0) == 0) {
      opts.escapes = a.substr(10);
      if (!one_of(opts.escapes, {"auto", "off", "force"}))
        return usage_error("--escapes: expected auto|off|force, got '" + opts.escapes + "'");
      continue;
    }
    if (a.rfind("--loose=", 0) == 0) {
      opts.loose = a.substr(8);
      if (!one_of(opts.loose, {"auto", "off", "on"}))
        return usage_error("--loose: expected auto|off|on, got '" + opts.loose + "'");
      continue;
    }
    if (a == "--panels") { opts.panels = true; continue; }
    if (a == "--no-scrollbar") { opts.scrollbar = false; continue; }
    if (a == "--terminal-bg") { opts.terminal_bg = true; continue; }
    // 0 keeps its meaning of "derive it" (terminal width, the terminal's own
    // cell size, the font's size); anything else has to be a sane number, so
    // --width=1000000 cannot ask for a million-column framebuffer.
    if (arg_value(a, "--font-px", &v)) {
      if (!parse_int(v, 0, 512, &opts.font_px))
        return usage_error("--font-px: expected 0 (derive it) .. 512, got '" + v + "'");
      continue;
    }
    if (arg_value(a, "--width", &v)) {
      if (!parse_int(v, 0, 4096, &opts.width))
        return usage_error("--width: expected 0 (auto) .. 4096 columns, got '" + v + "'");
      continue;
    }
    if (arg_value(a, "--height", &v)) {
      if (!parse_int(v, 0, 4096, &opts.height))
        return usage_error("--height: expected 0 (auto) .. 4096 rows, got '" + v + "'");
      continue;
    }
    if (arg_value(a, "--cell", &v)) {  // --cell=10x20 (or 10,20): cell size in pixels
      int cw = 0, ch = 0;
      if (sscanf(v.c_str(), "%dx%d", &cw, &ch) != 2 && sscanf(v.c_str(), "%d,%d", &cw, &ch) != 2)
        return usage_error("--cell: expected WxH in pixels (e.g. --cell=9x19), got '" + v + "'");
      if (cw < 1 || cw > 512 || ch < 1 || ch > 512)
        return usage_error("--cell: W and H must be 1..512 pixels, got '" + v + "'");
      opts.cell_w = cw;
      opts.cell_h = ch;
      continue;
    }
    if (arg_value(a, "--screenshot", &v)) {
      if (v.empty()) return usage_error("--screenshot needs a file name: --screenshot=out.png");
      opts.screenshot = v;
      continue;
    }
    if (a == "--compat") { opts.compat = true; continue; }
    if (a == "--code-fit") { opts.code_fit = true; continue; }
    if (a == "--diag") { opts.diag = true; continue; }
    if (a == "--dump") { opts.dump = "-"; continue; }
    if (arg_value(a, "--dump", &v)) { opts.dump = v; continue; }
    if (a == "--list-caps") { opts.list_caps = true; continue; }
    fprintf(stderr, "mdt: unknown option %s\n", a.c_str());
    print_usage();
    return 2;
  }
  opts.files = positional;
  if (opts.files.empty() && !opts.list_caps) {
    print_usage();
    return 2;
  }
  // The reader shows one document; taking the first of several and dropping the
  // rest without a word was worse than saying so.
  if (opts.files.size() > 1)
    return usage_error("mdt reads one file at a time, got " + std::to_string(opts.files.size()) +
                       " (" + opts.files[0] + ", " + opts.files[1] + ", ...)");
  // Only one non-interactive output at a time, or the caller cannot tell which
  // one happened.
  int outputs = (opts.diag ? 1 : 0) + (opts.dump.empty() ? 0 : 1) + (opts.screenshot.empty() ? 0 : 1);
  if (outputs > 1)
    return usage_error("--diag, --dump and --screenshot are alternatives; pick one");
  return run_app(opts);
}
