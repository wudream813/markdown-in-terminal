// page.h : the page renderer (--render=image).
//
// A text terminal draws mdt's cell grid with its own font; the page renderer
// draws it itself - the grid, the pictures and formulas, and headings at twice
// the size - into one bitmap per frame, which goes to the terminal through its
// graphics protocol (Terminal::emit_page).  --screenshot uses the same code,
// so a screenshot is exactly what the page mode shows.
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "font.h"
#include "image.h"
#include "render.h"
#include "term.h"

namespace mdt {

struct PageRect {
  int x = 0, y = 0, w = 0, h = 0;  // in cells
};

class PageRaster {
 public:
  // Loads the fonts for this cell size; a no-op when nothing changed.
  bool init(int cell_w, int cell_h, std::string* err = nullptr);
  bool ok() const { return base_ != nullptr; }
  int cell_w() const { return cell_w_; }
  int cell_h() const { return cell_h_; }
  const std::string& font_path() const;

  // Paints the frame into `out` (resized to the grid).  Order: the cell grid,
  // then the pictures (clipped to rows [0, doc_rows)), then the enlarged
  // headings, then the `covers` once more from the grid - the panels, the
  // scrollbar and the status bar sit on top of the document.
  // snap_subcell: draw pictures on their anchor cell, the way a terminal that
  // cannot offset an image inside a cell has to (tools/subcell_align_test.py).
  void paint(const Screen& scr, const std::vector<PlacedImage>& imgs,
             const std::vector<BigText>& bigs, const std::vector<PageRect>& covers,
             int doc_rows, bool snap_subcell, Image& out);

 private:
  std::unique_ptr<FontRaster> base_, big_;
  int cell_w_ = 0, cell_h_ = 0;
};

}  // namespace mdt
