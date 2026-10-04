// render.cpp : layout + drawing for DocView.
#include "render.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace mdt {

// ============================================================ highlighting ==
namespace {
struct LangDef {
  const char* ext;
  const char* keywords;
  bool hash_comment;
  bool slash_comment;
  bool backtick_string;
};
const LangDef kLangs[] = {
  {"c",   "auto break case char const continue default do double else enum extern float for goto if inline int long register restrict return short signed sizeof static struct switch typedef union unsigned void volatile while class namespace template typename public private protected virtual override new delete nullptr using try catch throw constexpr bool true false", true, true, false},
  {"cpp", "auto break case char const continue default do double else enum extern float for goto if inline int long register return short signed sizeof static struct switch typedef union unsigned void volatile while class namespace template typename public private protected virtual override new delete nullptr using try catch throw constexpr bool true false std string vector map set unique_ptr shared_ptr const auto", true, true, false},
  {"py",  "and as assert async await break class continue def del elif else except finally for from global if import in is lambda nonlocal not or pass raise return try while with yield None True False self", true, false, false},
  {"js",  "var let const function return if else for while do break continue new delete typeof instanceof class extends super this null undefined true false try catch finally throw async await import export from default of in", false, true, true},
  {"rust","fn let mut const struct enum impl trait pub use mod match if else while for loop return break continue unsafe where async await move ref self Self true false as dyn box", false, true, false},
  {"go",  "func var const type struct interface map chan package import return if else for range go defer select switch case default break continue nil true false", false, true, true},
  {"sh",  "if then else elif fi for while do done case esac function return export local echo cd exit set unset source alias", true, false, false},
  {"json","true false null", false, false, false},
};
const LangDef* lang_for(const std::string& lang) {
  std::string l = to_lower(lang);
  if (l.empty()) return nullptr;
  if (l == "c++" || l == "cc" || l == "cxx" || l == "hpp" || l == "h++" ) l = "cpp";
  if (l == "c" || l == "h") l = "c";
  if (l == "python" || l == "py3") l = "py";
  if (l == "javascript" || l == "ts" || l == "typescript" || l == "jsx" || l == "tsx") l = "js";
  if (l == "rs") l = "rust";
  if (l == "golang") l = "go";
  if (l == "bash" || l == "zsh" || l == "console" || l == "shell") l = "sh";
  for (auto& d : kLangs) if (l == d.ext) return &d;
  return nullptr;
}
bool is_word_char(char c) { return isalnum((unsigned char)c) || c == '_'; }
}  // namespace

CodeHighlight highlight_code(const std::string& code, const std::string& lang, const Theme& th) {
  CodeHighlight out;
  const LangDef* def = lang_for(lang);
  if (!def) return out;
  RGB kw{197, 134, 192};      // keywords
  RGB str{152, 195, 121};     // strings
  RGB com{128, 134, 148};     // comments
  RGB num{209, 154, 102};     // numbers
  RGB typ{86, 182, 194};      // types / builtins
  auto lines = split_lines(code);
  out.colors.resize(lines.size());
  bool in_block_comment = false;
  for (size_t li = 0; li < lines.size(); li++) {
    const std::string& l = lines[li];
    auto& row = out.colors[li];
    row.assign(l.size(), RGB{0, 0, 0});
    size_t i = 0;
    bool in_string = false, in_char = false;
    while (i < l.size()) {
      char c = l[i];
      if (in_block_comment) {
        row[i] = com;
        if (c == '*' && i + 1 < l.size() && l[i + 1] == '/') { row[i + 1] = com; i += 2; in_block_comment = false; continue; }
        i++;
        continue;
      }
      if (in_string || in_char) {
        row[i] = str;
        out.any = true;
        if (c == '\\' && i + 1 < l.size()) { row[i + 1] = str; i += 2; continue; }
        if ((in_string && c == '"') || (in_char && c == '\'')) { in_string = in_char = false; }
        i++;
        continue;
      }
      if (def->slash_comment && c == '/' && i + 1 < l.size() && l[i + 1] == '/') {
        for (size_t k = i; k < l.size(); k++) row[k] = com;
        out.any = true;
        break;
      }
      if (def->slash_comment && c == '/' && i + 1 < l.size() && l[i + 1] == '*') {
        in_block_comment = true;
        row[i] = row[i + 1] = com;
        i += 2;
        continue;
      }
      if (def->hash_comment && c == '#') {
        for (size_t k = i; k < l.size(); k++) row[k] = com;
        out.any = true;
        break;
      }
      if (c == '"' || c == '\'' || (def->backtick_string && c == '`')) {
        in_string = (c == '"' || c == '`');
        in_char = (c == '\'');
        row[i] = str;
        out.any = true;
        i++;
        continue;
      }
      if (isdigit((unsigned char)c) && (i == 0 || !is_word_char(l[i - 1]))) {
        size_t k = i;
        while (k < l.size() && (isalnum((unsigned char)l[k]) || l[k] == '.' || l[k] == 'x' || l[k] == '_')) k++;
        for (size_t j = i; j < k; j++) row[j] = num;
        out.any = true;
        i = k;
        continue;
      }
      if (isalpha((unsigned char)c) || c == '_') {
        size_t k = i;
        while (k < l.size() && is_word_char(l[k])) k++;
        std::string word = l.substr(i, k - i);
        std::string kwlist = def->keywords;
        bool is_kw = false;
        size_t p = 0;
        while (p < kwlist.size()) {
          size_t e = kwlist.find(' ', p);
          std::string kwn = kwlist.substr(p, e == std::string::npos ? std::string::npos : e - p);
          if (!kwn.empty() && kwn == word) { is_kw = true; break; }
          if (e == std::string::npos) break;
          p = e + 1;
        }
        if (is_kw) {
          for (size_t j = i; j < k; j++) row[j] = kw;
          out.any = true;
        } else if (!word.empty() && isupper((unsigned char)word[0]) && word.find('_') == std::string::npos) {
          for (size_t j = i; j < k; j++) row[j] = typ;
          out.any = true;
        }
        i = k;
        continue;
      }
      i++;
    }
  }
  (void)th;
  return out;
}

