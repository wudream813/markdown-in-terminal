// app.h : CLI options + interactive application.
#pragma once
#include <string>
#include <vector>

namespace mdt {

struct AppOptions {
  std::vector<std::string> files;
  std::string gfx = "auto";        // auto | kitty | iterm2 | sixel | none
  std::string math = "katex";      // katex | unicode | off
  std::string theme = "dark";      // dark | light | nord | monochrome
  bool toc = false;                // open the outline panel at start
  bool no_color = false;
  bool show_line_numbers = false;
  bool syntax = true;
  std::string entities = "auto";   // auto | off | force - HTML-escaped sources
  std::string escapes = "auto";    // auto | off | force - backslash-escaped sources
  std::string loose = "auto";      // auto | off | on - "#标题" / "-项目" without a space
  bool panels = false;             // draw tinted backgrounds behind code/quote/table
  bool terminal_bg = false;        // leave the background to the terminal
  bool scrollbar = true;           // show a scrollbar when the document is taller
  int font_px = 0;                 // override the derived em size
  int width = 0;                   // force a width in columns (dump/screenshot)
  int height = 0;
  std::string dump;
  bool diag = false;               // print what mdt sees of the file and of the terminal
  bool compat = false;             // --compat: plain output for terminals that misbehave
  std::string screenshot;          // write a PNG of the rendered screen
  bool list_caps = false;          // print terminal capabilities and exit
  bool version = false;
  std::string help;
};

int run_app(const AppOptions& opts);
void print_usage();

}  // namespace mdt
