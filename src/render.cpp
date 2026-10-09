// render.cpp : layout + drawing for DocView.
#include "render.h"

#include "platform.h"

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
std::shared_ptr<DocView::ImageAsset> DocView::sub_variant(const ImageAsset& a, int clip_top,
                                                          int vis_h) {
  int chh = std::max(1, cell_h());
  if (clip_top < 0) clip_top = 0;
  if (clip_top >= a.px_h) clip_top = a.px_h - 1;
  if (vis_h <= 0 || vis_h > a.px_h - clip_top) vis_h = a.px_h - clip_top;
  vis_h = std::max(chh, (vis_h / chh) * chh);   // stay on the cell grid
  auto key = std::make_tuple(&a, clip_top, vis_h);
  auto it = clip_cache_.find(key);
  if (it != clip_cache_.end()) return it->second;
  auto ca = std::make_shared<ImageAsset>(a);
  ca->rgba.assign((size_t)a.px_w * vis_h * 4, 0);
  for (int y = 0; y < vis_h; y++)
    memcpy(&ca->rgba[(size_t)y * a.px_w * 4],
           &a.rgba[(size_t)(y + clip_top) * a.px_w * 4], (size_t)a.px_w * 4);
  ca->px_h = vis_h;
  ca->rows = vis_h / chh;
  ca->baseline_px = a.baseline_px - clip_top;
  ca->png = png_encode(ca->rgba.data(), ca->px_w, vis_h);
  clip_cache_[key] = ca;
  return ca;
}