// ============================================================== DocView =====
int DocView::em_px() const {
  if (opt_.em_px_override > 0) return opt_.em_px_override;
  int ch = term_ ? term_->caps.px_cell_h() : 16;
  return std::max(8, (int)std::lround(ch * 0.85));
}
int DocView::cell_baseline_px() const {
  int ch = term_ ? term_->caps.px_cell_h() : 16;
  return (int)std::lround(ch * theme_.baseline);
}
int DocView::cell_w() const { return term_ ? term_->caps.px_cell_w() : 8; }
int DocView::cell_h() const { return term_ ? term_->caps.px_cell_h() : 16; }

void DocView::set_document(MdDocument doc, const std::string& path, const std::string& base_dir) {
  doc_ = std::move(doc);
  path_ = path;
  base_dir_ = base_dir;
  assets_.clear();
  asset_map_.clear();
  scroll_ = 0;
  relayout();
}

void DocView::set_width(int cols) {
  if (cols == cols_) return;
  cols_ = cols;
  relayout();
}

// ------------------------------------------------------------- assets -------
std::shared_ptr<DocView::ImageAsset> DocView::make_canvas(Image& img, int cols, int rows,
                                                          int target_w_px, int target_h_px,
                                                          double baseline_px) {
  auto a = std::make_shared<ImageAsset>();
  int cw = std::max(1, cell_w()), chh = std::max(1, cell_h());
  int canvas_w = std::max(1, target_w_px);
  int canvas_h = std::max(1, target_h_px);
  bool pad = (canvas_w > img.w + 1 || canvas_h > img.h + 1);
  if (canvas_w == img.w && canvas_h == img.h) {
    a->rgba = img.rgba;
  } else {
    a->rgba.assign((size_t)canvas_w * canvas_h * 4, 0);
    int ox = pad ? (canvas_w - img.w) / 2 : 0;
    int oy = pad ? (canvas_h - img.h) / 2 : 0;
    // when not padding, scale the source to exactly the canvas size (keeps aspect
    // because the caller computed both dimensions from the same ratio)
    if (!pad && (img.w != canvas_w || img.h != canvas_h)) {
      Image sc = img.scaled(canvas_w, canvas_h);
      if (!sc.empty()) { a->rgba = sc.rgba; img = sc; }
    } else {
      for (int y = 0; y < img.h; y++) {
        if (y + oy < 0 || y + oy >= canvas_h) continue;
        memcpy(&a->rgba[((size_t)(y + oy) * canvas_w + std::max(0, ox)) * 4],
               &img.rgba[(size_t)y * img.w * 4],
               (size_t)std::min(img.w, canvas_w) * 4);
      }
    }
  }
  a->px_w = canvas_w;
  a->px_h = canvas_h;
  a->cols = cols;
  a->rows = rows;
  a->baseline_px = baseline_px;
  a->png = png_encode(a->rgba.data(), a->px_w, a->px_h);
  (void)cw; (void)chh;
  return a;
}

// Cheap TeX size estimate used before the real typesetting happens.
void DocView::estimate_math_size(const std::string& tex, bool display, int& w, int& h) {
  double em = em_px();
  double width = 0;
  for (size_t i = 0; i < tex.size();) {
    if (tex[i] == '\\') {
      size_t j = i + 1;
      while (j < tex.size() && isalpha((unsigned char)tex[j])) j++;
      std::string cmd = tex.substr(i, j - i);
      ((void)0);
      width += (cmd == "\\frac" || cmd == "\\dfrac" || cmd == "\\tfrac" || cmd == "\\sum" ||
                cmd == "\\int" || cmd == "\\prod" || cmd == "\\sqrt" || cmd == "\\lim" ||
                cmd == "\\begin")
                   ? 1.0 * em
                   : 0.75 * em;
      i = (j > i + 1) ? j : i + 2;
      continue;
    }
    char c = tex[i++];
    if (c == '^' || c == '_') width += 0.25 * em;
    else if (c == '{' || c == '}') continue;
    else if (c == ' ' || c == '&' || c == ',') width += 0.25 * em;
    else if (isupper((unsigned char)c)) width += 0.62 * em;
    else if (isalnum((unsigned char)c)) width += 0.5 * em;
    else width += 0.4 * em;
  }
  width *= 1.12;
  double height = display ? 2.6 * em : 1.05 * em;
  w = std::max(4, (int)std::lround(width));
  h = std::max(4, (int)std::lround(height));
}

