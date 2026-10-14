// dev tool: where does the memory go?
//
//   make mem-report && ./build/mem-report big.md [width]
//
// Parses the document, lays it out exactly like the reader does (whole
// document, one Line per screen row) and prints the struct sizes, the object
// counts and the peak RSS.  The point is to keep the numbers visible while the
// layout is being made cheaper - "the 4 MB file uses 500 MB" is not a bug
// report you can act on.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "../src/md.h"
#include "../src/render.h"

using namespace mdt;

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

  MarkdownParser parser;
  MdOptions mo;
  MdDocument doc = parser.parse(text, mo);
  size_t spans = 0;
  for (auto& b : doc.blocks) spans += b.spans.size();
  printf("source              : %.2f MB\n", text.size() / 1e6);
  printf("parse               : %zu blocks, %zu top-level spans, %.1f MB peak\n", doc.blocks.size(),
         spans, after_read);

  if (!layout) return 0;
  DocView view;
  view.set_options(RenderOptions());
  view.set_width(width);
  view.set_view_rows(40);
  view.set_document(std::move(doc), argv[1], ".");
  view.relayout();

  DocView::LayoutStats st = view.layout_stats();
  printf("layout              : %d rows total, %zu lines, %zu runs, %zu table cells, %.1f MB of run text\n",
         st.rows, st.lines, st.runs, st.cells, st.run_bytes / 1e6);
  printf("peak RSS            : %.1f MB\n", peak_rss_mb());
  return 0;
}