std::shared_ptr<DocView::ImageAsset> DocView::make_canvas(Image& img, int cols, int rows,
                                                          int target_w_px, int target_h_px,
                                                          double baseline_px, int ink_y) {
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
    if (ink_y != -1000000) { oy = ink_y; pad = true; }   // caller bakes the offset
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
      p->px_w = p->cols * cw;   // same snap as the real asset (see below)
      p->px_h = p->rows * chh;
      // the placeholder has to sit on the same cell row as the real bitmap,
      // otherwise the layout jumps when the formula is finally typeset
      p->baseline_px = display ? 0 : cell_baseline_px();
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
    if (getenv("MDT_DEBUG_SVG")) fprintf(stderr, "[mdt] asset: no metrics for: %s\n", tex.c_str());
    asset_map_[key] = a;
    return a;
  }
  // fit box
  double scale = 1.0;
  if (m.px_w > max_w_px) scale = max_w_px / m.px_w;
  if (m.px_h * scale > max_h_px) scale = max_h_px / m.px_h;
  int ink_w = std::max(2, (int)std::ceil(m.px_w * scale));
  int ink_h = std::max(2, (int)std::ceil(m.px_h * scale));
  int cw = std::max(1, cell_w()), chh = std::max(1, cell_h());
  // Rasterise onto the exact cell grid: cols*cw by rows*chh pixels.  Anything
  // else makes the terminal rescale the bitmap to fit whole cells, which is
  // what makes formulas look soft.
  int w = std::max(cw, (int)std::ceil((double)ink_w / cw) * cw);
  int h = std::max(chh, (int)std::ceil((double)ink_h / chh) * chh);
  // An inline formula lives on a text row, and its bitmap is placed at a cell
  // boundary.  The sub-cell offset is baked into the bitmap instead of being
  // asked for in the placement: a terminal that cannot put an image at a pixel
  // offset inside a cell (iTerm2, sixel; and kitty's Y= does not do what one
  // might expect either) used to draw the formula a whole row too high, because
  // a baseline a fraction of a pixel above the text's anchored the bitmap on
  // the row above.  grid_ink_y() gives the drawing's row inside the canvas whose
  // top is the boundary the placement will use.
  int ink_y = -1000000;  // -1000000 = let raster_grid centre the drawing
  if (!display) {
    if (ink_h > chh) {
      // A multi-row formula (aligned, matrix, ...) gets its own rows in the
      // layout, so its ink starts at the top of the canvas instead of hanging
      // off a text baseline: the reserved block then hugs the drawing.
      ink_y = 0;
    } else {
      int cb = cell_baseline_px();
      double off = (double)cb - m.baseline_px * scale;  // ink top vs the text row top
      int rows_above = (int)std::floor(off / chh);
      ink_y = (int)std::lround(off - (double)rows_above * chh);
      if (ink_y < 0) { ink_y += chh; rows_above -= 1; }
      if (ink_y >= chh) { ink_y -= chh; rows_above += 1; }
    }
    h = std::max(chh, (int)std::ceil((double)(ink_y + ink_h) / chh) * chh);
  }
  Image img;
  int off_x = 0, off_y = 0;
  if (getenv("MDT_DEBUG_SVG"))
    fprintf(stderr, "[mdt] asset build src=%.40s display=%d em=%.1f ink=%.1fx%.1f max=%dx%d scale=%.3f -> %dx%d\n",
            tex.c_str(), display ? 1 : 0, em, m.px_w, m.px_h, max_w_px, max_h_px, scale, w, h);
  if (!math_->raster_grid(tex, display, em * scale, w, h, theme_.math_fg, img, &off_x, &off_y,
                          ink_y)) {
    a->failed = true;
    a->err = "raster failed";
    if (getenv("MDT_DEBUG_SVG"))
      fprintf(stderr, "[mdt] asset: raster failed (%dx%d px, scale %.2f, ink %.1fx%.1f) for: %s\n",
              w, h, scale, m.px_w, m.px_h, tex.c_str());
    asset_map_[key] = a;
    return a;
  }
  a->cols = w / cw;
  a->rows = h / chh;
  a->baseline_px = off_y + m.baseline_px * scale;
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
  // Pictures on the web are downloaded by a worker thread: a 题解 with three
  // images on a slow host used to block the event loop for the whole download,
  // which looked exactly like a dead keyboard.  Until the bytes are here the
  // picture is a short placeholder line, and the layout is refreshed once.
  bool remote_raster = is_remote_url(src) && !ends_with(lower, ".svg");
  RemoteImageLoader::Result loaded;
  if (remote_raster) {
    if (!opt_.remote_images) {  // no graphics, or a text dump: no network at all
      a->failed = true;
      a->err = "remote image";
      asset_map_[key] = a;
      return a;
    }
    loaded = loader_.get(src);
    if (getenv("MDT_DEBUG_IMAGE"))
      fprintf(stderr, "[mdt] image %s: cached=%d%s\n", src.c_str(), loaded ? 1 : 0,
              loaded ? (loaded->ok ? " ok" : (" failed: " + loaded->err).c_str())
                     : " (still downloading)");
    if (!loaded) {
      if (opt_.async_images) {
        if (getenv("MDT_DEBUG_IMAGE")) fprintf(stderr, "[mdt] image %s: queued\n", src.c_str());
        loader_.request(src);
        a->failed = true;      // drawn as "[loading…]" until the image arrives
        a->err = "loading…";
        asset_map_[key] = a;
        return a;
      }
    } else if (!loaded->ok) {
      a->failed = true;
      a->err = loaded->err;
      asset_map_[key] = a;
      return a;
    } else {
      img = loaded->img;       // already downloaded and decoded
    }
  }
  if (!remote_raster && (ends_with(lower, ".svg") || svg)) {
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
  } else if (!remote_raster || !loaded) {
    if (!image_load_source(src, img, &err, &base_dir_)) {
      if (getenv("MDT_DEBUG_IMAGE"))
        fprintf(stderr, "[mdt] image %s: download failed: %s\n", src.c_str(), err.c_str());
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
  double baseline = 0;  // a block picture hangs from the row it starts on
  int pic_y = -1000000;
  if (inline_mode) {
    // A picture inside a line of text sits on the text baseline, and its canvas
    // is grid-aligned the same way a formula's is (see asset_for_math).
    int cb = cell_baseline_px();
    double off = (double)cb - (double)fit_h;
    int rows_above = (int)std::floor(off / chh);
    pic_y = (int)std::lround(off - (double)rows_above * chh);
    if (pic_y < 0) { pic_y += chh; rows_above -= 1; }
    if (pic_y >= chh) { pic_y -= chh; rows_above += 1; }
    canvas_h = std::max(1, (int)std::ceil((double)(pic_y + fit_h) / chh)) * chh;
    rows = canvas_h / chh;
    baseline = pic_y + fit_h;
  }
  auto canvas = make_canvas(scaled, cols, rows, canvas_w, canvas_h, baseline, pic_y);
  canvas->source = src;
  canvas->natural_w = img.w;
  canvas->natural_h = img.h;
  canvas->err = a->err;
  canvas->failed = a->failed;
  asset_map_[key] = canvas;
  return canvas;
}

// -------------------------------------------------------------- layout ------
bool DocView::poll_images() {
  if (!loader_.take_changed()) return false;
  relayout();  // rebuild the picture rows; the image is cached in the loader
  return true;
}

void DocView::relayout() {
  layout_dirty_ = false;  // the flag only means "layout may be stale"
  clip_cache_.clear();    // the assets the cuts refer to are gone
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
// ------------------------------------------------- maths inside a table cell --
// The bitmap of a cell formula is exactly as wide as its Unicode transcription
// and one cell high, and the page colour is painted behind the ink.  Placed on
// the cell it covers the transcription completely, and because the bitmap is
// already the size of the cells it is drawn into, the terminal does not scale
// it (that is what keeps formulas sharp elsewhere too).
std::shared_ptr<DocView::ImageAsset> DocView::cell_math_asset(const TCell::Piece& pc, int cw, int chh,
                                                             RGB bg) {
  std::string key = fmt("CM|%d|%d|%d|%d|%06x|%s", cw, chh, pc.cols, pc.display ? 1 : 0,
                        (bg.r << 16) | (bg.g << 8) | bg.b, pc.tex.c_str());
  auto it = asset_map_.find(key);
  if (it != asset_map_.end()) return it->second;
  auto a = std::make_shared<ImageAsset>();
  a->source = pc.tex;
  asset_map_[key] = a;
  if (opt_.text_math || !math_) { a->failed = true; a->err = "no bitmap here"; return a; }
  double em = em_px();
  MathMetrics m = math_->metrics(pc.tex, false, em);
  if (!m.ok) { a->failed = true; a->err = "cannot typeset"; return a; }
  int w = std::max(2, pc.cols) * cw;
  int h = chh;
  int cb = cell_baseline_px();
  // scale the formula so it fits the width, the cell height, and leaves room
  // for the baseline: the ink has to sit inside one cell with its baseline on
  // the cell's text baseline, otherwise the bitmap cannot be placed on the
  // grid without hanging over its neighbours
  double scale = std::min(1.0, (double)w / std::max(1.0, m.px_w));
  if (m.px_h * scale > h - 2) scale = (h - 2) / std::max(1.0, m.px_h);
  if (m.baseline_px > 1 && m.baseline_px * scale > cb) scale = (double)cb / m.baseline_px;
  if (m.px_h * scale > h) scale = (double)h / std::max(1.0, m.px_h);
  Image img;
  int off_x = 0, off_y = 0;
  if (!math_->raster_grid(pc.tex, false, em * scale, w, h, theme_.math_fg, img, &off_x, &off_y)) {
    a->failed = true;
    a->err = "raster failed";
    return a;
  }
  // shift the ink so that its baseline lands exactly on the cell baseline
  int shift = (int)std::lround(cb - m.baseline_px * scale) - off_y;
  if (shift != 0) {
    std::vector<unsigned char> moved((size_t)w * h * 4, 0);
    for (int y = 0; y < h; y++) {
      int sy = y - shift;
      if (sy < 0 || sy >= h) continue;
      memcpy(&moved[(size_t)y * w * 4], &img.rgba[(size_t)sy * w * 4], (size_t)w * 4);
    }
    img.rgba.swap(moved);
  }
  // paint the cell background behind the ink, so anything underneath is covered
  // instead of showing through the transparent parts
  for (size_t i = 0; i + 3 < img.rgba.size(); i += 4) {
    double al = img.rgba[i + 3] / 255.0;
    for (int k = 0; k < 3; k++) {
      double fg = img.rgba[i + k];
      double bgc = k == 0 ? bg.r : (k == 1 ? bg.g : bg.b);
      img.rgba[i + k] = (uint8_t)std::lround(fg * al + bgc * (1 - al));
    }
    img.rgba[i + 3] = 255;
  }
  a->rgba = img.rgba;
  a->px_w = w;
  a->px_h = h;
  a->cols = std::max(2, pc.cols);
  a->rows = 1;
  a->baseline_px = cb;  // the ink was baked onto this baseline
  if (getenv("MDT_DEBUG_CELLMATH"))
    fprintf(stderr, "[mdt]   %s: px=%dx%d baseline=%.1f off_y=%d scale=%.3f -> base_in_box=%.1f\n",
            pc.tex.c_str(), (int)m.px_w, (int)m.px_h, m.baseline_px, off_y, scale, a->baseline_px);
  a->png = png_encode(a->rgba.data(), w, h);
  if (a->png.empty()) a->failed = true;
  if (getenv("MDT_DEBUG_CELLMATH")) {  // write the bitmap out for inspection
    static int n = 0;
    std::string path = fmt("/tmp/cellmath-%d.png", n++);
    if (FILE* f = plat::open_file(path, "wb")) {
      fwrite(a->png.data(), 1, a->png.size(), f);
      fclose(f);
      fprintf(stderr, "[mdt] cell maths %dx%d px -> %s (%s)\n", w, h, path.c_str(),
              pc.tex.c_str());
    }
  }
  return a;
}

// Places the cell's formulas as bitmaps on top of their transcription.
void DocView::place_cell_math(Screen* scr, int cell_x, int cell_w, int yy, const TCell& tc, int cw,
                              int chh, int scroll_row, RGB bg) {
  if (tc.pieces.empty()) return;
  if (opt_.text_math || !math_) return;
  if (!offscreen_ && term_ && !term_->caps.can_show_images()) return;
  if (yy - scroll_row < 0) return;  // this row is scrolled out
  int inner = std::max(2, cell_w - 2);
  for (const TCell::Piece& pc : tc.pieces) {
    if (pc.col + pc.cols > inner) continue;  // does not fit the column: text only
    auto a = cell_math_asset(pc, cw, chh, bg);
    if (a->failed || a->png.empty()) continue;
    cell_assets_.push_back(a);  // PlacedImage only holds pointers into it
    PlacedImage im;
    // cell_x is already the first column of the cell's content (the caller adds
    // its padding and alignment), so the piece only adds its own column.
    im.x = cell_x + pc.col;
    // The bitmap is one cell tall with the formula's baseline baked in at the
    // cell's own baseline (see cell_math_asset), so it is placed exactly on the
    // cell row: no sub-cell offset, nothing spilling into the row above or
    // below, and the transcription underneath is covered completely.
    im.y = yy - scroll_row;
    im.sub_y = 0;
    im.cols = a->cols;
    im.rows = 1;
    im.px_w = a->px_w;
    im.px_h = a->px_h;
    im.png = &a->png;
    im.rgba = &a->rgba;
    images_.push_back(im);
    // Terminals paint cell text above placed images, so the Unicode
    // transcription has to go from the text layer or it shows through/around
    // the bitmap.  Without graphics the transcription is what remains.
    if (scr) {
      for (int k = 0; k < pc.cols && pc.col + k < cell_w; k++)
        scr->put(im.x + k, im.y, ' ', theme_.fg, bg);
    }
  }
}

// Table cells are drawn as text inside a fixed grid, so a typeset formula has
// no place there.  Inline maths becomes the Unicode transcription instead of
// the raw TeX ("$\frac{a}{b}$" used to be shown verbatim).
std::string spans_cell_text(const std::vector<Span>& spans) {
  std::string out;
  for (auto& s : spans) {
    if (s.kind == Span::Math) out += latex_to_unicode(s.tex, s.display_math);
    else out += s.text;
  }
  return out;
}

// in the indent they start at.
void DocView::layout_table(const Block& b, int indent, int avail_w, BlockLayout& bl, int& row) {

    int ncols = 0;
    for (auto& r : b.rows) ncols = std::max(ncols, (int)r.size());
    if (ncols == 0) { row += 1; return; }
    std::vector<int> natural(ncols, 0), minw(ncols, 3);
    for (auto& r : b.rows)
      for (int c = 0; c < (int)r.size(); c++) {
        std::string txt = spans_cell_text(r[(size_t)c].spans);
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
    auto border_line = [&](int idx, int src_row = -1) {  // full-width border
      Line l;
      l.kind = Line::Table;
      l.row = row - bl.row;
      l.code_index = idx;  // 0 top, 2 bottom, 3 header rule, 4 body-row rule
      l.img = src_row;     // for 4: the source row above the rule
      l.x = indent;        // blocks nested in a list item start further right
      int x = 0;
      for (int c = 0; c < ncols; c++) { TCell tc; tc.x = x; tc.width = w[c]; x += w[c] + 1; l.cells.push_back(tc); }
      bl.lines.push_back(l);
      row++;
    };
    // ONE box around all rows (a border per row made each row its own box)
    border_line(0);
    size_t nrows = b.rows.size();
    std::vector<std::vector<std::vector<std::string>>> all_lines(nrows);
    std::vector<int> maxlines(nrows, 1);
    for (size_t ri = 0; ri < nrows; ri++) {
      auto& rr = b.rows[ri];
      all_lines[ri].assign(rr.size(), {});
      for (int c = 0; c < (int)rr.size(); c++) {
        std::string txt = spans_cell_text(rr[(size_t)c].spans);
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
        all_lines[ri][c] = cl;
        maxlines[ri] = std::max(maxlines[ri], (int)cl.size());
      }
    }
    // vertical merges: the origin's content sits in the middle of the display
    // rows the whole merged block occupies (vmap: slot -> origin row + line)
    std::vector<std::vector<std::vector<std::pair<int, int>>>> vmap(
        nrows, std::vector<std::vector<std::pair<int, int>>>());
    for (size_t ri = 0; ri < nrows; ri++)
      vmap[ri].assign(b.rows[ri].size(), std::vector<std::pair<int, int>>());
    for (int c = 0; c < ncols; c++) {
      size_t ri = 0;
      while (ri < nrows) {
        if (c >= (int)b.rows[ri].size() || b.rows[ri][(size_t)c].merge == 1) { ri++; continue; }
        size_t o = ri, re = ri + 1;
        while (re < nrows && c < (int)b.rows[re].size() && b.rows[re][(size_t)c].merge == 1) re++;
        int D = 0;
        for (size_t t = o; t < re; t++) D += maxlines[t];
        int dy = (D - maxlines[o]) / 2;
        if (dy > 0) {
          int d = 0;
          for (size_t t = o; t < re; t++) {
            for (int k = 0; k < maxlines[t]; k++) {
              int lno = d + k - dy;
              vmap[t][(size_t)c].resize((size_t)maxlines[t], {-2, -2});
              vmap[t][(size_t)c][(size_t)k] =
                  (lno >= 0 && lno < maxlines[o]) ? std::make_pair((int)o, lno)
                                                  : std::make_pair(-1, -1);
            }
            d += maxlines[t];
          }
        }
        ri = re;
      }
    }
    // "^" under a colspan group: what does the separator to its right do?
    // Walk up through the rows above until a concrete "<"/">" answers.
    auto above_open = [&](int rr, int c) {
      while (rr >= 0) {
        const auto& row = b.rows[(size_t)rr];
        if (c + 1 >= (int)row.size()) return false;
        if (row[(size_t)c + 1].merge == 2 || row[(size_t)c].merge == 3) return true;
        if (row[(size_t)c + 1].merge == 1 && row[(size_t)c].merge == 1) { rr--; continue; }
        return false;
      }
      return false;
    };
    for (size_t ri = 0; ri < nrows; ri++) {
      auto& r = b.rows[ri];
      int ml = maxlines[ri];
      for (int lno = 0; lno < ml; lno++) {
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
          tc.merge = r[(size_t)c].merge;
          if (tc.merge == 1 && c + 1 < (int)r.size() && r[(size_t)c + 1].merge == 1)
            tc.hinherit = above_open((int)ri - 1, c);
          // a vertically merged block draws the origin's content centred:
          // this slot may belong to another row of the span (or be empty)
          int orow = (int)ri, olno = lno;
          if (c < (int)vmap[ri].size() && lno < (int)vmap[ri][(size_t)c].size()) {
            auto vm = vmap[ri][(size_t)c][(size_t)lno];
            if (vm.first == -1) { orow = -1; }
            else if (vm.first >= 0) { orow = vm.first; olno = vm.second; }
          }
          const std::vector<Span>* sp_src = &r[(size_t)c].spans;
          if (orow == -1) {
            tc.text = "";
            sp_src = nullptr;
          } else if (orow != (int)ri) {
            tc.v_row = orow;
            tc.v_lno = olno;
            tc.text = olno < (int)all_lines[(size_t)orow][c].size()
                          ? all_lines[(size_t)orow][c][(size_t)olno]
                          : "";
            tc.align = b.rows[(size_t)orow][(size_t)c].align;
            tc.header = b.rows[(size_t)orow][(size_t)c].header;
            sp_src = &b.rows[(size_t)orow][(size_t)c].spans;
          } else {
            tc.text = lno < (int)all_lines[ri][c].size() ? all_lines[ri][c][(size_t)lno] : "";
          }
          // where the formulas sit inside that text, in columns
          int col = 0;
          bool fits = true;
          if (sp_src)
          for (const Span& sp : *sp_src) {
            if (sp.kind != Span::Math) { col += str_width(sp.text); continue; }
            std::string plain = latex_to_unicode(sp.tex, sp.display_math);
            int pw = str_width(plain);
            if (olno == 0 && sp_src != nullptr && orow != -1) {
              TCell::Piece pc;
              pc.col = col;
              pc.cols = std::max(2, pw);
              pc.tex = sp.tex;
              pc.display = sp.display_math;
              tc.pieces.push_back(pc);
            }
            col += pw;
          }
          (void)fits;
          l.cells.push_back(tc);
          x += w[c] + 1;
        }
        bl.lines.push_back(l);
        row++;
      }
      if (!r.empty() && r[0].header) border_line(3);
      else if (ri + 1 < nrows && b.table_style != "three" &&
               b.table_style != "tuack")
        border_line(4, (int)ri);  // grid: a rule between every pair of rows
    }
    border_line(2);
    row += opt_.paragraph_gap;
  
}

// Width of a code block's frame.  The frame spans the page by default; with
// --code-fit it hugs the code instead (a short snippet then gets a small box).
int DocView::code_frame_width(const Block& b) const {
  if (!opt_.code_fit) return std::min(cols_, content_w_ + 1);
  int maxw = 0;
  for (const std::string& l : split_lines(b.code)) maxw = std::max(maxw, str_width(l));
  int w = maxw + 4;                                   // 2 borders + 2 padding
  if (!b.lang.empty()) w = std::max(w, (int)str_width(b.lang) + 9);  // label in the top border
  w = std::min(w, content_w_ + 1);
  return std::max(20, w);
}

// Splits one code line into segments of at most `width` columns, remembering
// the byte offset each segment starts at (that is what syntax colours index).
void DocView::wrap_code_line(const std::string& line, int width,
                             std::vector<std::pair<std::string, int>>& out) {
  if (line.empty()) { out.emplace_back("", 0); return; }
  size_t i = 0, start = 0;
  int cur = 0;
  while (i < line.size()) {
    size_t save = i;
    uint32_t cp = utf8_next(line, i);
    int cw = std::max(1, cp_width(cp));
    if (cur + cw > width && i > start) {
      out.emplace_back(line.substr(start, save - start), (int)start);
      start = save;
      cur = 0;
    }
    cur += cw;
  }
  out.emplace_back(line.substr(start), (int)start);
}

namespace {
// The four Luogu callout severities and their bar colours (tokyo-night-ish,
// matching the palette the themes already use).
bool is_callout_name(const std::string& n) {
  return n == "info" || n == "success" || n == "warning" || n == "error";
}
RGB callout_color(const std::string& n) {
  if (n == "success") return RGB{158, 206, 106};
  if (n == "warning") return RGB{224, 175, 104};
  if (n == "error")   return RGB{247, 118, 142};
  return RGB{126, 197, 255};  // info
}
}  // namespace

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
        // A dimmed "#", "##", ... in front of the text: without it a heading is
        // only recognisable by its colour, which is hard to see on some
        // terminals.  Wrapped heading lines stay aligned with the text.
        std::string hashes((size_t)std::min(6, std::max(1, b.level)), '#');
        int marker_w = (int)hashes.size() + 1;
        int text_x = 1 + marker_w;
        row += (bi == 0 ? 0 : 1);
        if (b.level <= 2) {
          // headings 1-2 get a rule underneath
          std::vector<Line> lines;
          build_lines(b.spans, text_x, content_w_ - text_x, base, lines);
          for (size_t li2 = 0; li2 < lines.size(); li2++) {
            if (li2 == 0) { lines[li2].marker = hashes + " "; lines[li2].marker_w = marker_w; }
            lines[li2].row = row - bl.row;
            bl.lines.push_back(lines[li2]);
            row += lines[li2].rows;
          }
          Line rule;
          rule.kind = Line::Rule;
          rule.row = row - bl.row;
          bl.lines.push_back(rule);
          row++;
          row += (b.level == 1 ? 1 : 0);
        } else {
          std::vector<Line> lines;
          build_lines(b.spans, text_x, content_w_ - text_x, base, lines);
          for (size_t li2 = 0; li2 < lines.size(); li2++) {
            if (li2 == 0) { lines[li2].marker = hashes + " "; lines[li2].marker_w = marker_w; }
            lines[li2].row = row - bl.row;
            bl.lines.push_back(lines[li2]);
            row += lines[li2].rows;
          }
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
            l.rows = a->rows;
          } else {
            l.kind = Line::Text;
          }
          bl.lines.push_back(l);
          int rows_needed = (!a->failed ? a->rows : 1);
          row += rows_needed;
          if (!a->failed) row += 0;
        } else if (img_only && opt_.inline_images) {  // a text dump keeps the alt text
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
              l.rows = a->rows;
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
          for (auto& l : lines) { l.row = row - bl.row; bl.lines.push_back(l); row += l.rows; }
        }
        row += opt_.paragraph_gap;
        break;
      }
      case Block::CodeBlock: {
        std::vector<std::string> clines = split_lines(b.code);
        if (clines.empty()) clines.push_back("");
        int frame_w = code_frame_width(b);
        int inner = std::max(8, frame_w - 3);
        // The frame: a top border, the wrapped lines, a bottom border.
        Line top;
        top.kind = Line::Code;
        top.row = 0;
        top.code_index = -1;
        bl.lines.push_back(top);
        row++;
        for (size_t k = 0; k < clines.size(); k++) {
          std::vector<std::pair<std::string, int>> segs;
          wrap_code_line(clines[k], inner, segs);
          for (auto& seg : segs) {
            Line l;
            l.kind = Line::Code;
            l.code_index = (int)k;          // source line, for syntax colours
            l.code_col = seg.second;        // byte offset inside that line
            l.code_text = seg.first;
            l.row = row - bl.row;
            bl.lines.push_back(l);
            row++;
          }
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
        int start_row = row;
        std::vector<std::vector<Line>> chunks;
        for (auto& sublist : b.items)
          for (const Block& sb : sublist) {
            std::vector<Line> lines;
            quote_lines(lines, sb, 3, std::vector<int>{1});
            if (!lines.empty()) chunks.push_back(std::move(lines));
          }
        for (size_t k = 0; k < chunks.size(); k++) {
          if (k) {  // the blank line that separated the two paragraphs
            Line gap;
            gap.bars = std::vector<int>{1};
            gap.row = row - bl.row;
            bl.lines.push_back(gap);
            row++;
          }
          for (Line& l : chunks[k]) {
            l.row = row - bl.row;
            bl.lines.push_back(l);
            row += l.rows;
          }
        }
        if (row == start_row) {
          Line l;
          l.row = 0;
          bl.lines.push_back(l);
          row++;
        }
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
            // "[x]" / "[ ]" instead of U+2611/U+2610: those glyphs have
            // ambiguous or emoji presentation, so the tick and the empty box
            // do not line up in every font.  Brackets are always one cell.
            marker = checked ? "[x] " : "[ ] ";
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
                for (auto& sublist : sb.items)
                  for (const Block& qb : sublist) {
                    std::vector<Line> qlines;
                    quote_lines(qlines, qb, item_indent + 2, std::vector<int>{item_indent});
                    for (Line& l2 : qlines) lines.push_back(l2);
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
              row += l.rows;
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

      case Block::Directive: {
        directive_layout(bl, b, row, 1, std::vector<int>{}, std::vector<RGB>{}, false,
                         RGB{0, 0, 0});
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
          l.rows = a->rows;
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
void DocView::quote_lines(std::vector<Line>& out, const Block& qb, int indent,
                          std::vector<int> bars, std::vector<RGB> bar_cols) {
  if (qb.type == Block::Quote) {
    // > > nested: one more bar in the column the inner text used to start at
    bars.push_back(indent);
    bar_cols.push_back(theme_.quote_bar);
    for (auto& sublist : qb.items)
      for (const Block& inner : sublist)
        quote_lines(out, inner, indent + 2, bars, bar_cols);
    return;
  }
  std::vector<Line> lines;
  switch (qb.type) {
    case Block::Heading: {
      Span base;
      base.bold = true;
      base.has_color = true;
      base.color = theme_.heading[std::min(6, std::max(1, qb.level))];
      build_lines(qb.spans, indent, content_w_ - indent, base, lines);
      break;
    }
    case Block::Paragraph:
    case Block::Html: {
      Span base;
      base.has_color = true;
      base.color = theme_.quote_fg;
      build_lines(qb.spans.empty() ? std::vector<Span>{} : qb.spans, indent,
                  content_w_ - indent, base, lines);
      break;
    }
    case Block::CodeBlock: {
      Span base;
      base.has_color = true;
      base.color = theme_.code_fg;
      for (auto& cl : split_lines(qb.code)) {
        Line l;
        l.kind = Line::Code;
        Run r;
        r.text = cl;
        r.x = indent;
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
      lines.push_back(l);
      break;
    }
    default: {
      std::string t = !qb.spans.empty() ? spans_plain_text(qb.spans) : qb.code;
      if (!t.empty()) {
        std::vector<Span> sp;
        Span s2;
        s2.text = t;
        s2.has_color = true;
        s2.color = theme_.quote_fg;
        sp.push_back(s2);
        Span base;
        base.has_color = true;
        base.color = theme_.quote_fg;
        build_lines(sp, indent, content_w_ - indent, base, lines);
      }
      break;
    }
  }
  for (Line& l : lines) {
    l.bars = bars;
    l.bar_rgbs = bar_cols;
    out.push_back(l);
  }
}

// One Luogu directive container: callout (coloured bar + bold title),
// align{left|center|right}, epigraph (right-aligned, dimmed, attribution) or
// an unknown name (content passes through unchanged).  Nesting works because
// children go through directive_child_layout, which calls back into here.
// A callout title sits on a faint wash of its own colour so it reads as a
// collapsible-box header instead of an ordinary bold line.
RGB DocView::band_tint(RGB c) const {
  RGB b = theme_.bg;
  return RGB{(uint8_t)((b.r * 84 + c.r * 16) / 100),
             (uint8_t)((b.g * 84 + c.g * 16) / 100),
             (uint8_t)((b.b * 84 + c.b * 16) / 100)};
}

void DocView::directive_layout(BlockLayout& bl, const Block& b, int& row, int indent,
                               const std::vector<int>& bars, const std::vector<RGB>& bar_cols,
                               bool custom, RGB bar_rgb) {
  if (b.dir_leaf) return;
  bool callout = is_callout_name(b.dir_name);
  RGB c = callout ? callout_color(b.dir_name) : bar_rgb;
  bool cu = custom || callout;
  // A callout draws its own bar in column `indent`; everything belonging to it
  // (title, content, nested containers) carries that column plus its colour.
  std::vector<int> own_bars = bars;
  std::vector<RGB> own_cols = bar_cols;
  if (callout) { own_bars.push_back(indent); own_cols.push_back(c); }
  if (callout && !b.dir_label.empty()) {
    // The title is ordinary inline markup - Luogu titles regularly carry
    // formulas (:::info[$x^2$ 标题]) - so parse and wrap it like a paragraph.
    std::vector<Span> sp = inline_parser_.parse_inline(b.dir_label);
    Span base;
    base.bold = true;
    base.has_color = true;
    base.color = c;
    std::vector<Line> tl;
    build_lines(sp, indent + 2, content_w_ - indent - 2, base, tl);
    if (tl.empty()) tl.resize(1);
    RGB tint = band_tint(c);
    for (Line& l : tl) {
      l.bars = own_bars;
      l.bar_rgbs = own_cols;
      l.bars_custom = cu;
      l.bar_rgb = c;
      l.band = true;
      l.band_bg = tint;
      l.band_x0 = indent;             // from the callout's own bar column ...
      l.band_x1 = content_w_ - 1;     // ... to the right content edge
      l.row = row - bl.row;
      bl.lines.push_back(l);
      row += l.rows;
    }
  }
  std::vector<int> bars2 = own_bars;
  std::vector<RGB> cols2 = own_cols;
  int ind2 = indent;
  if (callout) ind2 = indent + 2;

  std::string a = to_lower(b.dir_attrs);
  Align al = Align::Left;
  bool shift = false, epi = false;
  if (b.dir_name == "align") {
    al = a == "center" ? Align::Center : a == "right" ? Align::Right : Align::Left;
    shift = al != Align::Left;
  } else if (b.dir_name == "epigraph") {
    al = Align::Right;
    shift = epi = true;
  }

  size_t first = bl.lines.size();
  static const std::vector<Block> no_blocks;
  const std::vector<Block>& sub = b.items.empty() ? no_blocks : b.items[0];
  for (const Block& sb : sub)
    directive_child_layout(bl, sb, row, ind2, bars2, cols2, cu, c);

  if (shift) {
    // move each built line so its content sits centred / right-aligned inside
    // the content width
    for (size_t k = first; k < bl.lines.size(); k++) {
      Line& l = bl.lines[k];
      if (l.runs.empty()) continue;
      int minx = 1 << 30, maxx = 0;
      for (const auto& r : l.runs) {
        if (r.width <= 0 && r.text.empty()) continue;
        minx = std::min(minx, r.x);
        maxx = std::max(maxx, r.x + r.width);
      }
      if (minx > maxx) continue;
      int delta = al == Align::Center ? (content_w_ - (maxx - minx)) / 2 - minx
                                      : (content_w_ - 1) - maxx;
      for (auto& r : l.runs) r.x += delta;
    }
  }
  if (epi) {
    // readable quote colour instead of DIM-on-muted: the epigraph used to be
    // the darkest text on the page
    for (size_t k = first; k < bl.lines.size(); k++)
      for (auto& r : bl.lines[k].runs) {
        if (!r.style.has_color) { r.style.has_color = true; r.style.color = theme_.quote_fg; }
      }
    if (!b.dir_label.empty()) {
      Line l;
      Run r;
      r.text = b.dir_label;
      r.width = str_width(r.text);
      r.x = std::max(1, content_w_ - 1 - r.width);
      r.style.has_color = true;
      r.style.color = theme_.muted;
      l.runs.push_back(r);
      l.bars = bars;
      l.bar_rgbs = bar_cols;
      l.bars_custom = custom;
      l.bar_rgb = bar_rgb;
      l.row = row - bl.row;
      bl.lines.push_back(l);
      row += 1;
    }
  }
}

// One child block inside a directive container.  Everything ends up as lines
// of the SAME BlockLayout (tables go through layout_table, formulas through
// build_lines so their bitmaps reserve rows just like everywhere else).
void DocView::directive_child_layout(BlockLayout& bl, const Block& sb, int& row, int indent,
                                     const std::vector<int>& bars,
                                     const std::vector<RGB>& bar_cols, bool custom, RGB bar_rgb) {
  auto stamp = [&](std::vector<Line>& lines) {
    for (auto& l : lines) {
      if (!l.bars.empty() || !bars.empty()) {
        if (l.bars.empty()) { l.bars = bars; l.bar_rgbs = bar_cols; }
        if (custom) { l.bars_custom = true; l.bar_rgb = bar_rgb; }
      }
      l.row = row - bl.row;
      bl.lines.push_back(l);
      row += l.rows;
    }
  };
  switch (sb.type) {
    case Block::Directive:
      directive_layout(bl, sb, row, indent, bars, bar_cols, custom, bar_rgb);
      return;
    case Block::Heading: {
      Span base;
      base.bold = true;
      base.has_color = true;
      base.color = theme_.heading[std::min(6, std::max(1, sb.level))];
      std::vector<Line> lines;
      build_lines(sb.spans, indent, content_w_ - indent, base, lines);
      stamp(lines);
      return;
    }
    case Block::Paragraph:
    case Block::Html: {
      std::vector<Line> lines;
      build_lines(sb.spans, indent, content_w_ - indent, Span{}, lines);
      stamp(lines);
      return;
    }
    case Block::CodeBlock: {
      Span base;
      base.has_color = true;
      base.color = theme_.code_fg;
      int idx = 0;
      for (auto& cl : split_lines(sb.code)) {
        Line l;
        l.kind = Line::Code;
        l.code_index = idx++;
        l.code_text = cl;
        Run r;
        r.text = cl;
        r.x = indent;
        r.width = str_width(cl);
        r.style = base;
        l.runs.push_back(r);
        std::vector<Line> one{std::move(l)};
        stamp(one);
      }
      return;
    }
    case Block::Hr: {
      std::vector<Line> lines(1);
      lines[0].kind = Line::Rule;
      stamp(lines);
      return;
    }
    case Block::MathBlock: {
      Span ms;
      ms.kind = Span::Math;
      ms.tex = sb.code;
      ms.display_math = true;
      std::vector<Line> lines;
      build_lines(std::vector<Span>{ms}, indent, content_w_ - indent, Span{}, lines);
      stamp(lines);
      return;
    }
    case Block::Table:
      layout_table(sb, indent, content_w_ - indent, bl, row);
      return;
    case Block::Quote: {
      std::vector<Line> lines;
      quote_lines(lines, sb, indent, bars, bar_cols);
      if (custom)
        for (auto& l : lines) { l.bars_custom = true; l.bar_rgb = bar_rgb; }
      stamp(lines);
      return;
    }
    case Block::List: {
      int li = 0;
      for (size_t k = 0; k < sb.items.size(); k++) {
        bool task = k < sb.item_is_task.size() && sb.item_is_task[k];
        bool checked = k < sb.item_checked.size() && sb.item_checked[k];
        std::string marker = task ? (checked ? "[x] " : "[ ] ")
                           : sb.ordered ? std::to_string(sb.start_num + li) + ". "
                                        : "\u2022 ";
        li++;
        int mw = str_width(marker);
        bool firstm = true;
        for (const Block& ib : sb.items[k]) {
          if (ib.type != Block::Paragraph && ib.type != Block::Html) {
            directive_child_layout(bl, ib, row, indent + mw, bars, bar_cols, custom, bar_rgb);
            firstm = false;
            continue;
          }
          std::vector<Line> lines;
          build_lines(ib.spans, indent + mw, content_w_ - indent - mw, Span{}, lines);
          for (auto& l : lines) {
            if (firstm) {
              Run r;
              r.text = marker;
              r.x = indent;
              r.width = mw;
              r.style.has_color = true;
              r.style.color = task ? (checked ? RGB{140, 195, 140} : theme_.muted)
                                   : theme_.bullet;
              l.runs.insert(l.runs.begin(), r);
              firstm = false;
            }
            std::vector<Line> one{std::move(l)};
            stamp(one);
          }
        }
        if (firstm) row += 0;  // empty item: nothing to show
      }
      return;
    }
    default:
      return;
  }
}

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
      style.kind = Span::Code;   // runs keep the kind: draw gives them a wash
    }
    if (sp.is_link && sp.kind != Span::Image) {
      style.has_color = true;
      style.color = theme_.link;
      style.is_link = true;    // hit testing needs the URL on the run
      style.link = sp.link;
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
    cur.rows = 1;
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
      cur.rows = std::max(cur.rows, r.rows);
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
        l.rows = asset->rows;
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