std::shared_ptr<DocView::ImageAsset> DocView::asset_for_math(const std::string& tex, bool display,
                                                             int max_w_px, int max_h_px,
                                                             bool allow_pending, bool force) {
  std::string key = fmt("M|%d|%d|%d|%s", display ? 1 : 0, max_w_px, max_h_px, tex.c_str());
  if (opt_.text_math) {  // user asked for plain Unicode formulas
    auto a = std::make_shared<ImageAsset>();
    a->source = tex;
    a->failed = true;
    a->err = "text maths";
    asset_map_[key] = a;
    return a;
  }
  if (!force) {
    auto it = asset_map_.find(key);
    if (it != asset_map_.end()) return it->second;
  }
  if (lazy_metrics_ && allow_pending && math_) {
    MathMetrics probe;
    if (!math_->metrics_cached(tex, display, em_px(), &probe)) {
      // defer the real typesetting until the formula is actually visible
      auto p = std::make_shared<ImageAsset>();
      p->source = tex;
      p->pending = true;
      p->is_display = display;
      p->max_w = max_w_px;
      p->max_h = max_h_px;
      int ew = 0, eh = 0;
      estimate_math_size(tex, display, ew, eh);
      double scale = 1.0;
      if (ew > max_w_px) scale = (double)max_w_px / ew;
      if (eh * scale > max_h_px) scale = (double)max_h_px / eh;
      p->px_w = std::max(4, (int)std::lround(ew * scale));
      p->px_h = std::max(4, (int)std::lround(eh * scale));
      int cw = std::max(1, cell_w()), chh = std::max(1, cell_h());
      p->cols = std::max(1, (int)std::ceil((double)p->px_w / cw));
      p->rows = std::max(1, (int)std::ceil((double)p->px_h / chh));
      p->baseline_px = display ? 0 : em_px() * 0.75;
      asset_map_[key] = p;
      return p;
    }
  }
  auto a = std::make_shared<ImageAsset>();
  a->source = tex;
  if (!math_) {
    a->failed = true;
    a->err = "math disabled";
    asset_map_[key] = a;
    return a;
  }
  double em = em_px();
  MathMetrics m = math_->metrics(tex, display, em);
  if (!m.ok) {
    a->failed = true;
    a->err = "cannot typeset";
    asset_map_[key] = a;
    return a;
  }
  // fit box
  double scale = 1.0;
  if (m.px_w > max_w_px) scale = max_w_px / m.px_w;
  if (m.px_h * scale > max_h_px) scale = max_h_px / m.px_h;
  int w = std::max(2, (int)std::ceil(m.px_w * scale));
  int h = std::max(2, (int)std::ceil(m.px_h * scale));
  Image img;
  if (!math_->raster(tex, display, em * scale, w, h, theme_.math_fg, img)) {
    a->failed = true;
    a->err = "raster failed";
    asset_map_[key] = a;
    return a;
  }
  int cw = std::max(1, cell_w()), chh = std::max(1, cell_h());
  a->cols = std::max(1, (int)std::ceil((double)w / cw));
  a->rows = std::max(1, (int)std::ceil((double)h / chh));
  a->baseline_px = m.baseline_px * scale;
  a->rgba = img.rgba;
  a->px_w = w;
  a->px_h = h;
  a->png = png_encode(a->rgba.data(), w, h);
  a->natural_w = m.px_w;
  a->natural_h = m.px_h;
  if (a->png.empty()) a->failed = true;
  asset_map_[key] = a;
  return a;
}

std::shared_ptr<DocView::ImageAsset> DocView::asset_for_image(const std::string& src, int max_w_px,
                                                              int max_h_px, bool inline_mode) {
  std::string key = fmt("I|%d|%d|%d|%s", inline_mode ? 1 : 0, max_w_px, max_h_px, src.c_str());
  auto it = asset_map_.find(key);
  if (it != asset_map_.end()) return it->second;
  auto a = std::make_shared<ImageAsset>();
  a->source = src;
  Image img;
  std::string err;
  std::string lower = to_lower(src);
  bool svg = ends_with(lower, ".svg") || (is_remote_url(src) == false && file_exists(src) == false && false);
  std::string local = src;
  if (!is_remote_url(local) && !local.empty() && local[0] != '/' && local[0] != '~') {
    local = base_dir_.empty() ? local : base_dir_ + "/" + local;
  }
  if (ends_with(lower, ".svg") || svg) {
    std::string xml;
    if (is_remote_url(src)) {
      std::string data;
      if (!http_get(src, data, &err)) { a->failed = true; a->err = err; asset_map_[key] = a; return a; }
      xml = data;
    } else if (!read_file(local, xml)) {
      a->failed = true;
      a->err = "cannot read " + local;
      asset_map_[key] = a;
      return a;
    }
    SvgImage doc;
    if (!svg_parse(xml, doc, &err)) {
      a->failed = true;
      a->err = err;
      asset_map_[key] = a;
      return a;
    }
    double tw = std::min((double)max_w_px, (double)max_h_px * 4);
    if (!svg_rasterize(doc, tw, theme_.fg, img)) {
      a->failed = true;
      a->err = "svg raster failed";
      asset_map_[key] = a;
      return a;
    }
  } else {
    if (!image_load_source(src, img, &err, &base_dir_)) {
      a->failed = true;
      a->err = err.empty() ? "cannot load image" : err;
      asset_map_[key] = a;
      return a;
    }
  }
  a->natural_w = img.w;
  a->natural_h = img.h;
  double fit = 1.0;
  if (img.w > max_w_px) fit = (double)max_w_px / img.w;
  if (img.h * fit > max_h_px) fit = (double)max_h_px / img.h;
  int fit_w = std::max(1, (int)std::lround(img.w * fit));
  int fit_h = std::max(1, (int)std::lround(img.h * fit));
  Image scaled = img.scaled(fit_w, fit_h);
  if (scaled.empty()) { a->failed = true; a->err = "scale failed"; asset_map_[key] = a; return a; }
  int cw = std::max(1, cell_w()), chh = std::max(1, cell_h());
  int cols = std::max(1, (int)std::ceil((double)fit_w / cw));
  int rows = std::max(1, (int)std::ceil((double)fit_h / chh));
  // cell aligned canvas (keeps placement simple and avoids overlaps)
  int canvas_w = inline_mode ? fit_w : cols * cw;
  int canvas_h = inline_mode ? fit_h : rows * chh;
  auto canvas = make_canvas(scaled, cols, rows, canvas_w, canvas_h, 0);
  canvas->source = src;
  canvas->natural_w = img.w;
  canvas->natural_h = img.h;
  canvas->err = a->err;
  canvas->failed = a->failed;
  asset_map_[key] = canvas;
  return canvas;
}

// -------------------------------------------------------------- layout ------
void DocView::relayout() {
  layout_dirty_ = false;  // the flag only means "layout may be stale"
  assets_.clear();
  asset_map_.clear();
  layout_.clear();
  images_.clear();
  layout_blocks();
  int maxr = std::max(0, total_rows_ - std::max(1, view_rows_));
  scroll_ = std::min(scroll_, maxr);
  if (scroll_ < 0) scroll_ = 0;
}


