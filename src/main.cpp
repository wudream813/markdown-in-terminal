// main.cpp : argument parsing + entry point.
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

int main(int argc, char** argv) {
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
    if (arg_value(a, "--gfx", &v) || (a == "--gfx" && i + 1 < ac && (v = av[++i], true))) { opts.gfx = v; continue; }
    if (arg_value(a, "--math", &v)) { opts.math = v; continue; }
    if (a == "--theme" && i + 1 < ac) { opts.theme = av[++i]; continue; }
    if (arg_value(a, "--theme", &v)) { opts.theme = v; continue; }
    if (a == "--toc") { opts.toc = true; continue; }
    if (a == "--no-color") { opts.no_color = true; opts.theme = "monochrome"; continue; }
    if (a == "--line-numbers") { opts.show_line_numbers = true; continue; }
    if (a == "--syntax" ) { opts.syntax = true; continue; }
    if (a == "--no-syntax") { opts.syntax = false; continue; }
    if (a.rfind("--entities=", 0) == 0) { opts.entities = a.substr(11); continue; }
    if (a.rfind("--escapes=", 0) == 0) { opts.escapes = a.substr(10); continue; }
    if (a.rfind("--loose=", 0) == 0) { opts.loose = a.substr(8); continue; }
    if (a == "--panels") { opts.panels = true; continue; }
    if (a == "--no-scrollbar") { opts.scrollbar = false; continue; }
    if (a == "--terminal-bg") { opts.terminal_bg = true; continue; }
    if (arg_value(a, "--font-px", &v)) { opts.font_px = atoi(v.c_str()); continue; }
    if (arg_value(a, "--width", &v)) { opts.width = atoi(v.c_str()); continue; }
    if (arg_value(a, "--cell", &v)) {  // --cell=10x20 (or 10,20): cell size in pixels
      int cw = 0, ch = 0;
      if (sscanf(v.c_str(), "%dx%d", &cw, &ch) == 2 || sscanf(v.c_str(), "%d,%d", &cw, &ch) == 2) {
        opts.cell_w = cw;
        opts.cell_h = ch;
      }
      continue;
    }
    if (arg_value(a, "--height", &v)) { opts.height = atoi(v.c_str()); continue; }
    if (arg_value(a, "--screenshot", &v)) { opts.screenshot = v; continue; }
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
  return run_app(opts);
}
