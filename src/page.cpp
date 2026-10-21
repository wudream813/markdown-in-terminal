// page.cpp : the page renderer (--render=image) - see page.h.
#include "page.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "util.h"

namespace mdt {

bool PageRaster::init(int cell_w, int cell_h, std::string* err) {
  if (base_ && cell_w == cell_w_ && cell_h == cell_h_) return true;
  base_.reset();
  big_.reset();
  if (cell_w <= 0 || cell_h <= 0) {
    if (err) *err = "unknown cell size";
    return false;
  }
  auto base = std::make_unique<FontRaster>();
  if (!base->init(cell_w, cell_h, err)) return false;
  // headings 1-2: the same font at twice the cell size
  auto big = std::make_unique<FontRaster>();
  if (!big->init(cell_w * 2, cell_h * 2, err)) return false;
  base_ = std::move(base);
  big_ = std::move(big);
  cell_w_ = cell_w;
  cell_h_ = cell_h;
  return true;
}

const std::string& PageRaster::font_path() const {
  static const std::string none;
  return base_ ? base_->path() : none;
}

void PageRaster::paint(const Screen& scr, const std::vector<PlacedImage>& imgs,
                       const std::vector<BigText>& bigs, const std::vector<PageRect>& covers,
                       int doc_rows, bool snap_subcell, Image& img) {
  int cols = scr.width(), rows = scr.height();
  int cw = cell_w_, ch = cell_h_;
  img.w = cols * cw;
  img.h = rows * ch;
  img.rgba.assign((size_t)img.w * img.h * 4, 0);
  if (!base_) return;
  auto cell = [&](int x, int y) {
    const Cell& c = scr.at(x, y);
    base_->draw_cell(img, x, y, c.cp, c.fg, c.bg, c.attr);
  };
  for (int y = 0; y < rows; y++)
    for (int x = 0; x < cols; x++) cell(x, y);

  // pictures and typeset formulas, alpha-blended over the page
  int max_y = std::min(img.h, std::max(0, doc_rows) * ch);
  for (const PlacedImage& im : imgs) {
    if (!im.rgba || im.rgba->empty() || im.px_w <= 0 || im.px_h <= 0) continue;
    if (im.rgba->size() < (size_t)im.px_w * im.px_h * 4) continue;
    // a source crop (kitty-style placement) shows part of the bitmap
    int sx0 = 0, sy0 = 0, sw = im.px_w, sh = im.px_h;
    if (im.src_w > 0 && im.src_h > 0) {
      sx0 = std::max(0, im.src_x);
      sy0 = std::max(0, im.src_y);
      sw = std::min(im.src_w, im.px_w - sx0);
      sh = std::min(im.src_h, im.px_h - sy0);
    }
    int ox = im.x * cw + (snap_subcell ? 0 : im.sub_x);
    int oy = im.y * ch + (snap_subcell ? 0 : im.sub_y);
    for (int y = 0; y < sh; y++) {
      int yy = oy + y;
      if (yy < 0 || yy >= max_y) continue;
      const uint8_t* srow = &(*im.rgba)[((size_t)(sy0 + y) * im.px_w + sx0) * 4];
      uint8_t* drow = &img.rgba[(size_t)yy * img.w * 4];
      for (int x = 0; x < sw; x++) {
        int xx = ox + x;
        if (xx < 0 || xx >= img.w) continue;
        const uint8_t* sp = srow + (size_t)x * 4;
        int a = sp[3];
        if (!a) continue;
        uint8_t* dp = drow + (size_t)xx * 4;
        dp[0] = (uint8_t)((sp[0] * a + dp[0] * (255 - a)) / 255);
        dp[1] = (uint8_t)((sp[1] * a + dp[1] * (255 - a)) / 255);
        dp[2] = (uint8_t)((sp[2] * a + dp[2] * (255 - a)) / 255);
        dp[3] = 255;
      }
    }
  }

  // enlarged headings
  for (const BigText& bt : bigs) {
    FontRaster* f = bt.scale == 2 && big_ ? big_.get() : base_.get();
    int scale = f == big_.get() ? 2 : 1;
    int clip0 = std::max(0, bt.clip_y0) * ch, clip1 = std::min(rows, bt.clip_y1) * ch;
    for (const BigText::Piece& pc : bt.pieces) {
      int col = pc.col;
      size_t i = 0;
      while (i < pc.text.size()) {
        uint32_t cp = utf8_next(pc.text, i);
        int w = cp_width(cp);
        if (w <= 0) continue;  // combining marks: the font has no room for them here
        int px = (bt.x + col * scale) * cw;
        if (px >= img.w) break;
        f->draw_cell_px(img, px, bt.y * ch, cp, pc.fg, bt.bg, pc.attr, clip0, clip1);
        col += w;
      }
    }
  }

  // panels, scrollbar, status bar: on top of the document again
  for (const PageRect& r : covers)
    for (int y = std::max(0, r.y); y < std::min(rows, r.y + r.h); y++)
      for (int x = std::max(0, r.x); x < std::min(cols, r.x + r.w); x++) {
        // a wide character whose left half is outside the rectangle still
        // owns this cell: repaint from its first column
        if (x == r.x && x > 0 && scr.at(x, y).cp == 0) cell(x - 1, y);
        cell(x, y);
      }
}

}  // namespace mdt
