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
#include "i18n.h"
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
  fprintf(stderr, tr("mdt: %s\n\n", "mdt: %s\n\n"), msg.c_str());
  print_usage();
  return 2;
}

// "%s: expected %s, got '%s'" is most of what this file has to say, so it gets
// one translation and every option uses it.
static std::string want_got(const char* option, const char* expected, const std::string& got) {
  return fmt(tr("%s: expected %s, got '%s'", "%s: 需要 %s 之一，但给了 '%s'"), option, expected,
             got.c_str());
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

// ---------------------------------------------------------------- config -----
// One settings file, so a reader does not have to type --panels --theme=nord
// every time: "key = value" lines, "#" starts a comment, the command line
// always wins over the file.
//
//   ~/.config/mdt/config          ($XDG_CONFIG_HOME/mdt/config on Linux/macOS,
//                                  %APPDATA%\\mdt\\config on Windows)
//
// MDT_CONFIG=/path/to/file points at another file (used by the test suite).
static std::string default_config_path() {
  const char* forced = getenv("MDT_CONFIG");
  if (forced && *forced) return forced;
#ifdef _WIN32
  const char* appdata = getenv("APPDATA");
  return appdata && *appdata ? std::string(appdata) + "\\mdt\\config" : std::string();
#else
  const char* xdg = getenv("XDG_CONFIG_HOME");
  if (xdg && *xdg) return std::string(xdg) + "/mdt/config";
  const char* home = getenv("HOME");
  return home && *home ? std::string(home) + "/.config/mdt/config" : std::string();
#endif
}

// ---------------------------------------------------------------- language ---
// This runs before anything is printed, because the first thing mdt might
// print is an error, and it should be in the language the reader asked for.
// MDT_LANG is mdt's own variable and counts as an explicit request: a value it
// does not know gets a warning rather than silence.  LANG is set for every
// program on the machine, so an unrecognised value there is simply not ours to
// complain about - it falls back to English.
static bool g_lang_from_mdt_env = false;

static void language_from_environment() {
  if (const char* v = getenv("MDT_LANG"); v && *v) {
    Lang lang = Lang::En;
    if (lang_from_name(v, &lang)) {
      set_language(lang);
    } else {
      fprintf(stderr,
              tr("mdt: MDT_LANG=%s is not a language mdt speaks (%s), using English\n",
                 "mdt: MDT_LANG=%s 不是 mdt 支持的语言（可用 %s），改用英文\n"),
              v, lang_list());
      set_language(Lang::En);
    }
    set_language_source(std::string("MDT_LANG=") + v);
    g_lang_from_mdt_env = true;
    return;
  }
  for (const char* name : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
    if (const char* v = getenv(name); v && *v) {
      set_language(lang_from_env_string(v));
      set_language_source(std::string(name) + "=" + v);
      return;
    }
  }
  set_language(Lang::En);
  set_language_source("default (English)");
}

static bool truthy(const std::string& v) {
  return v == "1" || v == "on" || v == "yes" || v == "true" || v == "always";
}

// Applies the values that are safe to set from a file.  Anything unrecognised
// is reported (a typo in a config file is as annoying as one on the command
// line) but does not stop the reader.
static void load_config(AppOptions* opts, bool lang_explicit) {
  std::string path = default_config_path();
  if (path.empty()) return;
  std::string text;
  if (!read_file(path, text)) return;  // no file is the normal case
  opts->config_path = path;
  size_t pos = 0, line_no = 0;
  while (pos <= text.size()) {
    size_t nl = text.find('\n', pos);
    std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
    pos = nl == std::string::npos ? text.size() + 1 : nl + 1;
    line_no++;
    size_t hash = line.find('#');
    if (hash != std::string::npos) line = line.substr(0, hash);
    line = trim(line);
    if (line.empty()) continue;
    size_t eq = line.find('=');
    if (eq == std::string::npos) {
      fprintf(stderr, tr("mdt: %s:%zu: expected key = value, got '%s'\n",
                         "mdt: %s:%zu: 应该是「键 = 值」，但看到 '%s'\n"),
              path.c_str(), line_no, line.c_str());
      continue;
    }
    std::string key = to_lower(trim(line.substr(0, eq)));
    std::string val = trim(line.substr(eq + 1));
    if (key == "theme") {
      if (one_of(val, {"dark", "light", "nord", "monochrome"})) opts->theme = val;
      else fprintf(stderr, tr("mdt: %s:%zu: unknown theme '%s'\n", "mdt: %s:%zu: 不认识的主题 '%s'\n"),
                    path.c_str(), line_no, val.c_str());
    } else if (key == "gfx") {
      if (one_of(val, {"auto", "kitty", "iterm2", "sixel", "none"})) opts->gfx = val;
      else fprintf(stderr, tr("mdt: %s:%zu: unknown gfx '%s'\n",
                              "mdt: %s:%zu: 不认识的图形协议 '%s'\n"),
                    path.c_str(), line_no, val.c_str());
    } else if (key == "math") {
      if (one_of(val, {"katex", "unicode", "off"})) opts->math = val;
      else fprintf(stderr, tr("mdt: %s:%zu: unknown math '%s'\n",
                              "mdt: %s:%zu: 不认识的公式引擎 '%s'\n"),
                    path.c_str(), line_no, val.c_str());
    } else if (key == "encoding") {
      std::string v = to_lower(val);
      if (one_of(v, {"auto", "utf-8", "utf8", "gbk", "gb18030", "big5", "cp1252", "latin1", "off"}))
        opts->encoding = (v == "utf8") ? "utf-8" : v;
      else fprintf(stderr, tr("mdt: %s:%zu: unknown encoding '%s'\n",
                              "mdt: %s:%zu: 不认识的编码 '%s'\n"),
                    path.c_str(), line_no, val.c_str());
    } else if (key == "panels") {
      opts->panels = truthy(val);
    } else if (key == "terminal_bg" || key == "terminal-bg") {
      opts->terminal_bg = truthy(val);
    } else if (key == "scrollbar") {
      opts->scrollbar = truthy(val);
    } else if (key == "line_numbers" || key == "line-numbers") {
      opts->show_line_numbers = truthy(val);
    } else if (key == "syntax") {
      opts->syntax = truthy(val);
    } else if (key == "code_fit" || key == "code-fit") {
      opts->code_fit = truthy(val);
    } else if (key == "lang" || key == "language") {
      // The config file is consulted only when neither --lang nor MDT_LANG
      // said anything: an explicit request should not be undone by a file the
      // reader may have forgotten about.
      if (!lang_explicit) {
        std::string v = to_lower(val);
        Lang lang = Lang::En;
        if (v == "auto") {
          language_from_environment();
          set_language_source("config (auto)");
        } else if (lang_from_name(v, &lang)) {
          set_language(lang);
          set_language_source("config");
        } else {
          fprintf(stderr, tr("mdt: %s:%zu: unknown lang '%s' (%s)\n",
                             "mdt: %s:%zu: 不认识的 lang '%s'（可用 %s）\n"),
                  path.c_str(), line_no, val.c_str(), lang_list());
        }
      }
    } else {
      fprintf(stderr, tr("mdt: %s:%zu: unknown setting '%s'\n", "mdt: %s:%zu: 不认识的配置项 '%s'\n"),
              path.c_str(), line_no, key.c_str());
    }
  }
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

  // Language first: --lang, then MDT_LANG, then LC_ALL/LC_MESSAGES/LANG.  The
  // config file gets a say further down (load_config), but only when neither
  // of the two explicit ones was given.
  language_from_environment();
  std::string lang_arg;
  bool lang_on_cli = false;
  for (int i = 1; i < ac; i++) {
    if (av[i].rfind("--lang=", 0) == 0) {
      lang_arg = av[i].substr(7);
      lang_on_cli = true;
    }
  }
  if (lang_on_cli && lang_arg != "auto") {
    Lang lang = Lang::En;
    if (lang_from_name(lang_arg, &lang)) {
      set_language(lang);
      set_language_source("--lang=" + lang_arg);
    } else {
      return usage_error(fmt(tr("--lang: expected %s, got '%s'", "--lang: 需要 %s 之一，但给了 '%s'"),
                             lang_list(), lang_arg.c_str()));
    }
  } else if (lang_on_cli) {
    set_language_source("--lang=auto");
  }
  bool lang_explicit = lang_on_cli || g_lang_from_mdt_env;

  AppOptions opts;
  load_config(&opts, lang_explicit);  // the command line below overrides anything from the file
  bool theme_from_cli = false;
  bool no_color_flag = false;
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
    // already applied above, before anything could print; this only records it
    if (a.rfind("--lang=", 0) == 0) { opts.lang = a.substr(7); continue; }
    if (arg_value(a, "--gfx", &v) || (a == "--gfx" && i + 1 < ac && (v = av[++i], true))) {
      if (!one_of(v, {"auto", "kitty", "iterm2", "sixel", "none"}))
        return usage_error(want_got("--gfx", "auto|kitty|iterm2|sixel|none", v));
      opts.gfx = v;
      continue;
    }
    if (arg_value(a, "--math", &v)) {
      if (!one_of(v, {"katex", "unicode", "off"}))
        return usage_error(want_got("--math", "katex|unicode|off", v));
      opts.math = v;
      continue;
    }
    bool is_theme = false;
    if (a == "--theme" && i + 1 < ac) { opts.theme = av[++i]; is_theme = true; }
    else if (arg_value(a, "--theme", &v)) { opts.theme = v; is_theme = true; }
    if (is_theme) {
      if (!one_of(opts.theme, {"dark", "light", "nord", "monochrome"}))
        return usage_error(want_got("--theme", "dark|light|nord|monochrome", opts.theme));
      theme_from_cli = true;
      continue;
    }
    if (a == "--toc") { opts.toc = true; continue; }
    if (a == "--no-color") {
      opts.no_color = true;
      opts.theme = "monochrome";
      no_color_flag = true;
      continue;
    }
    if (a == "--line-numbers") { opts.show_line_numbers = true; continue; }
    if (a == "--syntax" ) { opts.syntax = true; continue; }
    if (a == "--no-syntax") { opts.syntax = false; continue; }
    if (a.rfind("--entities=", 0) == 0) {
      opts.entities = a.substr(11);
      if (!one_of(opts.entities, {"off", "force"}))
        return usage_error(want_got("--entities", "off|force", opts.entities));
      continue;
    }
    if (a.rfind("--escapes=", 0) == 0) {
      opts.escapes = a.substr(10);
      if (!one_of(opts.escapes, {"auto", "off", "force"}))
        return usage_error(want_got("--escapes", "auto|off|force", opts.escapes));
      continue;
    }
    if (a.rfind("--encoding=", 0) == 0) {
      opts.encoding = a.substr(11);
      for (auto& ch : opts.encoding) ch = (char)tolower((unsigned char)ch);
      if (!one_of(opts.encoding, {"auto", "utf-8", "utf8", "gbk", "gb18030", "big5", "cp1252",
                                  "latin1", "off"}))
        return usage_error(want_got("--encoding", "auto|utf-8|gbk|gb18030|big5|cp1252|latin1|off",
                                    opts.encoding));
      if (opts.encoding == "utf8") opts.encoding = "utf-8";
      continue;
    }
    if (a.rfind("--loose=", 0) == 0) {
      opts.loose = a.substr(8);
      if (!one_of(opts.loose, {"auto", "off", "on"}))
        return usage_error(want_got("--loose", "auto|off|on", opts.loose));
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
        return usage_error(fmt(tr("--font-px: expected 0 (derive it) .. 512, got '%s'",
                                  "--font-px: 需要 0（自动推导）到 512 之间的值，但给了 '%s'"),
                               v.c_str()));
      continue;
    }
    if (arg_value(a, "--width", &v)) {
      if (!parse_int(v, 0, 4096, &opts.width))
        return usage_error(fmt(tr("--width: expected 0 (auto) .. 4096 columns, got '%s'",
                                  "--width: 需要 0（自动）到 4096 列之间的值，但给了 '%s'"),
                               v.c_str()));
      continue;
    }
    if (arg_value(a, "--height", &v)) {
      if (!parse_int(v, 0, 4096, &opts.height))
        return usage_error(fmt(tr("--height: expected 0 (auto) .. 4096 rows, got '%s'",
                                  "--height: 需要 0（自动）到 4096 行之间的值，但给了 '%s'"),
                               v.c_str()));
      continue;
    }
    if (arg_value(a, "--cell", &v)) {  // --cell=10x20 (or 10,20): cell size in pixels
      int cw = 0, ch = 0;
      if (sscanf(v.c_str(), "%dx%d", &cw, &ch) != 2 && sscanf(v.c_str(), "%d,%d", &cw, &ch) != 2)
        return usage_error(fmt(tr("--cell: expected WxH in pixels (e.g. --cell=9x19), got '%s'",
                                  "--cell: 需要 WxH 形式的像素尺寸（例如 --cell=9x19），但给了 '%s'"),
                               v.c_str()));
      if (cw < 1 || cw > 512 || ch < 1 || ch > 512)
        return usage_error(fmt(tr("--cell: W and H must be 1..512 pixels, got '%s'",
                                  "--cell: 宽和高必须在 1..512 像素之间，但给了 '%s'"),
                               v.c_str()));
      opts.cell_w = cw;
      opts.cell_h = ch;
      continue;
    }
    if (arg_value(a, "--screenshot", &v)) {
      if (v.empty())
        return usage_error(tr("--screenshot needs a file name: --screenshot=out.png",
                              "--screenshot 需要一个文件名：--screenshot=out.png"));
      opts.screenshot = v;
      continue;
    }
    if (a == "--compat") { opts.compat = true; continue; }
    if (a == "--code-fit") { opts.code_fit = true; continue; }
    if (a == "--diag") { opts.diag = true; continue; }
    if (a == "--dump") { opts.dump = "-"; continue; }
    if (arg_value(a, "--dump", &v)) { opts.dump = v; continue; }
    if (a == "--list-caps") { opts.list_caps = true; continue; }
    fprintf(stderr, tr("mdt: unknown option %s\n", "mdt: 不认识的选项 %s\n"), a.c_str());
    print_usage();
    return 2;
  }
  // https://no-color.org: NO_COLOR (any non-empty value) asks for no colour at
  // all.  An explicit --theme/--no-color, or a theme in the config file, is a
  // more specific request and wins.
  if (const char* nc = getenv("NO_COLOR")) {
    if (*nc && !theme_from_cli && !no_color_flag && opts.theme == "dark") {
      opts.theme = "monochrome";
      opts.no_color = true;
    }
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