// ------------------------------------------------------------------ tables --
// One table = one frame: a top border, the rows, a rule under the header and
// a bottom border.  Nested tables (inside list items or quotes) only differ
// in the indent they start at.
void DocView::layout_table(const Block& b, int indent, int avail_w, BlockLayout& bl, int& row) {

    int ncols = 0;
    for (auto& r : b.rows) ncols = std::max(ncols, (int)r.size());
    if (ncols == 0) { row += 1; return; }
    std::vector<int> natural(ncols, 0), minw(ncols, 3);
    for (auto& r : b.rows)
      for (int c = 0; c < (int)r.size(); c++) {
        std::string txt = spans_plain_text(r[(size_t)c].spans);
        natural[c] = std::max(natural[c], str_width(txt));
        // longest word
        int cur = 0;
        for (char ch : txt) {
          if (ch == ' ') { minw[c] = std::max(minw[c], cur); cur = 0; }
          else cur++;
        }
        minw[c] = std::max(minw[c], cur);
      }
    int avail = avail_w - 2 - (ncols - 1) * 3 - 2;  // outer frame + separators
    int total = 0;
    for (int c = 0; c < ncols; c++) { natural[c] += 2; total += natural[c]; }
    std::vector<int> w = natural;
    while (total > avail) {
      int widest = 0;
      for (int c = 1; c < ncols; c++) if (w[c] > w[widest]) widest = c;
      if (w[widest] <= minw[widest] + 2) break;
      w[widest]--;
      total--;
    }
    auto border_line = [&](int idx) {  // a full-width border line of the box
      Line l;
      l.kind = Line::Table;
      l.row = row - bl.row;
      l.code_index = idx;  // 0 = top, 2 = bottom, 3 = rule under the header
      l.x = indent;        // blocks nested in a list item start further right
      int x = 0;
      for (int c = 0; c < ncols; c++) { TCell tc; tc.x = x; tc.width = w[c]; x += w[c] + 1; l.cells.push_back(tc); }
      bl.lines.push_back(l);
      row++;
    };
    // ONE box around all rows (a border per row made each row its own box)
    border_line(0);
    for (auto& r : b.rows) {
      std::vector<std::vector<std::string>> cell_lines(r.size());
      int maxlines = 1;
      for (int c = 0; c < (int)r.size(); c++) {
        std::string txt = spans_plain_text(r[(size_t)c].spans);
        int inner = std::max(2, w[c] - 2);
        std::vector<std::string> cl;
        std::string cur;
        int curw = 0;
        for (size_t i = 0; i < txt.size();) {
          size_t save = i;
          uint32_t cp = utf8_next(txt, i);
          int cwid = cp_width(cp);
          std::string piece = txt.substr(save, i - save);
          if (cp == ' ' && curw > 0) {
            // break opportunity
          }
          if (curw + cwid > inner && curw > 0) {
            cl.push_back(rtrim(cur));
            cur.clear();
            curw = 0;
            if (piece == " ") continue;
          }
          cur += piece;
          curw += cwid;
        }
        cl.push_back(rtrim(cur));
        cell_lines[c] = cl;
        maxlines = std::max(maxlines, (int)cl.size());
      }
      for (int lno = 0; lno < maxlines; lno++) {
        Line l;
        l.kind = Line::Table;
        l.row = row - bl.row;
        l.code_index = 1;  // 1 = content row
        l.x = indent;
        int x = 0;
        for (int c = 0; c < (int)r.size(); c++) {
          TCell tc;
          tc.x = x;
          tc.width = w[c];
          tc.align = r[(size_t)c].align;
          tc.header = r[(size_t)c].header;
          tc.text = lno < (int)cell_lines[c].size() ? cell_lines[c][lno] : "";
          l.cells.push_back(tc);
          x += w[c] + 1;
        }
        bl.lines.push_back(l);
        row++;
      }
      if (!r.empty() && r[0].header) border_line(3);
    }
    border_line(2);
    row += opt_.paragraph_gap;
  
}

