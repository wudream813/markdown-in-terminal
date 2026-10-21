// dev/test tool: does refresh_layout() (only the blocks that showed a newly
// measured formula are laid out again) end up where a full relayout() does?
//
//   make refresh-check && ./build/refresh-check file.md [width]
//
// Scrolls through the document page by page with lazy formula metrics, the
// way the reader does, calling refresh_layout() whenever a draw measured
// something.  Once a whole pass measures nothing new, the incremental layout
// must be identical to a full relayout of the same document.  Exit 0 = same.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "../src/mdmath.h"
#include "../src/md.h"
#include "../src/render.h"
#include "../src/term.h"

using namespace mdt;

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: refresh-check file.md [width]\n");
    return 2;
  }
  int cols = argc > 2 ? atoi(argv[2]) : 90, rows = 40;
  std::string text;
  if (!read_file(argv[1], text)) {
    fprintf(stderr, "cannot read %s\n", argv[1]);
    return 2;
  }
  MarkdownParser parser;
  MdDocument doc = parser.parse(text, MdOptions());

  MathRenderer math;
  math.init(true);
  Terminal term;
  term.caps.cols = cols;
  term.caps.rows = rows;
  term.caps.cell_w = 10;
  term.caps.cell_h = 20;
  term.caps.win_w = cols * 10;
  term.caps.win_h = rows * 20;
  term.caps.gfx = GfxProto::Kitty;
  DocView view;
  view.set_terminal(&term);
  view.set_math(&math);
  view.set_offscreen(true);
  RenderOptions ro;
  ro.lazy_metrics = true;
  ro.inline_images = false;
  ro.remote_images = false;
  view.set_options(ro);
  view.set_width(cols);
  view.set_view_rows(rows);
  view.set_document(std::move(doc), argv[1], ".");

  Screen scr;
  scr.init(cols, rows, RGB{200, 200, 200}, RGB{0, 0, 0});
  int refreshes = 0;
  double refresh_ms = 0;
  for (int pass = 0; pass < 20; pass++) {
    bool measured = false;
    for (int top = 0; top < view.total_rows(); top += rows / 2) {
      view.draw(scr, 0, 0, cols, rows, top);
      if (view.layout_dirty()) {
        auto t = std::chrono::steady_clock::now();
        view.refresh_layout();
        refresh_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
        refreshes++;
        measured = true;
      }
    }
    if (!measured) break;
  }
  std::string inc = view.layout_signature();
  view.relayout();
  std::string full = view.layout_signature();
  // Folding a callout open/closed lays out that one block again, too.
  int toggles = 0;
  bool toggles_ok = true;
  if (inc == full) {
    for (int top = 0; top < view.total_rows() && toggles < 50; top++) {
      view.scroll_to(top);
      if (view.scroll_top() != top) break;
      int mx = 0;
      while (mx < cols && view.fold_title_at(mx, 0) < 0) mx++;
      if (mx >= cols) continue;
      view.toggle_box_at(mx, 0);
      toggles++;
      std::string a = view.layout_signature();
      int keep = view.scroll_top();
      view.relayout();
      if (a != view.layout_signature()) {
        toggles_ok = false;
        inc = a;
        full = view.layout_signature();
        break;
      }
      view.scroll_to(keep);
    }
  }
  printf("%s: %d refreshes, %.1f ms total, %d toggles, %d rows, js=%d -> %s\n", argv[1], refreshes,
         refresh_ms, toggles, view.total_rows(), math.has_js() ? 1 : 0,
         inc == full && toggles_ok ? "same" : "DIFFERENT");
  if (inc != full) {
    size_t i = 0;
    while (i < inc.size() && i < full.size() && inc[i] == full[i]) i++;
    size_t from = inc.rfind('\n', i);
    from = from == std::string::npos ? 0 : from + 1;
    fprintf(stderr, "first difference:\n  refresh: %.200s\n  full   : %.200s\n", inc.c_str() + from,
            full.c_str() + from);
    return 1;
  }
  return 0;
}
