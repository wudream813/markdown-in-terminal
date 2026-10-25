// dev tool: where does the memory go?
//
//   make mem-report && ./build/mem-report big.md [width]
//
// Parses the document, lays it out exactly like the reader does (whole
// document, one Line per screen row) and prints the struct sizes, the object
// counts and the peak RSS.  The point is to keep the numbers visible while the
// layout is being made cheaper - "the 4 MB file uses 500 MB" is not a bug
// report you can act on.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "../src/md.h"
#include "../src/render.h"

using namespace mdt;

// Resident set right now (Linux; 0 elsewhere) - the peak alone cannot tell
// what a phase keeps from what it only needed for a moment.
static double rss_now_mb() {
  FILE* f = fopen("/proc/self/statm", "r");
  if (!f) return 0;
  long pages = 0, res = 0;
  if (fscanf(f, "%ld %ld", &pages, &res) != 2) res = 0;
  fclose(f);
  return res * 4096.0 / 1e6;
}

struct Count {
  size_t blocks = 0, spans = 0, cells = 0, str_bytes = 0;
  void str(const std::string& s) {
    if (s.capacity() > 15) str_bytes += s.capacity() + 1;
  }
  void spans_of(const std::vector<Span>& v) {
    spans += v.size();
    for (auto& sp : v) { str(sp.text); str(sp.tex); str(sp.link); }
  }
  void walk(const std::vector<Block>& v) {
    for (auto& b : v) {
      blocks++;
      spans_of(b.spans);
      str(b.code);
      for (auto& it : b.items) walk(it);
      for (auto& r : b.rows)
        for (auto& c : r) { cells++; spans_of(c.spans); }
    }
  }
};

static double ms_since(std::chrono::steady_clock::time_point t) {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
}

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: mem-report file.md [width] [--no-layout]\n");
    return 1;
  }
  int width = argc > 2 && argv[2][0] != '-' ? atoi(argv[2]) : 100;
  bool layout = !(argc > 2 && std::string(argv[2]) == "--no-layout");

  size_t b_sz = 0, s_sz = 0, l_sz = 0, r_sz = 0, c_sz = 0;
  DocView::struct_sizes(&b_sz, &s_sz, &l_sz, &r_sz, &c_sz);
  printf("struct sizes        : Block=%zu Span=%zu Line=%zu Run=%zu TCell=%zu\n", b_sz, s_sz, l_sz, r_sz,
         c_sz);
  std::string text;
  if (!read_file(argv[1], text)) {
    fprintf(stderr, "cannot read %s\n", argv[1]);
    return 1;
  }
  double after_read = peak_rss_mb();

  auto t_parse = std::chrono::steady_clock::now();
  MarkdownParser parser;
  MdOptions mo;
  MdDocument doc = parser.parse(text, mo);
  double parse_ms = ms_since(t_parse);
  double after_parse = peak_rss_mb();
  Count n;
  n.walk(doc.blocks);
  printf("source              : %.2f MB\n", text.size() / 1e6);
  printf("parse               : %zu top-level blocks, %.0f ms, %.1f MB peak (%.1f MB before), "
         "%.1f MB resident after\n",
         doc.blocks.size(), parse_ms, after_parse, after_read, rss_now_mb());
  printf("document            : %zu blocks, %zu spans, %zu table cells, %.1f MB of heap strings\n",
         n.blocks, n.spans, n.cells, n.str_bytes / 1e6);

  if (!layout) return 0;
  DocView view;
  view.set_options(RenderOptions());
  view.set_width(width);
  view.set_view_rows(40);
  auto t_layout = std::chrono::steady_clock::now();
  view.set_document(std::move(doc), argv[1], ".");   // lays the document out
  double layout_ms = ms_since(t_layout);
  if (const char* sig = getenv("MDT_LAYOUT_SIG")) {  // equivalence checks: the whole layout as text
    view.finish_layout();
    FILE* f = fopen(sig, "wb");
    if (f) {
      std::string s = view.layout_signature();
      fwrite(s.data(), 1, s.size(), f);
      fclose(f);
    }
  }

  DocView::LayoutStats st = view.layout_stats();
  printf("layout              : %d rows total, %zu lines, %zu runs, %zu table cells, %.1f MB of run text, "
         "%.0f ms\n",
         st.rows, st.lines, st.runs, st.cells, st.run_bytes / 1e6, layout_ms);
  printf("peak RSS            : %.1f MB (%.1f MB resident now)\n", peak_rss_mb(), rss_now_mb());
  return 0;
}