void DocView::layout_blocks() {
  content_w_ = std::max(20, cols_ - 2 * opt_.content_margin);
  int row = 0;
  int cw = std::max(1, cell_w()), chh = std::max(1, cell_h());
  int max_img_rows = std::max(3, view_rows_ * opt_.max_image_rows_pct / 100);

  for (size_t bi = 0; bi < doc_.blocks.size(); bi++) {
    const Block& b = doc_.blocks[bi];
    BlockLayout bl;
    bl.block = (int)bi;
    bl.row = row;

    switch (b.type) {
      case Block::Heading: {
        Span base;
        base.bold = true;
        base.has_color = true;
        base.color = theme_.heading[std::min(6, std::max(1, b.level))];
        int indent = (b.level == 1) ? 0 : (b.level - 1);
        row += (bi == 0 ? 0 : 1);
        if (b.level <= 2) {
          // headings 1-2 get a rule underneath
          std::vector<Line> lines;
          build_lines(b.spans, 1, content_w_ - 1, base, lines);
          for (auto& l : lines) { l.row = row - bl.row; bl.lines.push_back(l); row++; }
          Line rule;
          rule.kind = Line::Rule;
          rule.row = row - bl.row;
          bl.lines.push_back(rule);
          row++;
          row += (b.level == 1 ? 1 : 0);
        } else {
          std::vector<Line> lines;
          build_lines(b.spans, indent + 1, content_w_ - indent - 1, base, lines);
          for (auto& l : lines) { l.row = row - bl.row; bl.lines.push_back(l); row++; }
        }
        break;
      }
      case Block::Paragraph: {
        // a paragraph that only holds images or display maths becomes an image row
        std::vector<const Span*> meaningful;
        for (auto& sp : b.spans) {
          if (sp.kind == Span::Text && trim(sp.text).empty()) continue;
          meaningful.push_back(&sp);
        }
        bool img_only = !meaningful.empty();
        for (auto* sp : meaningful) if (sp->kind != Span::Image) img_only = false;
        bool math_only = meaningful.size() == 1 && meaningful[0]->kind == Span::Math &&
                         meaningful[0]->display_math;
        if (math_only) {
          int maxw = content_w_ * cw - 8;
          int maxh = max_img_rows * chh;
          auto a = asset_for_math(meaningful[0]->tex, true, maxw, maxh);
          Line l;
          l.kind = Line::Image;
          l.row = 0;
          if (!a->failed) {
            l.img = (int)assets_.size();
            assets_.push_back(a);
            l.img_rows = a->rows;
          } else {
            l.kind = Line::Text;
          }
          bl.lines.push_back(l);
          int rows_needed = (!a->failed ? a->rows : 1);
          row += rows_needed;
          if (!a->failed) row += 0;
        } else if (img_only) {
          for (auto* sp : meaningful) {
            if (sp->kind != Span::Image) continue;
            int maxw = content_w_ * cw - 8;
            int maxh = max_img_rows * chh;
            auto a = asset_for_image(sp->link, maxw, maxh, false);
            Line l;
            l.row = 0;
            if (!a->failed) {
              l.kind = Line::Image;
              l.img = (int)assets_.size();
              assets_.push_back(a);
              l.img_rows = a->rows;
              row += a->rows + 1;
            } else {
              l.kind = Line::Text;
              row += 1;
            }
            bl.lines.push_back(l);
          }
        } else {
          std::vector<Line> lines;
          build_lines(b.spans, 1, content_w_ - 1, Span{}, lines);
          for (auto& l : lines) { l.row = row - bl.row; bl.lines.push_back(l); row++; }
        }
        row += opt_.paragraph_gap;
        break;
      }
      case Block::CodeBlock: {
        std::vector<std::string> clines = split_lines(b.code);
        if (clines.empty()) clines.push_back("");
        Line top;
        top.kind = Line::Code;
        top.row = 0;
        top.code_index = -1;
        bl.lines.push_back(top);
        row++;
        for (size_t k = 0; k < clines.size(); k++) {
          Line l;
          l.kind = Line::Code;
          l.code_index = (int)k;
          l.row = row - bl.row;
          bl.lines.push_back(l);
          row++;
        }
        Line bottom;
        bottom.kind = Line::Code;
        bottom.row = row - bl.row;
        bottom.code_index = -2;
        bl.lines.push_back(bottom);
        row++;
        row += opt_.paragraph_gap;
        break;
      }
      case Block::Quote: {
        int inner_indent = 3;
        int start_row = row;
        std::vector<BlockLayout> subs;
        for (auto& sublist : b.items) {
          for (const Block& sb : sublist) {
            // lay out the sub-block with an indent by re-using layout_blocks logic
            BlockLayout inner;
            inner.block = (int)bi;
            inner.row = row;
            // simple approach: paragraphs/headings/lists inside quotes
            std::vector<Line> lines;
            const Block& sb2 = sb;
            switch (sb2.type) {
              case Block::Heading: {
                Span base;
                base.bold = true;
                base.has_color = true;
                base.color = theme_.heading[std::min(6, std::max(1, sb2.level))];
                build_lines(sb2.spans, inner_indent, content_w_ - inner_indent, base, lines);
                break;
              }
              case Block::Paragraph:
              case Block::Html: {
                Span base;
                base.has_color = true;
                base.color = theme_.quote_fg;
                build_lines(sb2.spans.empty() ? std::vector<Span>{} : sb2.spans, inner_indent,
                            content_w_ - inner_indent, base, lines);
                break;
              }
              case Block::CodeBlock: {
                Span base;
                base.has_color = true;
                base.color = theme_.code_fg;
                for (auto& cl : split_lines(sb2.code)) {
                  Line l;
                  l.kind = Line::Code;
                  l.bar = inner_indent - 2;
                  Run r;
                  r.text = cl;
                  r.x = inner_indent;
                  r.width = str_width(cl);
                  r.style = base;
                  l.runs.push_back(r);
                  lines.push_back(l);
                }
                break;
              }
              case Block::Hr: {
                Line l;
                l.kind = Line::Rule;
                l.bar = inner_indent - 2;
                lines.push_back(l);
                break;
              }
              default: {
                Line l;
                l.kind = Line::Text;
                lines.push_back(l);
                break;
              }
            }
            for (auto& l : lines) {
              l.bar = inner_indent - 2;
              l.row = row - bl.row;
              bl.lines.push_back(l);
              row++;
            }
            inner.rows = row - inner.row;
            subs.push_back(inner);
          }
          row += 0;
        }
        if (row == start_row) {
          Line l;
          l.row = 0;
          bl.lines.push_back(l);
          row++;
        }
        (void)subs;
        row += opt_.paragraph_gap;
        break;
      }
      case Block::List: {
        int li = 0;
        for (size_t k = 0; k < b.items.size(); k++) {
          bool task = k < b.item_is_task.size() && b.item_is_task[k];
          bool checked = k < b.item_checked.size() && b.item_checked[k];
          std::string marker;
          RGB marker_color = theme_.bullet;
          if (task) {
            marker = checked ? "\u2611 " : "\u2610 ";
            marker_color = checked ? RGB{140, 195, 140} : theme_.muted;
          } else if (b.ordered) {
            marker = std::to_string(b.start_num + li) + ". ";
          } else {
            marker = "\u2022 ";
          }
          int marker_w = str_width(marker);
          int item_indent = 2 + marker_w;
          bool first_line_done = false;
          const std::vector<Block>& sub = b.items[k];
          if (sub.empty()) {
            Line l;
            l.row = row - bl.row;
            Run r;
            r.text = marker;
            r.x = 2;
            r.width = marker_w;
            r.style.has_color = true;
            r.style.color = marker_color;
            l.runs.push_back(r);
            bl.lines.push_back(l);
            row++;
          }
          for (const Block& sb : sub) {
            std::vector<Line> lines;
            Span base;
            switch (sb.type) {
              case Block::Heading: {
                base.bold = true;
                base.has_color = true;
                base.color = theme_.heading[std::min(6, std::max(1, sb.level))];
                build_lines(sb.spans, item_indent, content_w_ - item_indent, base, lines);
                break;
              }
              case Block::Paragraph:
                build_lines(sb.spans, item_indent, content_w_ - item_indent, Span{}, lines);
                break;
              case Block::CodeBlock: {
                Span cb;
                cb.has_color = true;
                cb.color = theme_.code_fg;
                for (auto& cl : split_lines(sb.code)) {
                  Line l;
                  l.kind = Line::Code;
                  Run r;
                  r.text = cl;
                  r.x = item_indent;
                  r.width = str_width(cl);
                  r.style = cb;
                  l.runs.push_back(r);
                  lines.push_back(l);
                }
                break;
              }
              case Block::List: {
                // nested list: render markers with extra indentation
                for (size_t kk = 0; kk < sb.items.size(); kk++) {
                  std::string m2 = sb.ordered ? std::to_string(sb.start_num + (int)kk) + ". " : "\u2022 ";
                  for (const Block& sbb : sb.items[kk]) {
                    std::vector<Line> l2;
                    if (sbb.type == Block::Paragraph)
                      build_lines(sbb.spans, item_indent + 2 + str_width(m2), content_w_ - item_indent - 2 - str_width(m2), Span{}, l2);
                    bool firstm = true;
                    for (auto& l : l2) {
                      if (firstm) {
                        Run r;
                        r.text = m2;
                        r.x = item_indent;
                        r.width = str_width(m2);
                        r.style.has_color = true;
                        r.style.color = marker_color;
                        l.runs.insert(l.runs.begin(), r);
                        firstm = false;
                      }
                      lines.push_back(l);
                    }
                  }
                }
                break;
              }
              case Block::Hr: {
                Line l;
                l.kind = Line::Rule;
                lines.push_back(l);
                break;
              }
              case Block::Quote: {
                // a quote inside a list item keeps its bar, just indented
                int qind = item_indent + 2;
                for (auto& sublist : sb.items) {
                  for (const Block& qb : sublist) {
                    Span qbase;
                    qbase.has_color = true;
                    qbase.color = theme_.quote_fg;
                    std::vector<Line> qlines;
                    if (qb.type == Block::Heading) {
                      qbase.bold = true;
                      qbase.color = theme_.heading[std::min(6, std::max(1, qb.level))];
                      build_lines(qb.spans, qind, content_w_ - qind, qbase, qlines);
                    } else if (qb.type == Block::Paragraph || qb.type == Block::Html) {
                      build_lines(qb.spans, qind, content_w_ - qind, qbase, qlines);
                    } else if (qb.type == Block::CodeBlock) {
                      Span cb;
                      cb.has_color = true;
                      cb.color = theme_.code_fg;
                      for (auto& cl : split_lines(qb.code)) {
                        Line l2;
                        l2.kind = Line::Code;
                        Run r;
                        r.text = cl;
                        r.x = qind;
                        r.width = str_width(cl);
                        r.style = cb;
                        l2.runs.push_back(r);
                        qlines.push_back(l2);
                      }
                    } else {
                      std::string t = !qb.spans.empty() ? spans_plain_text(qb.spans) : qb.code;
                      if (!t.empty()) {
                        std::vector<Span> sp;
                        Span s2;
                        s2.text = t;
                        s2.has_color = true;
                        s2.color = theme_.quote_fg;
                        sp.push_back(s2);
                        build_lines(sp, qind, content_w_ - qind, qbase, qlines);
                      }
                    }
                    for (auto& l2 : qlines) {
                      l2.bar = item_indent;
                      lines.push_back(l2);
                    }
                  }
                }
                break;
              }
              case Block::Table: {
                // nested table: drawn by draw_table, so it needs its own lines
                if (!first_line_done) {
                  Line l;
                  Run r;
                  r.text = marker;
                  r.x = 2;
                  r.width = marker_w;
                  r.style.has_color = true;
                  r.style.color = marker_color;
                  l.runs.push_back(r);
                  l.row = row - bl.row;
                  bl.lines.push_back(l);
                  row++;
                  first_line_done = true;
                }
                layout_table(sb, item_indent, content_w_ - item_indent, bl, row);
                break;
              }
              default: {
                // Never drop content: anything else is shown as its plain text.
                std::string t = !sb.spans.empty() ? spans_plain_text(sb.spans) : sb.code;
                if (!t.empty()) {
                  std::vector<Span> sp;
                  Span s2;
                  s2.text = t;
                  build_lines(sp, item_indent, content_w_ - item_indent, Span{}, lines);
                }
                break;
              }
            }
            for (auto& l : lines) {
              if (!first_line_done) {
                Run r;
                r.text = marker;
                r.x = 2;
                r.width = marker_w;
                r.style.has_color = true;
                r.style.color = marker_color;
                l.runs.insert(l.runs.begin(), r);
                first_line_done = true;
              }
              l.row = row - bl.row;
              bl.lines.push_back(l);
              row++;
            }
            if (!first_line_done) {
              Line l;
              l.row = row - bl.row;
              Run r;
              r.text = marker;
              r.x = 2;
              r.width = marker_w;
              r.style.has_color = true;
              r.style.color = marker_color;
              l.runs.push_back(r);
              bl.lines.push_back(l);
              row++;
              first_line_done = true;
            }
          }
          bool tight = true;
          if (!tight) row++;
          li++;
        }
        row += opt_.paragraph_gap;
        break;
      }
      case Block::Table: {
        layout_table(b, 0, content_w_, bl, row);
        row += opt_.paragraph_gap;
        break;
      }

      case Block::Hr: {
        Line l;
        l.kind = Line::Rule;
        l.row = 0;
        bl.lines.push_back(l);
        row += 1;
        row += opt_.paragraph_gap;
        break;
      }
      case Block::MathBlock: {
        int maxw = content_w_ * cw - 8;
        int maxh = max_img_rows * chh;
        auto a = asset_for_math(b.code, true, maxw, maxh);
        Line l;
        l.kind = Line::Image;
        l.row = 0;
        if (!a->failed) {
          l.img = (int)assets_.size();
          assets_.push_back(a);
          l.img_rows = a->rows;
          row += a->rows;
        } else {
          std::vector<Span> sp;
          Span s2;
          s2.text = latex_to_unicode(b.code, true);
          s2.has_color = true;
          s2.color = theme_.math_fg;
          sp.push_back(s2);
          std::vector<Line> ml;
          build_lines(sp, 2, content_w_ - 4, Span{}, ml);
          for (auto& ml2 : ml) { ml2.row = row - bl.row; bl.lines.push_back(ml2); row++; }
          row += opt_.paragraph_gap;
          break;
        }
        bl.lines.push_back(l);
        row += opt_.paragraph_gap;
        break;
      }
      case Block::Html: {
        Span base;
        base.has_color = true;
        base.color = theme_.muted;
        base.dim = true;
        std::vector<Line> lines;
        build_lines(b.spans, 1, content_w_ - 1, base, lines);
        if (lines.empty()) {
          // raw html block: show a trimmed excerpt
          std::string txt = collapse_ws(b.code);
          if (txt.size() > 200) txt = txt.substr(0, 200) + " ...";
          std::vector<Span> sp;
          Span s;
          s.text = txt;
          sp.push_back(s);
          build_lines(sp, 1, content_w_ - 1, base, lines);
        }
        for (auto& l : lines) { l.row = row - bl.row; bl.lines.push_back(l); row++; }
        row += opt_.paragraph_gap;
        break;
      }
      default: break;
    }
    bl.rows = row - bl.row;
    if (bl.rows < 1) bl.rows = 1;
    layout_.push_back(bl);
  }
  total_rows_ = std::max(1, row + 1);
}

