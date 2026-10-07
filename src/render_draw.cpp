// render_draw.cpp : the drawing half of DocView (text grid + graphics overlays).
#include <algorithm>
#include <cmath>
#include <cstring>

#include "render.h"

namespace mdt {

int DocView::block_at_row(int row) const {
  if (layout_.empty()) return -1;
  int lo = 0, hi = (int)layout_.size() - 1;
  while (lo < hi) {
    int mid = (lo + hi + 1) / 2;
    if (layout_[(size_t)mid].row <= row) lo = mid;
    else hi = mid - 1;
  }
  return layout_[(size_t)lo].block;
}
int DocView::row_of_block(int block) const {
  for (auto& bl : layout_) if (bl.block == block) return bl.row;
  return 0;
}
void DocView::scroll_to(int row) {
  int maxr = std::max(0, total_rows_ - std::max(1, view_rows_));
  scroll_ = std::max(0, std::min(row, maxr));
}
void DocView::scroll_by(int dy) { scroll_to(scroll_ + dy); }
void DocView::scroll_page(int dir) { scroll_to(scroll_ + dir * std::max(1, view_rows_ - 2)); }
void DocView::scroll_home() { scroll_to(0); }
void DocView::scroll_end() { scroll_to(total_rows_); }

std::string DocView::plain_text() const {
  std::string out;
  for (auto& b : doc_.blocks) {
    switch (b.type) {
      case Block::Heading: out += std::string((size_t)std::max(1, b.level), '#') + " "; out += spans_plain_text(b.spans); out += "\n"; break;
      case Block::Paragraph: out += spans_plain_text(b.spans); out += "\n\n"; break;
      case Block::CodeBlock: out += "```" + b.lang + "\n" + b.code + "\n```\n\n"; break;
      case Block::MathBlock: out += "$$\n" + b.code + "\n$$\n\n"; break;
      case Block::Hr: out += "---\n\n"; break;
      case Block::Directive:
        if (!b.dir_leaf && !b.dir_label.empty()) { out += b.dir_label; out += "\n"; }
        break;
      case Block::Quote: out += "> "; break;
      case Block::List: out += "- "; break;
      default: break;
    }
  }
  return out;
}

// ------------------------------------------------------------- drawing ------
void DocView::draw_line(Screen& scr, int x0, int y0, int w, const Line& line, int sy) {
  (void)w;
  switch (line.kind) {
    case Line::Rule: {
      RGB c = theme_.rule;
      scr.hline(x0, sy, cols_ - x0, 0x2500, c, theme_.bg, A_DIM);
      break;
    }
    case Line::Table: {
      // handled in draw_table
      break;
    }
    case Line::Image: {
      break;  // placed as a graphic
    }
    case Line::Code:
    case Line::Text:
    default: {
      for (int bcol : line.bars)
        if (bcol >= 0)
          scr.put(x0 + bcol, sy, 0x2502,
                  line.bars_custom ? line.bar_rgb : theme_.quote_bar, theme_.bg);
      if (!line.marker.empty())  // dimmed "#" before a heading
        scr.put_str(x0 + 1, sy, line.marker, theme_.muted, theme_.bg, A_DIM);
      for (const Run& r : line.runs) {
        if (r.img >= 0) continue;  // graphics are placed separately
        RGB fg = r.style.has_color ? r.style.color : theme_.fg;
        RGB bg = theme_.bg;
        if (r.style.kind == Span::Code) bg = theme_.flat_bg ? theme_.bg : theme_.code_bg;
        if (r.style.kind == Span::Code && !r.style.has_color) fg = theme_.code_fg;
        uint8_t attr = 0;
        if (r.style.bold) attr |= A_BOLD;
        if (r.style.italic) attr |= A_ITALIC;
        if (r.style.underline) attr |= A_UNDER;
        if (r.style.strike) attr |= A_STRIKE;
        if (r.style.dim) attr |= A_DIM;
        int x = x0 + r.x;
        if (r.style.kind == Span::Code) {
          for (int k = 0; k < r.width && x + k < x0 + cols_; k++)
            if (x + k >= x0) scr.put(x + k, sy, ' ', fg, bg);
        }
        scr.put_str(x, sy, r.text, fg, bg, attr);
      }
      break;
    }
  }
}

void DocView::draw_code_block(Screen& scr, int x0, int y0, int w, int h, const BlockLayout& bl) {
  const Block& b = doc_.blocks[(size_t)bl.block];
  int top = y0 + bl.row;
  int rows = bl.rows - opt_.paragraph_gap;
  if (rows < 2) return;
  int frame_x = x0;
  int frame_w = std::min(std::max(1, cols_ - frame_x), code_frame_width(b));
  RGB bg = theme_.flat_bg ? theme_.bg : theme_.code_bg;
  RGB frame = theme_.code_frame;
  // background
  for (int yy = top; yy < top + rows; yy++)
    for (int xx = frame_x; xx < frame_x + frame_w; xx++) scr.put(xx, yy, ' ', theme_.code_fg, bg);
  // frame
  scr.box_rounded(frame_x, top, frame_w, rows, frame, bg);
  // language label in the top border
  if (!b.lang.empty()) {
    std::string label = " " + b.lang + " ";
    if ((int)label.size() + 6 < frame_w) scr.put_str(frame_x + 3, top, label, theme_.muted, bg);
  }
  CodeHighlight hl = theme_.syntax ? highlight_code(b.code, b.lang, theme_) : CodeHighlight();
  int text_x = frame_x + 2;
  int line_no_w = 0;
  if (opt_.show_line_numbers) {
    int nlines = (int)split_lines(b.code).size();
    line_no_w = (int)std::to_string(nlines).size() + 2;
  }
  for (const Line& l : bl.lines) {
    if (l.kind != Line::Code || l.code_index < 0) continue;  // borders have index -1/-2
    int yy = top + l.row;
    if (yy >= top + rows - 1) break;
    // Luogu "```cpp lines=5-6,11": the listed source lines get a warm tint
    RGB lbg = bg;
    if (!b.hl_ranges.empty()) {
      int n1 = l.code_index + 1;
      for (const auto& rg : b.hl_ranges)
        if (n1 >= rg.first && n1 <= rg.second) {
          lbg = RGB{(uint8_t)std::min(255, bg.r + 26), (uint8_t)std::min(255, bg.g + 21),
                    (uint8_t)std::min(255, bg.b + 8)};
          break;
        }
    }
    if (lbg.r != bg.r || lbg.g != bg.g || lbg.b != bg.b)
      for (int xx = frame_x + 1; xx < frame_x + frame_w - 1; xx++)
        scr.put(xx, yy, ' ', theme_.code_fg, lbg);
    if (opt_.show_line_numbers && l.code_col == 0) {
      std::string num = fmt("%*d ", line_no_w - 1, l.code_index + 1);
      scr.put_str(text_x, yy, num, theme_.muted, lbg, A_DIM);
    }
    const std::string& text = l.code_text;
    int x = text_x + line_no_w;
    size_t i = 0;
    while (i < text.size()) {
      size_t save = i;
      uint32_t cp = utf8_next(text, i);
      int cwid = cp_width(cp);
      if (cwid == 0) continue;
      RGB fg = theme_.code_fg;
      size_t src_off = (size_t)l.code_col + save;  // offset inside the source line
      if (hl.any && (size_t)l.code_index < hl.colors.size() && src_off < hl.colors[(size_t)l.code_index].size()) {
        RGB c = hl.colors[(size_t)l.code_index][src_off];
        if (c.r || c.g || c.b) fg = c;
      }
      if (x + cwid <= frame_x + frame_w - 1) scr.put(x, yy, cp, fg, lbg);
      x += cwid;
    }
  }
}

void DocView::draw_table(Screen& scr, int x0, int y0, int w, int h, const BlockLayout& bl) {
  (void)w; (void)h;
  const Block& b = doc_.blocks[(size_t)bl.block];
  // Luogu cute-table styles: "three" = booktabs (heavy top/bottom, thin header
  // rule, no verticals), "tuack" = booktabs plus thin verticals and, with
  // tuack=N, a heavy vertical rule after column N (1-based).
  const bool three = b.table_style == "three";
  const bool tuack = b.table_style == "tuack";
  const int heavy = tuack ? b.table_style_arg : 0;
  auto border_run = [&](int x, int yy, int n, uint32_t cp) {
    for (int k = 0; k < n; k++) scr.put(x + k, yy, cp, theme_.table_border, theme_.bg);
  };
  // merge marker of column c in the source row a border line touches:
  // which 0 = first row (top border), 1 = last row (bottom border),
  // 2 = first body row (the rule under the header)
  auto row_merge = [&](int which, int c) -> int {
    if (b.rows.empty() || c < 0) return 0;
    size_t r = 0;
    if (which == 1) r = b.rows.size() - 1;
    else if (which == 2) {
      while (r < b.rows.size() && !b.rows[r].empty() && b.rows[r][0].header) r++;
    }
    if (r >= b.rows.size() || c >= (int)b.rows[r].size()) return 0;
    return b.rows[r][(size_t)c].merge;
  };
  for (const Line& l : bl.lines) {
    if (l.kind != Line::Table || l.cells.empty()) continue;
    int yy = y0 + bl.row + l.row;
    int lx = x0 + l.x;  // nested tables start further right
    int span = 1 + l.cells.back().x + l.cells.back().width + 1;  // border columns

    if (l.code_index == 0 || l.code_index == 2 || l.code_index == 3) {  // border / header rule
      if (three || tuack) {
        uint32_t cp = l.code_index == 3 ? 0x2500 : 0x2501;  // ─ under the header, ━ top/bottom
        border_run(lx, yy, span, cp);
        continue;
      }
      int which = l.code_index == 0 ? 0 : l.code_index == 2 ? 1 : 2;
      uint32_t l_c, m_c, r_c;
      if (l.code_index == 0)      { l_c = 0x250C; m_c = 0x252C; r_c = 0x2510; }
      else if (l.code_index == 2) { l_c = 0x2514; m_c = 0x2534; r_c = 0x2518; }
      else                        { l_c = 0x251C; m_c = 0x253C; r_c = 0x2524; }
      scr.put(lx, yy, l_c, theme_.table_border, theme_.bg);
      for (size_t ci = 0; ci < l.cells.size(); ci++) {
        const TCell& tc = l.cells[ci];
        // a "^" cell in the row below continues through the header rule
        bool through = which == 2 && row_merge(2, (int)ci) == 1;
        border_run(lx + 1 + tc.x, yy, tc.width, through ? (uint32_t)' ' : (uint32_t)0x2500);
        // "<"/">" merges remove the junction between two columns
        bool open = row_merge(which, (int)ci + 1) == 2 || row_merge(which, (int)ci) == 3;
        scr.put(lx + 1 + tc.x + tc.width, yy, open ? 0x2500 : m_c, theme_.table_border,
                theme_.bg);
      }
      scr.put(lx + 1 + l.cells.back().x + l.cells.back().width, yy, r_c, theme_.table_border,
              theme_.bg);
      continue;
    }

    // content row
    for (size_t ci = 0; ci < l.cells.size(); ci++) {
      const TCell& tc = l.cells[ci];
      int x = lx + 1 + tc.x;
      RGB border = theme_.table_border;
      RGB fg = tc.header ? theme_.table_header_fg : theme_.fg;
      RGB bg = (tc.header && !theme_.flat_bg) ? theme_.table_header_bg : theme_.bg;
      // separator to the left of this cell: blank when it continues the cell
      // on the other side ("<" here, ">" in the neighbour)
      bool open_left = tc.merge == 2 || (ci > 0 && l.cells[ci - 1].merge == 3);
      uint32_t sep = three ? (uint32_t)' '
                   : (tuack && heavy > 0 && (int)ci == heavy) ? 0x2503
                                                              : 0x2502;
      scr.put(x - 1, yy, open_left ? (uint32_t)' ' : sep, border, theme_.bg);
      for (int k = 0; k < tc.width; k++) scr.put(x + k, yy, ' ', fg, bg);
      int tw = str_width(tc.text);
      int pad = tc.width - tw;
      int off = tc.align == Align::Right ? pad : (tc.align == Align::Center ? pad / 2 : 0);
      if (pad < 0) off = 0;
      scr.put_str(x + off, yy, tc.text, fg, bg, tc.header ? A_BOLD : 0);
      // formulas in this cell are drawn as bitmaps over their transcription
      if (!tc.pieces.empty())
        place_cell_math(&scr, x + off, tc.width, yy, tc, std::max(1, cell_w()),
                        std::max(1, cell_h()), 0, bg);
    }
    // right edge of the box
    int rx = lx + 1 + l.cells.back().x + l.cells.back().width;
    bool open_right = l.cells.back().merge == 3;
    uint32_t sep = three ? (uint32_t)' '
                 : (tuack && heavy > 0 && (int)l.cells.size() == heavy) ? 0x2503
                                                                        : 0x2502;
    scr.put(rx, yy, open_right ? (uint32_t)' ' : sep, theme_.table_border, theme_.bg);
  }
}

// Turns an estimated (pending) maths asset into the real one on first sight.
// Returns the asset to draw and records whether the layout needs refreshing.
std::shared_ptr<DocView::ImageAsset> DocView::resolve_asset(const ImageAsset& a) {
  if (!a.pending || !math_) return nullptr;
  auto real = asset_for_math(a.source, a.is_display, a.max_w, a.max_h, false, true);
  if (!real || real->failed) return nullptr;
  if (real->cols != a.cols || real->rows != a.rows ||
      std::abs(real->baseline_px - a.baseline_px) > 1.0)
    layout_dirty_ = true;
  return real;
}

void DocView::draw(Screen& scr, int x0, int y0, int w, int h, int scroll_row) {
  if (getenv("MDT_DEBUG_LAYOUT")) {
    for (auto& bl : layout_) {
      int imgs = 0;
      for (auto& l : bl.lines) { for (auto& r : l.runs) if (r.img >= 0) imgs++; }
      fprintf(stderr, "block %d row=%d rows=%d lines=%zu runs_with_img=%d type=%d\n",
              bl.block, bl.row, bl.rows, bl.lines.size(), imgs, (int)doc_.blocks[bl.block].type);
      if (getenv("MDT_DEBUG_LINES"))
        for (size_t k = 0; k < bl.lines.size(); k++) {
          const Line& dl = bl.lines[k];
          std::string dt;
          for (auto& dr : dl.runs) dt += "[" + std::to_string(dr.x) + ":" + dr.text + "]";
          fprintf(stderr, "  line %zu kind=%d row=%d bar=%d x=%d cells=%zu %s\n", k, (int)dl.kind,
                  dl.row, (dl.bars.empty() ? -1 : dl.bars[0]), dl.x, dl.cells.size(), dt.c_str());
        }
    }
    for (size_t i = 0; i < assets_.size(); i++)
      fprintf(stderr, "asset %zu %s %dx%d px cols=%d rows=%d baseline=%.1f\n", i,
              assets_[i]->source.substr(0, 24).c_str(), assets_[i]->px_w, assets_[i]->px_h,
              assets_[i]->cols, assets_[i]->rows, assets_[i]->baseline_px);
  }
  images_.clear();
  cell_assets_.clear();  // rebuilt with the frame (see place_cell_math)
  scr.fill_rect(x0, y0, w, h, theme_.bg);
  int chh = std::max(1, cell_h());
  for (const BlockLayout& bl : layout_) {
    if (bl.row + bl.rows <= scroll_row) continue;
    if (bl.row >= scroll_row + h) break;
    const Block& b = doc_.blocks[(size_t)bl.block];

    // Tables can also be nested inside list items and quotes, so the decision
    // follows the lines, not the block type.  A block may hold both table lines
    // and ordinary ones (a list item with a table in it), hence no `continue`.
    for (const Line& l : bl.lines)
      if (l.kind == Line::Table) { draw_table(scr, x0, y0 - scroll_row, w, h, bl); break; }
    if (b.type == Block::CodeBlock) {
      draw_code_block(scr, x0, y0 - scroll_row, w, h, bl);
      continue;
    }
    for (const Line& l : bl.lines) {
      int row = bl.row + l.row;
      // A line may span several rows (a picture, a multi-row formula): cull it
      // only once its LAST row is above the viewport, otherwise scrolling the
      // top of a picture out of view made the whole picture disappear.
      int span = std::max(1, l.rows);
      if (row + span <= scroll_row) continue;
      if (row >= scroll_row + h) break;
      int sy = y0 + row - scroll_row;
      draw_line(scr, x0, sy, w, l, sy);
      // graphics for this line
      if (l.kind == Line::Image && l.img >= 0) {
        // No `row < scroll_row` skip here: a picture whose top rows scrolled
        // off is still (partially) visible and gets clipped below.
        auto a = assets_[(size_t)l.img];
        if (a->pending) {
          auto real = resolve_asset(*a);
          if (!real) continue;
          assets_[(size_t)l.img] = real;
          a = real;
        }
        if (getenv("MDT_DEBUG_SVG"))
          fprintf(stderr, "[mdt] line image row=%d sy=%d px=%dx%d cols=%d rows=%d pending=%d src=%.50s\n",
                  row, sy, a->px_w, a->px_h, a->cols, a->rows, a->pending ? 1 : 0,
                  a->source.c_str());
        // A block image may hang over either edge of the viewport.  The
        // visible part is sent as its own (cached, grid-aligned) bitmap, so
        // the picture is cut by the window edge instead of disappearing or
        // being pinned to the top row.
        int clip = 0, vis = a->px_h;
        int sy2 = sy;
        if (sy < 0) {
          clip = -sy * chh;
          if (clip >= a->px_h) continue;      // wholly above the top
          vis -= clip;
          sy2 = 0;
        }
        int vis_rows = std::min(a->rows, h - sy2);
        if (vis_rows <= 0) continue;          // wholly below the fold
        if (vis_rows * chh < vis) vis = vis_rows * chh;
        if (clip || vis != a->px_h) a = sub_variant(*a, clip, vis);
        PlacedImage im;
        im.x = x0 + opt_.content_margin + std::max(0, (content_w_ - a->cols) / 2);
        im.y = sy2;
        im.cols = 0;  // native pixels
        im.rows = 0;
        im.px_w = a->px_w;
        im.px_h = a->px_h;
        im.png = &a->png;
        im.rgba = &a->rgba;
        if (term_ && term_->caps.gfx == GfxProto::Sixel) {
          im.cols = a->cols;
          im.rows = a->rows;
        }
        images_.push_back(im);
      }
      if (l.kind == Line::Text) {
        // Inline graphics start at the top of the line's reserved rows: the
        // bitmap canvas is baked so that its own ink sits where the text
        // baseline wants it (round-11 bake), and the layout reserves as many
        // rows as the bitmap spans (Line::rows), so nothing is drawn over.
        for (const Run& r : l.runs) {
          if (r.img < 0 || r.img >= (int)assets_.size()) continue;
          auto a = assets_[(size_t)r.img];
          if (a->pending) {
            auto real = resolve_asset(*a);
            if (!real) continue;
            assets_[(size_t)r.img] = real;
            a = real;
          }
          // The canvas top IS the placement boundary: one row for ordinary
          // inline formulas, as many rows as the bitmap spans for tall ones
          // (aligned blocks, matrices, inline pictures), which the layout
          // reserved for this line.
          int grow = row;
          int sub = 0;
          int screen_y = y0 + grow - scroll_row;  // top cell of the image
          // Whether the bitmap is visible is decided in PIXELS: a one-cell
          // image whose anchor is the row above the viewport is still almost
          // completely on screen (the old cell-based test threw it away and
          // drew a placeholder box instead).
          if (getenv("MDT_DEBUG_SVG"))
            fprintf(stderr, "[mdt] place img row=%d x=%d y0=%d grow=%d sub=%d rows=%d px=%dx%d\n",
                    row, x0 + r.x, y0, grow, sub, a->rows, a->px_w, a->px_h);
          int top_px_screen = screen_y * chh + sub;
          if (top_px_screen >= h * chh) continue;                    // below the fold
          if (top_px_screen + a->px_h <= 0) {                        // above the top
            scr.put_str(x0 + r.x, sy, "\u25a1", theme_.muted, theme_.bg);
            continue;
          }
          if (top_px_screen < 0) {
            // cut off what the viewport top hides and show the rest
            int clip = -top_px_screen;
            a = sub_variant(*a, clip, a->px_h - clip);
            screen_y = 0;
            sub = 0;
          }
          int vis_rows = std::min(a->rows, h - screen_y);            // cut at the bottom
          if (vis_rows <= 0) continue;
          if (vis_rows * chh < a->px_h) a = sub_variant(*a, 0, vis_rows * chh);
          PlacedImage im;
          im.x = x0 + r.x;
          im.y = screen_y;
          im.sub_y = sub;
          im.px_w = a->px_w;
          im.px_h = a->px_h;
          im.png = &a->png;
          im.rgba = &a->rgba;
          // The bitmap is exactly cols x rows cells big (see asset_for_math), so
          // telling the terminal how many cells it spans makes it draw the
          // image 1:1 instead of rescaling it (which is what made formulas look
          // soft - a formula used to be squeezed into a single cell).
          im.cols = a->cols;
          im.rows = a->rows;
          im.sub_x = 0;
          im.sub_y = sub;
          images_.push_back(im);
        }
      }
    }
  }
}

}  // namespace mdt