// Builds wrapped, styled lines from a list of spans.
void DocView::build_lines(const std::vector<Span>& spans, int indent, int width, const Span& base,
                          std::vector<Line>& out) {
  if (width <= 4) width = 4;
  int cw = std::max(1, cell_w()), chh = std::max(1, cell_h());
  struct Atom {
    std::string text;
    Span style;
    int w = 0;
    bool space = false;
    bool brk = false;     // line may break after this atom
    int img = -1;
    int px_w = 0, px_h = 0;
    double baseline = 0;
    bool is_math = false, is_display = false;
    std::string tex;
    std::string src;
    bool is_image = false;
  };
  std::vector<Atom> atoms;
  for (const Span& sp : spans) {
    Span style = base;
    if (sp.bold) style.bold = true;
    if (sp.italic) style.italic = true;
    if (sp.strike) style.strike = true;
    if (sp.underline || sp.is_link) style.underline = true;
    if (sp.kind == Span::Code) {
      style.has_color = true;
      style.color = theme_.code_fg;
    }
    if (sp.is_link && sp.kind != Span::Image) {
      style.has_color = true;
      style.color = theme_.link;
    }
    if (sp.has_color) { style.has_color = true; style.color = sp.color; }
    switch (sp.kind) {
      case Span::Text: {
        std::string t = sp.text;
        size_t i = 0;
        std::string cur;
        auto flush_word = [&]() {
          if (!cur.empty()) {
            Atom a;
            a.text = cur;
            a.style = style;
            a.w = str_width(cur);
            a.brk = true;
            atoms.push_back(a);
            cur.clear();
          }
        };
        while (i < t.size()) {
          size_t save = i;
          uint32_t cp = utf8_next(t, i);
          int cwid = cp_width(cp);
          if (cp == ' ' || cp == '\t' || cp == '\n') {
            flush_word();
            size_t j = i;
            while (j < t.size() && (t[j] == ' ' || t[j] == '\t' || t[j] == '\n')) j++;
            i = j;
            Atom a;
            a.text = " ";
            a.space = true;
            a.w = 1;
            a.brk = true;
            a.style = style;
            atoms.push_back(a);
            continue;
          }
          if (cwid == 2) {  // CJK: breakable between characters
            flush_word();
            Atom a;
            a.text = t.substr(save, i - save);
            a.style = style;
            a.w = 2;
            a.brk = true;
            atoms.push_back(a);
            continue;
          }
          cur += t.substr(save, i - save);
        }
        flush_word();
        break;
      }
      case Span::Code: {
        Atom a;
        a.text = " " + sp.text + " ";
        a.style = style;
        a.w = str_width(a.text);
        a.brk = true;
        atoms.push_back(a);
        break;
      }
      case Span::Math: {
        Atom a;
        a.is_math = true;
        a.tex = sp.tex;
        a.is_display = sp.display_math;
        a.style = style;
        a.brk = true;
        if (sp.display_math) {
          a.img = -2;  // display maths inside a paragraph: own line
          atoms.push_back(a);
          break;
        }
        int maxw = std::min((width)*cw, 40 * cw);
        int maxh = std::max(chh * 2, (int)(chh * 3.2));
        auto asset = asset_for_math(sp.tex, false, maxw, maxh);
        if (asset->failed) {
          a.text = " " + latex_to_unicode(sp.tex, false) + " ";
          a.w = str_width(a.text);
          a.style.has_color = true;
          a.style.color = theme_.math_fg;
          atoms.push_back(a);
        } else {
          a.img = (int)assets_.size();
          assets_.push_back(asset);
          a.px_w = asset->px_w;
          a.px_h = asset->px_h;
          a.baseline = asset->baseline_px;
          a.w = std::max(1, (int)std::ceil((double)asset->px_w / cw));
          atoms.push_back(a);
        }
        break;
      }
      case Span::Image: {
        if (!opt_.inline_images) {
          Atom a;
          a.text = "[" + (sp.text.empty() ? std::string("image") : sp.text) + "]";
          a.style.has_color = true;
          a.style.color = theme_.link;
          a.w = str_width(a.text);
          atoms.push_back(a);
          break;
        }
        Atom a;
        a.is_image = true;
        a.src = sp.link;
        a.style = style;
        a.brk = true;
        int maxh = chh * 2;
        int maxw = std::min(width * cw, 24 * cw);
        auto asset = asset_for_image(sp.link, maxw, maxh, true);
        if (asset->failed) {
          a.text = "[" + (sp.text.empty() ? asset->err : sp.text) + "]";
          a.style.has_color = true;
          a.style.color = theme_.muted;
          a.w = str_width(a.text);
          atoms.push_back(a);
        } else {
          a.img = (int)assets_.size();
          assets_.push_back(asset);
          a.px_w = asset->px_w;
          a.px_h = asset->px_h;
          a.w = std::max(1, (int)std::ceil((double)asset->px_w / cw));
          atoms.push_back(a);
        }
        break;
      }
      case Span::LineBreak: {
        Atom a;
        a.text = "";
        a.w = 0;
        a.brk = false;
        a.style = style;
        // hard break marker
        a.text = "\n";
        atoms.push_back(a);
        break;
      }
      default: break;
    }
  }
  // greedy wrap
  std::vector<Line> lines;
  Line cur;
  int x = 0;
  auto start_line = [&]() {
    cur.runs.clear();
    x = 0;
  };
  size_t i = 0;
  auto emit_text = [&](const Atom& a, bool as_space) {
    std::string t = as_space ? "" : a.text;
    if (t.empty() && !as_space && a.img < 0) return;
    if (!as_space) {
      Run r;
      r.text = t;
      r.style = a.style;
      r.x = x;
      r.width = a.w;
      if (a.img >= 0) {
        r.img = a.img;
        r.rows = std::max(1, (int)std::ceil((double)a.px_h / chh));
        r.width = a.w;
      }
      if (!cur.runs.empty() && cur.runs.back().img < 0 && r.img < 0 &&
          cur.runs.back().style.color == r.style.color &&
          cur.runs.back().style.bold == r.style.bold &&
          cur.runs.back().style.italic == r.style.italic &&
          cur.runs.back().style.underline == r.style.underline &&
          cur.runs.back().style.strike == r.style.strike &&
          cur.runs.back().style.has_color == r.style.has_color &&
          cur.runs.back().style.kind == r.style.kind) {
        cur.runs.back().text += r.text;
        cur.runs.back().width += r.width;
      } else {
        cur.runs.push_back(r);
      }
      x += a.w;
    } else {
      Run r;
      r.text = " ";
      r.style = a.style;
      r.x = x;
      r.width = 1;
      if (!cur.runs.empty() && cur.runs.back().img < 0 && cur.runs.back().style.has_color == r.style.has_color &&
          cur.runs.back().style.color == r.style.color) {
        cur.runs.back().text += " ";
        cur.runs.back().width += 1;
      } else {
        cur.runs.push_back(r);
      }
      x += 1;
    }
  };
  auto trim_line = [&]() {
    if (!cur.runs.empty() && cur.runs.back().img < 0) {
      while (!cur.runs.empty()) {
        Run& r = cur.runs.back();
        std::string t = rtrim(r.text);
        int removed = r.width - str_width(t);
        if (removed > 0) { r.text = t; r.width -= removed; }
        if (r.text.empty()) cur.runs.pop_back();
        else break;
      }
    }
  };
  while (i < atoms.size()) {
    const Atom& a = atoms[i];
    if (a.text == "\n" && a.img < 0) {  // hard break
      trim_line();
      cur.row = (int)lines.size();
      lines.push_back(cur);
      start_line();
      i++;
      continue;
    }
    if (a.img == -2) {  // display maths inside a paragraph: its own centred line
      trim_line();
      if (!cur.runs.empty()) {
        cur.row = (int)lines.size();
        lines.push_back(cur);
        start_line();
      }
      Line l;
      l.kind = Line::Image;
      int maxw = content_w_ * cw - 8;
      int maxh = std::max(chh * 3, view_rows_ * opt_.max_image_rows_pct / 100 * chh);
      auto asset = asset_for_math(a.tex, true, maxw, maxh);
      if (!asset->failed) {
        l.img = (int)assets_.size();
        assets_.push_back(asset);
        l.img_rows = asset->rows;
      } else {
        std::vector<Span> sp;
        Span s2;
        s2.text = latex_to_unicode(a.tex, true);
        s2.has_color = true;
        s2.color = theme_.math_fg;
        sp.push_back(s2);
        std::vector<Line> ml;
        build_lines(sp, indent + 2, content_w_ - indent - 4, Span{}, ml);
        for (auto& ml2 : ml) { ml2.row = (int)lines.size(); lines.push_back(ml2); }
        start_line();
        i++;
        continue;
      }
      l.row = (int)lines.size();
      lines.push_back(l);
      start_line();
      i++;
      continue;
    }
    if (a.space) {
      if (cur.runs.empty()) { i++; continue; }  // no leading space
      emit_text(a, true);
      i++;
      continue;
    }
    if (x + a.w > width && x > 0) {
      // wrap
      trim_line();
      cur.row = (int)lines.size();
      lines.push_back(cur);
      start_line();
      continue;  // retry atom on the new line
    }
    emit_text(a, false);
    i++;
  }
  trim_line();
  if (!cur.runs.empty() || lines.empty()) {
    cur.row = (int)lines.size();
    lines.push_back(cur);
  }
  for (auto& l : lines) {
    l.row = 0;
    for (auto& r : l.runs) r.x += indent;
  }
  for (auto& l : lines) out.push_back(l);
}

}  // namespace mdt
