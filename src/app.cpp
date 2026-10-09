// app.cpp : the interactive reader (event loop, outline panel, search, status bar).
#include "app.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define stat _stat
#define stat_struct _stat64
#else
#include <unistd.h>
#endif

#include "font.h"
#include "image.h"
#include "mdmath.h"
#include "md.h"
#include "platform.h"
#include "render.h"
#include "term.h"
#include "util.h"

namespace mdt {

// --------------------------------------------------------------- themes -----
static Theme theme_by_name(const std::string& name) {
  Theme t;
  std::string n = to_lower(name);
  if (n == "light") {
    t.bg = {252, 252, 250}; t.fg = {40, 44, 52}; t.muted = {120, 126, 136};
    t.heading[1] = {28, 92, 171}; t.heading[2] = {30, 112, 116}; t.heading[3] = {60, 84, 140};
    t.heading[4] = {70, 76, 88}; t.heading[5] = {86, 90, 100}; t.heading[6] = {100, 104, 112};
    t.link = {28, 110, 200}; t.code_fg = {150, 80, 30}; t.code_bg = {242, 242, 245};
    t.code_frame = {210, 212, 220}; t.quote_fg = {96, 102, 112}; t.quote_bar = {150, 170, 200};
    t.bullet = {28, 110, 200}; t.rule = {210, 212, 220}; t.table_border = {200, 203, 212};
    t.table_header_bg = {238, 240, 245}; t.table_header_fg = {40, 44, 52};
    t.math_fg = {40, 44, 52}; t.status_bg = {238, 240, 245}; t.status_fg = {90, 96, 106};
    t.marker_bg = {224, 228, 236}; t.image_frame = {205, 208, 216};
  } else if (n == "nord") {
    t.bg = {46, 52, 64}; t.fg = {216, 222, 233}; t.muted = {143, 155, 179};
    t.heading[1] = {136, 192, 208}; t.heading[2] = {143, 188, 187}; t.heading[3] = {129, 161, 193};
    t.heading[4] = {180, 190, 205}; t.heading[5] = {170, 178, 190}; t.heading[6] = {160, 168, 180};
    t.link = {136, 192, 208}; t.code_fg = {235, 203, 139}; t.code_bg = {59, 66, 82};
    t.code_frame = {76, 86, 106}; t.quote_fg = {175, 184, 200}; t.quote_bar = {94, 129, 172};
    t.bullet = {136, 192, 208}; t.rule = {76, 86, 106}; t.table_border = {76, 86, 106};
    t.table_header_bg = {59, 66, 82}; t.table_header_fg = {216, 222, 233};
    t.math_fg = {235, 203, 139}; t.status_bg = {59, 66, 82}; t.status_fg = {190, 197, 210};
  } else if (n == "monochrome" || n == "mono") {
    t.bg = {0, 0, 0}; t.fg = {230, 230, 230}; t.muted = {130, 130, 130};
    for (int i = 1; i <= 6; i++) t.heading[i] = {255, 255, 255};
    t.link = {200, 200, 200}; t.code_fg = {220, 220, 220}; t.code_bg = {20, 20, 20};
    t.code_frame = {90, 90, 90}; t.quote_fg = {180, 180, 180}; t.quote_bar = {140, 140, 140};
    t.bullet = {220, 220, 220}; t.rule = {90, 90, 90}; t.table_border = {110, 110, 110};
    t.table_header_bg = {30, 30, 30}; t.table_header_fg = {240, 240, 240};
    t.math_fg = {240, 240, 240}; t.status_bg = {25, 25, 25}; t.status_fg = {190, 190, 190};
  }
  return t;
}

// ------------------------------------------------------------ app state -----
namespace {

struct OutlineItem {
  int block = 0;
  int level = 1;
  std::string text;
};

struct App {
  AppOptions opt;
  Terminal term;
  MathRenderer math;
  DocView view;
  Theme theme;
  std::vector<OutlineItem> outline;
  std::vector<std::pair<int, std::string>> search_hits;  // (block, context)
  std::string status_msg;
  double status_until = 0;
  bool toc_open = false;
  bool help_open = false;
  bool link_open = false;          // the clicked-link dialog
  std::string link_url;
  bool search_mode = false;
  bool quit = false;
  std::string search_query;
  int toc_sel = 0, toc_off = 0, toc_panel_w = 0;
  std::string file;
  size_t file_index = 0;
  bool needs_frame = true;
  bool images_dirty = true;
  bool show_scrollbar = true;
  bool terminal_bg = false;
  bool scrollbar_active = false;   // set while drawing; used for hit testing
  bool scroll_drag = false;        // a scrollbar drag is in progress
  Screen scr;
  int frame_log_no_ = 0;           // frame counter for MDT_FRAME_LOG

  bool images_pending_ = false;    // mirror of view.images_pending()

  void set_status(const std::string& s, double secs = 3.0) {
    status_msg = s;
    status_until = now_ms() + secs * 1000;
  }
  int view_rows() const { return std::max(1, term.caps.rows - 1); }
  int view_cols() const { return term.caps.cols; }

  bool load_file(const std::string& path, std::string* err);
  void build_outline();
  void do_search(const std::string& q);
  void jump_to_next_hit(int dir);
  void render();
  void run();
  std::string status_line() const;
};

bool App::load_file(const std::string& path, std::string* err) {
  std::string text;
  if (!read_file(path, text)) {
    if (err) *err = "cannot read file: " + path;
    return false;
  }
  file = path;
  MdOptions mo;
  mo.math = (opt.math != "off");
  mo.entities = opt.entities == "force" ? MdOptions::EntForce : MdOptions::EntOff;
  mo.escapes = opt.escapes == "off"      ? MdOptions::EscOff
             : opt.escapes == "force"    ? MdOptions::EscForce
                                         : MdOptions::EscAuto;
  mo.loose = opt.loose == "off"      ? MdOptions::LooseOff
           : opt.loose == "on"       ? MdOptions::LooseOn
                                     : MdOptions::LooseAuto;
  MarkdownParser parser;
  MdDocument doc = parser.parse(text, mo);
  view.set_width(view_cols());
  view.set_view_rows(view_rows());
  view.set_document(std::move(doc), path, dir_name(abs_path(path)));
  build_outline();
  term.set_title(fmt("%s - mdt", file.c_str()));
  return true;
}

void App::build_outline() {
  outline.clear();
  for (size_t i = 0; i < view.doc().blocks.size(); i++) {
    const Block& b = view.doc().blocks[i];
    if (b.type != Block::Heading) continue;
    OutlineItem it;
    it.block = (int)i;
    it.level = b.level;
    it.text = spans_plain_text(b.spans);
    outline.push_back(it);
  }
  toc_sel = 0;
}

void App::do_search(const std::string& q) {
  search_hits.clear();
  if (q.empty()) return;
  std::string needle = to_lower(q);
  std::string buf;
  for (size_t i = 0; i < view.doc().blocks.size(); i++) {
    std::string text;
    const Block& b = view.doc().blocks[i];
    switch (b.type) {
      case Block::CodeBlock: text = b.code; break;
      case Block::MathBlock: text = b.code; break;
      case Block::Table: {
        for (auto& r : b.rows)
          for (auto& c : r) text += spans_plain_text(c.spans) + " | ";
        break;
      }
      default: text = spans_plain_text(b.spans); break;
    }
    std::string low = to_lower(text);
    size_t pos = low.find(needle);
    if (pos != std::string::npos) {
      std::string ctx = text.substr(pos > 20 ? pos - 20 : 0, needle.size() + 50);
      search_hits.emplace_back((int)i, collapse_ws(ctx));
    }
  }
  if (search_hits.empty()) {
    set_status("no match: " + q);
  } else {
    set_status(fmt("%d match(es) for '%s'", (int)search_hits.size(), q.c_str()));
    view.scroll_to(std::max(0, view.row_of_block(search_hits[0].first) - 2));
  }
}

void App::jump_to_next_hit(int dir) {
  if (search_hits.empty()) {
    set_status("no active search");
    return;
  }
  int cur_row = view.scroll_top();
  int cur = -1;
  for (size_t i = 0; i < search_hits.size(); i++) {
    int r = view.row_of_block(search_hits[i].first);
    if (r <= cur_row + 1) cur = (int)i;
  }
  int next;
  if (dir > 0) next = (cur + 1) % (int)search_hits.size();
  else next = (cur <= 0) ? (int)search_hits.size() - 1 : cur - 1;
  view.scroll_to(std::max(0, view.row_of_block(search_hits[(size_t)next].first) - 2));
  set_status(fmt("match %d/%d: %s", next + 1, (int)search_hits.size(), search_hits[(size_t)next].second.c_str()), 4);
}

// ------------------------------------------------------------- rendering ----
void App::render() {
  int vcols = view_cols(), vrows = view_rows();
  Screen& scr = this->scr;
  if (scr.width() != vcols || scr.height() != term.caps.rows)
    scr.init(vcols, term.caps.rows, theme.fg, theme.bg);
  scr.set_terminal_bg(terminal_bg);
  scr.clear();

  view.set_width(vcols);
  view.set_view_rows(vrows);
  view.scroll_to(view.scroll_top());

  view.draw(scr, 0, 0, vcols, vrows, view.scroll_top());

  // ---- scrollbar ----------------------------------------------------------
  // It lives in the right margin (content_margin is 1), so it never covers text.
  scrollbar_active = false;
  if (show_scrollbar && view.total_rows() > vrows) {
    scrollbar_active = true;
    int x = vcols - 1;
    int total = view.total_rows();
    int max_scroll = std::max(1, total - vrows);
    int thumb = std::max(2, (int)std::lround((double)vrows * vrows / (double)total));
    if (thumb > vrows) thumb = vrows;
    int travel = vrows - thumb;
    int top = travel > 0 ? (int)std::lround((double)view.scroll_top() * travel / max_scroll) : 0;
    top = std::max(0, std::min(travel, top));
    RGB track = theme.rule, thumb_fg = theme.bullet;
    for (int y = 0; y < vrows; y++) {
      bool on = y >= top && y < top + thumb;
      scr.put(x, y, on ? 0x2588 : 0x2502, on ? thumb_fg : track, theme.bg, 0);
    }
  }

  // ---- overlays -----------------------------------------------------------
  if (toc_open && !outline.empty()) {
    int pw = std::min(42, std::max(24, vcols / 3));
    toc_panel_w = pw;
    int px = vcols - pw;
    RGB pbg = theme.status_bg;
    scr.fill_rect(px, 0, pw, vrows, pbg);
    scr.put_str(px + 2, 0, "OUTLINE", theme.heading[1], pbg, A_BOLD);
    int visible = vrows - 2;
    if (toc_sel < toc_off) toc_off = toc_sel;
    if (toc_sel >= toc_off + visible) toc_off = toc_sel - visible + 1;
    for (int i = 0; i < visible; i++) {
      int idx = toc_off + i;
      if (idx >= (int)outline.size()) break;
      const OutlineItem& it = outline[(size_t)idx];
      RGB fg = (it.level <= 2) ? theme.heading[std::min(6, it.level)] : theme.fg;
      int ind = std::min(6, std::max(0, it.level - 1)) * 2;
      std::string label = substr_cells(it.text, 0, pw - ind - 4);
      RGB bg = (idx == toc_sel) ? theme.marker_bg : pbg;
      for (int k = 0; k < pw - 2; k++) scr.put(px + 1 + k, i + 1, ' ', fg, bg);
      scr.put_str(px + 2 + ind, i + 1, label, fg, bg, idx == toc_sel ? A_BOLD : 0);
    }
  }
  if (link_open && !link_url.empty()) {
    int uw = (int)str_width(link_url);
    int pw = std::min(vcols - 4, std::max(46, uw + 6));
    int inner = pw - 4;
    int ulines = std::max(1, (uw + inner - 1) / inner);
    int ph = std::min(vrows - 2, ulines + 6);
    int px = (vcols - pw) / 2, py = (vrows - ph) / 2;
    RGB pbg = theme.status_bg;
    scr.fill_rect(px, py, pw, ph, pbg);
    scr.box_rounded(px, py, pw, ph, theme.code_frame, pbg);
    scr.put_str(px + 2, py + 1, " link ", theme.muted, pbg);
    for (int k = 0; k < ulines; k++) {
      size_t b = (size_t)k * inner;
      std::string part = link_url.substr(b, inner);
      scr.put_str(px + 2, py + 2 + k, part, theme.link, pbg);
    }
    scr.put_str(px + 2, py + 2 + ulines + 1,
                "c copy (clipboard)   o open in browser   Esc close", theme.muted, pbg);
  }
  if (help_open) {
    int pw = std::min(74, vcols - 4), ph = std::min(20, vrows - 2);
    int px = (vcols - pw) / 2, py = (vrows - ph) / 2;
    RGB pbg = theme.status_bg;
    scr.fill_rect(px, py, pw, ph, pbg);
    scr.box_rounded(px, py, pw, ph, theme.code_frame, pbg);
    const char* lines[] = {
      "mdt - markdown in the terminal",
      "",
      "  j / k, arrows     scroll one line",
      "  d / u             half page       space / b   page",
      "  g / G             top / bottom    J / K       next / prev heading",
      "  Tab               toggle outline  Enter       jump to heading",
      "  /                 search          n / N       next / prev match",
      "  o                 open first link in the browser",
      "  click a link      show it in a dialog; c copies, o opens",
      "  e                 export the document text to clipboard",
      "  r                 reload file     t           cycle theme",
      "  R                 force redraw    m           toggle maths rendering",
      "  ? / Esc           close this help q / Ctrl-C  quit",
      "",
      "  Images and formulas are drawn with the terminal graphics protocol",
      "  (kitty / iTerm2 / sixel); maths is typeset by the embedded TeX engine.",
    };
    int n = (int)(sizeof(lines) / sizeof(lines[0]));
    for (int i = 0; i < n && i + 2 < ph; i++)
      scr.put_str(px + 2, py + 1 + i, lines[i], i == 0 ? theme.heading[1] : theme.fg, pbg,
                  i == 0 ? A_BOLD : 0);
  }

  // ---- status bar ---------------------------------------------------------
  int sy = term.caps.rows - 1;
  RGB sfg = theme.status_fg, sbg = theme.status_bg;
  scr.fill_rect(0, sy, vcols, 1, sbg);
  std::string left = file;
  size_t slash = left.find_last_of('/');
  if (slash != std::string::npos) left = left.substr(slash + 1);
  int total = std::max(1, view.total_rows());
  int pct = (int)std::lround(100.0 * view.scroll_top() / total);
  int block = view.block_at_row(view.scroll_top());
  int src_line = 0;
  if (block >= 0 && block < (int)view.doc().blocks.size()) src_line = view.doc().blocks[(size_t)block].src_line;
  std::string engine = math.has_js() ? "katex" : "unicode";
  if (view.doc().entities_unescaped) engine += " +entities";
  if (view.doc().backslashes_removed > 0) engine += " +escapes";
  if (view.doc().loose_markers) engine += " +loose";
  if (view.doc().invisible_removed > 0) engine += " +clean";
  if (view.doc().code_gaps_collapsed > 0 || view.doc().code_refs_decoded > 0) engine += " +code";
  std::string msg = status_msg;
  if (now_ms() > status_until) msg.clear();
  std::string text;
  if (search_mode) {
    text = "/" + search_query;
    scr.put_str(1, sy, substr_cells(text, 0, vcols - 2), RGB{255, 220, 150}, sbg, A_BOLD);
  } else {
    text = fmt(" %s   %d%%  %s   [%s]  %s", left.c_str(), pct,
               src_line ? fmt("line %d", src_line).c_str() : "", gfx_name(term.caps.gfx),
               engine.c_str());
    if (!msg.empty()) text += "   " + msg;
    scr.put_str(0, sy, substr_cells(text, 0, vcols), sfg, sbg);
  }

  // ---- output -------------------------------------------------------------
  // Text and graphics go out inside ONE synchronized update (DEC 2026): if the
  // window closed before the placements, the terminal painted the new text
  // with the old pictures for one frame and every drag flickered.  Only the
  // cells that changed are written (render_diff, not render_full).
  bool sync = scr.sync_update();
  std::string out;
  if (sync) out += "\x1b[?2026h";
  out += scr.render_diff(false);
  // ---- image placement ----------------------------------------------------
  // Images are drawn by the terminal on top of the text, and the non-kitty
  // protocols cannot be erased cell by cell.  So anything a panel covers (the
  // help box, the outline panel) or that would spill into the status bar is
  // simply not placed - the frame that has just been written repaints those
  // cells and the terminal drops the stale bits.
  std::vector<PlacedImage> place = view.images();
  // Panels occlude pictures partially, not wholly: the visible remainder is
  // placed as source-rectangle crops (kitty, which can place one image in
  // several pieces); protocols without that keep the old drop behaviour.
  int cellw = std::max(1, term.caps.px_cell_w()), cellh = std::max(1, term.caps.px_cell_h());
  auto clip_against = [&](int rx, int ry, int rw, int rh) {
    std::vector<PlacedImage> out;
    for (const PlacedImage& im : place) {
      int iw = im.cols > 0 ? im.cols : (im.px_w + cellw - 1) / cellw;
      int ih = im.src_h > 0 ? (im.src_h + cellh - 1) / cellh
                            : (im.rows > 0 ? im.rows : (im.px_h + cellh - 1) / cellh);
      int x0 = im.x, y0 = im.y, x1 = x0 + iw, y1 = y0 + ih;
      int hx0 = std::max(rx, x0), hy0 = std::max(ry, y0);
      int hx1 = std::min(rx + rw, x1), hy1 = std::min(ry + rh, y1);
      if (hx0 >= hx1 || hy0 >= hy1) {  // untouched by the panel
        out.push_back(im);
        continue;
      }
      if (term.caps.gfx != GfxProto::Kitty || im.sub_x || im.sub_y) continue;
      int pid = 1;
      auto frag = [&](int fx0, int fy0, int fx1, int fy1) {
        if (fx1 <= fx0 || fy1 <= fy0) return;
        PlacedImage f = im;
        f.place_id = pid++;
        f.x = fx0;
        f.y = fy0;
        f.src_x = im.src_x + (fx0 - x0) * cellw;
        f.src_y = im.src_y + (fy0 - y0) * cellh;
        f.src_w = std::min(im.px_w - f.src_x, (fx1 - fx0) * cellw);
        f.src_h = std::min(im.px_h - f.src_y, (fy1 - fy0) * cellh);
        if (im.cols > 0) { f.cols = fx1 - fx0; f.rows = fy1 - fy0; }
        out.push_back(f);
      };
      frag(x0, y0, x1, hy0);   // above the panel
      frag(x0, hy1, x1, y1);   // below it
      frag(x0, hy0, hx0, hy1);  // left of it
      frag(hx1, hy0, x1, hy1);  // right of it
    }
    place = out;
  };
  if (help_open) {
    int pw = std::min(74, vcols - 4), ph = std::min(20, vrows - 2);
    clip_against((vcols - pw) / 2, (vrows - ph) / 2, pw, ph);
  } else if (link_open && !link_url.empty()) {
    int uw = (int)str_width(link_url);
    int pw = std::min(vcols - 4, std::max(46, uw + 6));
    int ulines = std::max(1, (uw + pw - 5) / (pw - 4));
    int ph = std::min(vrows - 2, ulines + 6);
    clip_against((vcols - pw) / 2, (vrows - ph) / 2, pw, ph);
  } else if (toc_open && toc_panel_w > 0) {
    clip_against(0, 0, toc_panel_w + 1, vrows);
  }
  {
    int vrows = view_rows();
    place.erase(std::remove_if(place.begin(), place.end(),
                               [&](const PlacedImage& im) {
                                 int ph = im.src_h > 0 ? im.src_h : im.px_h;
                                 int rows = (ph + std::max(1, term.caps.px_cell_h()) - 1) /
                                            std::max(1, term.caps.px_cell_h());
                                 return im.y + rows > vrows;  // would cover the status bar
                               }),
                place.end());
  }
  out += term.emit_images(place);
  if (sync) out += "\x1b[?2026l";
  if (const char* log = getenv("MDT_FRAME_LOG")) {
    // Writes exactly what mdt sent, with escape sequences spelled out, so a
    // rendering problem can be told apart from a terminal problem.
    if (FILE* f = plat::open_file(log, "ab")) {
      fprintf(f, "--- frame %d, %dx%d, %zu bytes ---\n", frame_log_no_++, term.caps.cols,
              term.caps.rows, out.size());
      std::string vis;
      for (unsigned char c : out) {
        if (c == 0x1b) vis += "\\e";                       // ESC -> two readable chars
        else if (c == '\n') vis += "\\n";
        else if (c == '\r') vis += "\\r";
        else if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\x%02x", c); vis += b; }
        else vis += (char)c;
      }
      fwrite(vis.data(), 1, vis.size(), f);
      fputc('\n', f);
      fclose(f);
    }
  }
  term.write_raw(out);
  images_dirty = false;
  needs_frame = false;
}

// ------------------------------------------------------------------- run ----
void App::run() {
  // One mouse event per redraw is too coarse for a drag (the terminal reports
  // motion faster than we should paint) and too fine to waste frames on:
  auto open_url_in_browser = [&](const std::string& url) {
    std::string cmd = plat::open_url_command(url);
#ifdef _WIN32
    // `start` is a cmd builtin: run it through the shell.
    std::string full = "cmd /c " + cmd;
    int rc = system(full.c_str());
#else
    int rc = system(cmd.c_str());
#endif
    if (!term.mode_intact()) term.reassert_mode();  // the browser may share the tty
    if (rc == 0) set_status("opened: " + url);
    else set_status("failed to open: " + url);
  };
  // apply_mouse folds an event into the view and says whether anything moved.
  auto apply_mouse = [&](const Terminal::KeyEvent& e) -> bool {
    if (e.wheel_up) {
      int s0 = view.scroll_top(); view.scroll_by(-3); return view.scroll_top() != s0;
    }
    if (e.wheel_down) {
      int s0 = view.scroll_top(); view.scroll_by(3); return view.scroll_top() != s0;
    }
    if (e.release) { scroll_drag = false; return false; }
    if (!e.drag) {
      if (link_open) { link_open = false; return true; }  // click dismisses
      std::string url;
      if (view.link_at(e.mx, e.my, url)) {
        link_url = url;
        link_open = true;
        return true;
      }
    }
    if (scroll_drag || (!e.drag && scrollbar_active &&
                        e.mx >= view_cols() - 1 && e.my >= 0 && e.my < view_rows())) {
      if (!e.drag) scroll_drag = true;
      int total = view.total_rows(), vrows = view_rows();
      int max_scroll = std::max(0, total - vrows);
      int my = std::min(std::max(0, e.my), vrows - 1);
      double frac = vrows > 1 ? (double)my / (double)(vrows - 1) : 0.0;
      int s0 = view.scroll_top();
      view.scroll_to((int)std::lround(frac * max_scroll));
      return view.scroll_top() != s0;
    }
    if (!e.drag && toc_open && toc_panel_w > 0 && e.my >= 1 && e.mx >= term.caps.cols - toc_panel_w) {
      int idx = toc_off + (e.my - 1);
      if (idx >= 0 && idx < (int)outline.size()) {
        toc_sel = idx;
        int s0 = view.scroll_top();
        view.scroll_to(std::max(0, view.row_of_block(outline[(size_t)idx].block) - 1));
        return view.scroll_top() != s0 || true;
      }
    }
    return false;
  };
  while (!quit) {
    if (!term.mode_intact()) {  // a child process cooked our tty: take it back
      term.reassert_mode();
      needs_frame = true;
    }
    if (term.resized()) {
      term.handle_resize();
      needs_frame = true;
    }
    if (needs_frame) {
      render();
    }
    if (view.layout_dirty()) {  // a formula was measured: refresh the layout once
      view.relayout();
      needs_frame = true;
      continue;
    }
    if (view.poll_images()) {  // a picture finished downloading
      needs_frame = true;
      continue;
    }
    bool pending = view.images_pending();
    if (pending != images_pending_) {
      images_pending_ = pending;
      if (pending) set_status("loading images…", 3600);
      else set_status("images loaded", 1.5);
      needs_frame = true;
    }
    Terminal::KeyEvent e = term.read_event(120);
    if (e.type == Terminal::KeyEvent::None) continue;
    if (e.type == Terminal::KeyEvent::Mouse) {
      // Drain everything the terminal has queued: a drag reports one event per
      // cell crossed, and only the last position of this batch needs a frame.
      bool changed = apply_mouse(e);
      Terminal::KeyEvent e2;
      while ((e2 = term.read_event(0)).type == Terminal::KeyEvent::Mouse)
        changed = apply_mouse(e2) || changed;
      if (changed) needs_frame = true;
      continue;
    }
    if (e.type == Terminal::KeyEvent::Special && e.code == -1) break;  // EOF
    int key = e.code;
    char ch = e.ch;
    if (search_mode) {
      if (key == Terminal::K_ENTER) {
        search_mode = false;
        do_search(search_query);
      } else if (key == Terminal::K_ESC) {
        search_mode = false;
      } else if (key == Terminal::K_BACKSPACE) {
        if (!search_query.empty()) {
          search_query.pop_back();
        }
      } else if (e.type == Terminal::KeyEvent::Char && ch) {
        search_query += ch;
      }
      needs_frame = true;
      continue;
    }
    if (link_open) {  // the link dialog eats every key until it closes
      if (key == Terminal::K_ESC || (e.type == Terminal::KeyEvent::Char &&
                                     (ch == 'q' || ch == '?'))) {
        link_open = false;
      } else if (e.type == Terminal::KeyEvent::Char && ch == 'c') {
        term.set_clipboard(link_url);  // OSC 52: the terminal puts it on the clipboard
        set_status("copied: " + link_url, 2.5);
      } else if (key == Terminal::K_ENTER ||
                 (e.type == Terminal::KeyEvent::Char && ch == 'o')) {
        open_url_in_browser(link_url);
        link_open = false;
      }
      needs_frame = true;
      continue;
    }
    bool page = false;
    switch (key) {
      case Terminal::K_UP: view.scroll_by(-1); break;
      case Terminal::K_DOWN: view.scroll_by(1); break;
      case Terminal::K_LEFT: view.scroll_by(0); break;
      case Terminal::K_RIGHT: break;
      case Terminal::K_PGUP: view.scroll_page(-1); page = true; break;
      case Terminal::K_PGDN: view.scroll_page(1); page = true; break;
      case Terminal::K_HOME: view.scroll_home(); break;
      case Terminal::K_END: view.scroll_end(); break;
      case Terminal::K_ESC:
        if (link_open) link_open = false;
        else if (help_open) help_open = false;
        else if (toc_open) toc_open = false;
        else quit = true;
        break;
      default: break;
    }
    if (page) { needs_frame = true; continue; }
    if (e.type != Terminal::KeyEvent::Char) { needs_frame = true; continue; }
    switch (ch) {
      case 'q':
        if (!help_open && !toc_open && !link_open) quit = true;
        else { help_open = toc_open = link_open = false; }
        break;
      case 'j': view.scroll_by(1); break;
      case 'k': view.scroll_by(-1); break;
      case ' ': view.scroll_page(1); break;
      case 'b': view.scroll_page(-1); break;
      case 'd': view.scroll_by(std::max(1, view_rows() / 2)); break;
      case 'u': view.scroll_by(-std::max(1, view_rows() / 2)); break;
      case 'g': view.scroll_home(); break;
      case 'G': view.scroll_end(); break;
      case '\t': toc_open = !toc_open; break;
      case 'J': {
        int cur = view.block_at_row(view.scroll_top());
        int best = -1;
        for (auto& it : outline) { if (it.block > cur) { best = it.block; break; } }
        if (best >= 0) view.scroll_to(std::max(0, view.row_of_block(best) - 1));
        else set_status("last heading");
        break;
      }
      case 'K': {
        int cur = view.block_at_row(view.scroll_top());
        int best = -1;
        for (auto& it : outline) { if (it.block < cur) best = it.block; }
        if (best >= 0) view.scroll_to(std::max(0, view.row_of_block(best) - 1));
        else set_status("first heading");
        break;
      }
      case '/': search_mode = true; search_query.clear(); break;
      case 'n': jump_to_next_hit(1); break;
      case 'N': jump_to_next_hit(-1); break;
      case 'o': {
        const auto& links = view.doc().links;
        if (links.empty()) set_status("no links in this document");
        else open_url_in_browser(links[0].url);
        break;
      }
      case 'e': {
        std::string text = view.plain_text();
        term.set_clipboard(text);
        set_status(fmt("copied %d bytes to the clipboard", (int)text.size()));
        break;
      }
      case 'r': {
        std::string err;
        if (load_file(file, &err)) set_status("reloaded");
        else set_status(err);
        break;
      }
      case 't': {
        static const char* names[] = {"dark", "light", "nord", "monochrome"};
        static int ti = 0;
        ti = (ti + 1) % 4;
        theme = theme_by_name(names[ti]);
        view.set_theme(theme);
        set_status(std::string("theme: ") + names[ti]);
        break;
      }
      case 'm': {
        RenderOptions ro = view.options();
        ro.text_math = !ro.text_math;
        view.set_options(ro);
        view.relayout();
        set_status(ro.text_math ? "maths: plain Unicode (press m to typeset)"
                                : "maths: typeset via the embedded TeX engine");
        break;
      }
      case 'R': {
        term.drop_image_cache();
        view.clear_image_cache();
        view.set_theme(theme);
        needs_frame = true;
        set_status(fmt("[%s] %dx%d cells, cell %dx%d px, window %dx%d px",
                       gfx_name(term.caps.gfx), term.caps.cols, term.caps.rows,
                       term.caps.px_cell_w(), term.caps.px_cell_h(), term.caps.px_width(),
                       term.caps.px_height()));
        break;
      }
      case '?': help_open = !help_open; break;
      case 3: quit = true; break;  // Ctrl-C
      default: break;
    }
    needs_frame = true;
  }
}

}  // namespace

// ---------------------------------------------------------- non interactive -
static bool render_screenshot(App* app_placeholder, const AppOptions& opt, std::string* err);
static bool render_dump(const AppOptions& opt, std::string* err);
static bool render_diag(const AppOptions& opt, std::string* err);

// Reads a file, or stdin when the path is "-".
static bool read_source(const std::string& path, std::string& out, std::string* err) {
  if (path == "-") {
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), stdin)) > 0) out.append(buf, n);
    return !out.empty() || feof(stdin);
  }
  if (!read_file(path, out)) {
    if (err) *err = "cannot read " + path;
    return false;
  }
  return true;
}

int run_app(const AppOptions& opts) {
  if (opts.list_caps) {
    const char* term = getenv("TERM");
    const char* prog = getenv("TERM_PROGRAM");
    const char* ct = getenv("COLORTERM");
    bool kitty_env = getenv("KITTY_WINDOW_ID") || (prog && (std::string(prog) == "WezTerm" ||
                                                           std::string(prog) == "ghostty"));
    bool iterm_env = prog && std::string(prog) == "iTerm.app";
    printf("TERM                 %s\n", term ? term : "(unset)");
    printf("TERM_PROGRAM         %s\n", prog ? prog : "(unset)");
    printf("COLORTERM            %s\n", ct ? ct : "(unset)");
    printf("kitty graphics       %s\n", kitty_env ? "expected (env)" : "probed at start-up (APC Gi=31,a=q)");
    printf("iTerm2 images        %s\n", iterm_env ? "expected (env)" : "probed via TERM_PROGRAM");
    printf("sixel                probed via DA1 (CSI c) - run mdt on a tty to see the result\n");
    printf("cell size / window   probed via CSI 16 t / CSI 14 t\n");
    printf("\nmaths engine         built in (MathJax TeX->SVG inside QuickJS)%s\n",
#if MDT_HAVE_CURL
           " + libcurl"
#else
           ""
#endif
    );
    return 0;
  }
  if (opts.files.empty()) {
    fprintf(stderr, "mdt: no input file\n\n");
    print_usage();
    return 2;
  }
  std::string path = opts.files[0];

  // Non-interactive modes first: they own stdin/stdout.
  if (opts.diag) {
    std::string err;
    if (!render_diag(opts, &err)) { fprintf(stderr, "mdt: %s\n", err.c_str()); return 1; }
    return 0;
  }
  if (!opts.dump.empty()) {
    std::string err;
    if (!render_dump(opts, &err)) { fprintf(stderr, "mdt: %s\n", err.c_str()); return 1; }
    return 0;
  }
  if (!opts.screenshot.empty()) {
    std::string err;
    if (!render_screenshot(nullptr, opts, &err)) { fprintf(stderr, "mdt: %s\n", err.c_str()); return 1; }
    return 0;
  }
  std::string text, rerr;
  if (!read_source(path, text, &rerr)) {
    fprintf(stderr, "mdt: %s\n", rerr.c_str());
    return 1;
  }

  App app;
  app.opt = opts;
  app.theme = theme_by_name(opts.theme);
  app.theme.syntax = opts.syntax;
  app.theme.flat_bg = !opts.panels;
  app.show_scrollbar = opts.scrollbar;
  app.terminal_bg = opts.terminal_bg;
  std::string err;

  app.math.init(opts.math == "katex", &err);
  if (opts.math == "katex" && !app.math.has_js())
    fprintf(stderr, "mdt: maths engine unavailable (%s), using unicode\n", err.c_str());

  mdt::set_compat_mode(opts.compat);
  if (!app.term.init(opts.compat ? "none" : opts.gfx)) return 1;
  // --cell forces a cell pixel size.  Honour it in interactive mode too (it
  // used to apply to --snap only), so a pty or an exotic terminal whose probe
  // reports nothing can still be tested with realistic cell geometry.
  if (opts.cell_w > 0) {
    app.term.caps.cell_w = opts.cell_w;
    app.term.caps.win_w = app.term.caps.cols * opts.cell_w;
  }
  if (opts.cell_h > 0) {
    app.term.caps.cell_h = opts.cell_h;
    app.term.caps.win_h = app.term.caps.rows * opts.cell_h;
  }
  app.scr.set_sync_update(!opts.compat);
  app.scr.set_safe_paint(opts.compat);

  app.view.set_terminal(&app.term);
  app.view.set_math(&app.math);
  app.view.set_theme(app.theme);
  RenderOptions ro;
  ro.show_line_numbers = opts.show_line_numbers;
  ro.em_px_override = opts.font_px;
  ro.text_math = (opts.math == "unicode");
  ro.lazy_metrics = true;  // only typeset formulas that come into view
  ro.code_fit = opts.code_fit;
  ro.async_images = true;  // never block the reader on the network
  if (!app.term.caps.can_show_images()) {
    ro.text_math = true;  // no graphics protocol: formulas would be invisible
    ro.inline_images = false;
    ro.remote_images = false;  // ... and nothing is worth downloading
  }
  app.view.set_options(ro);
  app.view.set_width(app.view_cols());
  app.view.set_view_rows(app.view_rows());

  if (!app.load_file(path, &err)) {
    app.term.shutdown();
    fprintf(stderr, "mdt: %s\n", err.c_str());
    return 1;
  }
  app.toc_open = opts.toc;
  app.set_status(fmt("%s%s  |  ? for help",
                     gfx_name(app.term.caps.gfx),
                     app.view.doc().entities_unescaped
                          ? "  |  decoded HTML entities"
                          : (app.view.doc().backslashes_removed > 0
                                 ? "  |  removed backslash escapes"
                                 : "")), 5);
  app.run();
  app.term.shutdown();
  return 0;
}

// Renders the whole document into a plain text grid (no graphics).

// "--diag": everything mdt can see about the file and the terminal, so a
// rendering problem can be reported without guesswork.  Works without a tty.
static bool render_diag(const AppOptions& opt, std::string* err) {
  std::string path = opt.files.empty() ? std::string() : opt.files[0];
  std::string text;
  if (!path.empty() && !read_source(path, text, err)) return false;

  printf("mdt diag\n");
  printf("  file            : %s (%zu bytes)\n", path.empty() ? "-" : path.c_str(), text.size());
  // encoding
  std::string enc = "utf-8, no BOM";
  std::string body = text;
  if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB &&
      (unsigned char)text[2] == 0xBF) { enc = "utf-8 with BOM"; body = text.substr(3); }
  else if (text.size() >= 2 && (unsigned char)text[0] == 0xFF && (unsigned char)text[1] == 0xFE)
    enc = "utf-16le (BOM) - NOT SUPPORTED, save the file as UTF-8";
  else if (text.size() >= 2 && (unsigned char)text[0] == 0xFE && (unsigned char)text[1] == 0xFF)
    enc = "utf-16be (BOM) - NOT SUPPORTED, save the file as UTF-8";
  else {
    int bad = 0;
    for (size_t i = 0; i < body.size() && i < 200000; i++)
      if ((unsigned char)body[i] >= 0x80) {
        // count invalid utf-8 lead bytes
        size_t n = 1;
        if (((unsigned char)body[i] & 0xE0) == 0xC0) n = 2;
        else if (((unsigned char)body[i] & 0xF0) == 0xE0) n = 3;
        else if (((unsigned char)body[i] & 0xF8) == 0xF0) n = 4;
        bool ok = i + n <= body.size();
        for (size_t k = 1; ok && k < n; k++)
          if (((unsigned char)body[i + k] & 0xC0) != 0x80) ok = false;
        if (!ok) { bad++; i++; continue; }
        i += n - 1;
      }
    if (bad > 0) enc = "utf-8, no BOM, but " + std::to_string(bad) + " invalid byte(s) before offset 200000 - probably not UTF-8 (GBK/ANSI?)";
  }
  printf("  encoding        : %s\n", enc.c_str());
  bool crlf = body.find("\r\n") != std::string::npos;
  printf("  line endings    : %s\n", crlf ? "CRLF" : "LF");
  bool bom = enc.rfind("utf-8 with BOM", 0) == 0;
  printf("  first bytes     :");
  for (size_t i = 0; i < body.size() && i < 12; i++) printf(" %02X", (unsigned char)body[i]);
  printf("%s\n", bom ? "  (BOM stripped)" : "");

  MdOptions mo;
  MarkdownParser parser;
  MdDocument doc = parser.parse(body, mo);
  int cnt[12] = {0};
  for (auto& b : doc.blocks) {
    int k = (int)b.type;
    if (k >= 0 && k < 12) cnt[k]++;
  }
  printf("  source decoded  : entities=%s backslash escapes=%d invisible chars=%d\n",
         doc.entities_unescaped ? "yes" : "no", doc.backslashes_removed, doc.invisible_removed);
  printf("  blocks          : heading=%d paragraph=%d list=%d table=%d code=%d html=%d quote=%d\n",
         cnt[(int)Block::Heading], cnt[(int)Block::Paragraph], cnt[(int)Block::List],
         cnt[(int)Block::Table], cnt[(int)Block::CodeBlock], cnt[(int)Block::Html],
         cnt[(int)Block::Quote]);
  // first paragraphs, as mdt parses them (escape-artifact check)
  int shown = 0;
  for (auto& b : doc.blocks) {
    if (b.type != Block::Heading && b.type != Block::Paragraph) continue;
    std::string t = spans_plain_text(b.spans);
    if (t.empty()) continue;
    printf("  %-15s : %s\n", shown == 0 ? "first text" : "", t.substr(0, 60).c_str());
    if (shown == 0) {
      printf("  first chars     :");
      size_t i = 0;
      std::string first8;
      for (int k = 0; k < 8 && i < t.size(); k++) {
        uint32_t cp = utf8_next(t, i);
        printf(" U+%04X", cp);
        first8 = t.substr(0, i);  // bytes up to and including this character
      }
      printf("   (\"%s\")\n", first8.c_str());
    }
    if (++shown >= 3) break;
  }
  // terminal side
  const char* names[] = {"TERM", "TERM_PROGRAM", "TERM_PROGRAM_VERSION", "COLORTERM",
                         "WT_SESSION", "KITTY_WINDOW_ID", "TERM_SESSION_ID", "SHELL", "LANG"};
  std::string envs;
  for (auto* n : names) {
    const char* v = getenv(n);
    if (v && *v) envs += std::string(" ") + n + "=" + v;
  }
  printf("  terminal env    :%s\n", envs.empty() ? " (none set)" : envs.c_str());
  int tc = plat::stdout_is_tty() ? 1 : 0;
  int cols = 0, rows = 0;
  int pxw = 0, pxh = 0;
  plat::window_size(cols, rows, pxw, pxh);
  printf("  stdout is a tty : %s", tc ? "yes" : "no");
  if (cols > 0) printf(", %dx%d cells", cols, rows);
  printf("\n");
  if (tc && (pxw <= 0 || pxh <= 0)) {
    // No pixel geometry from the ioctl: ask the terminal (it answers CSI 16 t).
    // The tty has to be raw for the answer to arrive un-echoed and un-buffered.
    plat::raw_begin();
    Terminal probe;
    probe.caps.cols = cols > 0 ? cols : 80;
    probe.caps.rows = rows > 0 ? rows : 24;
    probe.query_capabilities(250);
    plat::raw_end();
    pxw = probe.caps.win_w;
    pxh = probe.caps.win_h;
    if (probe.caps.cell_w > 0) { pxw = probe.caps.cell_w * cols; pxh = probe.caps.cell_h * rows; }
  }
  if (cols > 0 && pxw > 0 && pxh > 0) {
    int cw = (int)std::lround((double)pxw / cols), ch = (int)std::lround((double)pxh / rows);
    printf("  cell size       : %dx%d px  (images are rasterised onto this grid and "
           "drawn 1:1)\n", cw, ch);
  } else {
    printf("  cell size       : unknown - formulas are placed at their own pixel size\n");
  }
  TermCaps caps;
  caps.cols = cols > 0 ? cols : 80;
  caps.rows = rows > 0 ? rows : 24;
  caps.term_name = getenv("TERM") ? getenv("TERM") : "";
  if (getenv("TERM_PROGRAM")) caps.term_program = getenv("TERM_PROGRAM");
  caps.gfx = GfxProto::None;
  const char* gfg = getenv("KITTY_WINDOW_ID") ? "kitty (KITTY_WINDOW_ID set)"
                   : (getenv("TERM") && std::string(getenv("TERM")).find("kitty") != std::string::npos)
                         ? "kitty (TERM)"
                   : (getenv("TERM_PROGRAM") && std::string(getenv("TERM_PROGRAM")) == "iTerm.app")
                         ? "iterm2 (TERM_PROGRAM=iTerm.app)"
                   : (getenv("TERM_PROGRAM") && std::string(getenv("TERM_PROGRAM")) == "WezTerm")
                         ? "sixel (TERM_PROGRAM=WezTerm)"
                         : "none - text fallback unless the terminal answers the startup probe";
  printf("  graphics (guess): %s\n", gfg);
  printf("  ... the real detection happens at start-up, in the terminal:\n");
  printf("      'mdt --list-caps' prints it.  'mdt --compat file.md' turns the\n");
  printf("      protocol probes and graphics off if a terminal misbehaves.\n");
  return true;
}

static bool render_dump(const AppOptions& opt, std::string* err) {
  std::string path = opt.files[0];
  std::string text;
  if (!read_source(path, text, err)) return false;
  if (path == "-") path = "<stdin>";
  int width = opt.width > 0 ? opt.width : 100;
  MdOptions mo;
  mo.math = (opt.math != "off");
  mo.entities = opt.entities == "force" ? MdOptions::EntForce : MdOptions::EntOff;
  mo.escapes = opt.escapes == "off"      ? MdOptions::EscOff
             : opt.escapes == "force"    ? MdOptions::EscForce
                                         : MdOptions::EscAuto;
  mo.loose = opt.loose == "off"      ? MdOptions::LooseOff
           : opt.loose == "on"       ? MdOptions::LooseOn
                                     : MdOptions::LooseAuto;
  MarkdownParser parser;
  MdDocument doc = parser.parse(text, mo);

  TermCaps caps;
  caps.cols = width;
  caps.rows = 200;
  caps.cell_w = 8; caps.cell_h = 16; caps.gfx = GfxProto::None;
  Terminal term;
  MathRenderer math;
  math.init(opt.math == "katex", nullptr);
  DocView view;
  view.set_math(&math);
  view.set_theme(theme_by_name(opt.theme));
  RenderOptions ro;
  ro.show_line_numbers = opt.show_line_numbers;
  ro.em_px_override = opt.font_px;
  ro.text_math = true;  // a text dump cannot contain bitmaps
  ro.lazy_metrics = false;
  ro.code_fit = opt.code_fit;
  ro.inline_images = false;  // a text dump needs no pixels (and no network)
  view.set_options(ro);
  view.set_width(width);
  view.set_document(std::move(doc), path, dir_name(abs_path(path)));
  int rows = view.total_rows();
  view.set_view_rows(rows);
  Screen scr;
  scr.init(width, rows, view.theme().fg, view.theme().bg);
  view.draw(scr, 0, 0, width, rows, 0);
  std::string out;
  for (int y = 0; y < rows; y++) {
    std::string line;
    for (int x = 0; x < width; x++) {
      const Cell& c = scr.at(x, y);
      if (c.cp == 0) continue;
      line += utf8_encode(c.cp);
    }
    line = rtrim(line);
    out += line + "\n";
  }
  if (opt.dump == "-" || opt.dump.empty()) {
    printf("%s", out.c_str());
  } else {
    FILE* f = plat::open_file(opt.dump, "wb");
    if (!f) { *err = "cannot write " + opt.dump; return false; }
    fwrite(out.data(), 1, out.size(), f);
    fclose(f);
    printf("wrote %s (%d rows x %d cols)\n", opt.dump.c_str(), rows, width);
  }
  return true;
}

// Renders the screen through the built-in font rasteriser -> PNG.
static bool render_screenshot(App* placeholder, const AppOptions& opt, std::string* err) {
  (void)placeholder;
  std::string path = opt.files[0];
  std::string text;
  if (!read_source(path, text, err)) return false;
  if (path == "-") path = "<stdin>";
  int cols = opt.width > 0 ? opt.width : 110;
  int rows = opt.height > 0 ? opt.height : 48;
  int cell_w = opt.cell_w > 0 ? opt.cell_w : 9;
  int cell_h = opt.cell_h > 0 ? opt.cell_h : 19;

  MdOptions mo;
  mo.math = (opt.math != "off");
  mo.entities = opt.entities == "force" ? MdOptions::EntForce : MdOptions::EntOff;
  mo.escapes = opt.escapes == "off"      ? MdOptions::EscOff
             : opt.escapes == "force"    ? MdOptions::EscForce
                                         : MdOptions::EscAuto;
  mo.loose = opt.loose == "off"      ? MdOptions::LooseOff
           : opt.loose == "on"       ? MdOptions::LooseOn
                                     : MdOptions::LooseAuto;
  MarkdownParser parser;
  MdDocument doc = parser.parse(text, mo);

  MathRenderer math;
  math.init(opt.math == "katex", nullptr);
  Terminal term;
  term.caps.cols = cols;
  term.caps.rows = rows;
  term.caps.cell_w = cell_w;
  term.caps.cell_h = cell_h;
  term.caps.win_w = cols * cell_w;
  term.caps.win_h = rows * cell_h;
  term.caps.gfx = GfxProto::None;
  Theme theme = theme_by_name(opt.theme);
  theme.syntax = opt.syntax;
  theme.flat_bg = !opt.panels;
  DocView view;
  view.set_terminal(&term);
  view.set_math(&math);
  view.set_theme(theme);
  view.set_offscreen(true);  // the images are composited into the PNG below
  RenderOptions ro;
  ro.show_line_numbers = opt.show_line_numbers;
  ro.em_px_override = opt.font_px;
  ro.text_math = (opt.math == "unicode");
  ro.lazy_metrics = false;  // a screenshot must be exact
  ro.code_fit = opt.code_fit;
  view.set_options(ro);
  view.set_width(cols);
  view.set_view_rows(rows - 1);
  view.set_document(std::move(doc), path, dir_name(abs_path(path)));

  Screen scr;
  scr.init(cols, rows, theme.fg, theme.bg);
  view.draw(scr, 0, 0, cols, rows - 1, 0);
  // status bar
  std::string title = path;
  size_t slash = title.find_last_of('/');
  if (slash != std::string::npos) title = title.substr(slash + 1);
  std::string bar = fmt(" %s   %d%%   [%s]  %s", title.c_str(), 100, gfx_name(term.caps.gfx),
                        math.has_js() ? "katex" : "unicode");
  scr.fill_rect(0, rows - 1, cols, 1, theme.status_bg);
  scr.put_str(0, rows - 1, bar, theme.status_fg, theme.status_bg);

  FontRaster fr;
  std::string ferr;
  if (!fr.init(cell_w, cell_h, &ferr)) { *err = ferr; return false; }
  Image img;
  img.w = cols * cell_w;
  img.h = rows * cell_h;
  img.rgba.assign((size_t)img.w * img.h * 4, 0);
  for (int y = 0; y < rows; y++)
    for (int x = 0; x < cols; x++) {
      const Cell& c = scr.at(x, y);
      fr.draw_cell(img, x, y, c.cp, c.fg, c.bg, c.attr);
    }
  // composite the document images (maths, pictures) on top
  if (getenv("MDT_DEBUG_CELLMATH"))
    fprintf(stderr, "[mdt] screenshot: %zu placed images, cell %dx%d px\n",
            view.images().size(), cell_w, cell_h);
  // MDT_SNAP_SUBCELL makes --screenshot composite the way a terminal that
  // cannot place an image at a pixel offset inside a cell has to (iTerm2,
  // sixel): the bitmap lands on its anchor cell.  The two renders must be
  // identical - tools/subcell_align_test.py checks exactly that.
  bool snap_subcell = getenv("MDT_SNAP_SUBCELL") != nullptr;
  for (const PlacedImage& im : view.images()) {
    if (getenv("MDT_DEBUG_CELLMATH"))
      fprintf(stderr, "[mdt]   image at (%d,%d) %dx%d px, off (%d,%d), rgba=%d\n", im.x, im.y,
              im.px_w, im.px_h, im.sub_x, im.sub_y, im.rgba ? 1 : 0);
    if (!im.rgba) continue;
    int ox = im.x * cell_w;
    int oy = im.y * cell_h + (snap_subcell ? 0 : im.sub_y);
    for (int y = 0; y < im.px_h; y++) {
      int yy = oy + y;
      if (yy < 0 || yy >= img.h) continue;
      for (int x = 0; x < im.px_w; x++) {
        int xx = ox + x;
        if (xx < 0 || xx >= img.w) continue;
        const uint8_t* sp = &(*im.rgba)[((size_t)y * im.px_w + x) * 4];
        uint8_t* dp = &img.rgba[((size_t)yy * img.w + xx) * 4];
        int a = sp[3];
        if (!a) continue;
        dp[0] = (uint8_t)((sp[0] * a + dp[0] * (255 - a)) / 255);
        dp[1] = (uint8_t)((sp[1] * a + dp[1] * (255 - a)) / 255);
        dp[2] = (uint8_t)((sp[2] * a + dp[2] * (255 - a)) / 255);
        dp[3] = 255;
      }
    }
  }
  auto png = png_encode(img.rgba.data(), img.w, img.h);
  std::string out = opt.screenshot;
  FILE* f = plat::open_file(out, "wb");
  if (!f) { *err = "cannot write " + out; return false; }
  fwrite(png.data(), 1, png.size(), f);
  fclose(f);
  printf("wrote %s (%dx%d px, font %s)\n", out.c_str(), img.w, img.h, fr.path().c_str());
  return true;
}

void print_usage() {
  printf(
      "mdt - read Markdown in the terminal, with maths and images\n"
      "\n"
      "usage: mdt [options] <file.md>\n"
      "\n"
      "graphics\n"
      "  --gfx=auto|kitty|iterm2|sixel|none   terminal graphics protocol (default: auto)\n"
      "  --math=katex|unicode|off             maths engine (default: katex, built in)\n"
      "  --no-color                           monochrome output\n"
      "  --line-numbers                       line numbers inside code blocks\n"
      "  --no-syntax                          plain code blocks (no highlighting)\n"
      "  --font-px=N                          override the derived maths font size\n"
      "\n"
      "appearance\n"
      "  --theme=dark|light|nord|monochrome   colour theme (default: dark)\n"
      "  --toc                                open the outline panel on start\n"
      "  --panels                             tinted backgrounds for code/quote/table\n"
      "  --terminal-bg                        keep the terminal's own background\n"
      "  --no-scrollbar                       hide the scrollbar\n"
      "  --entities=off|force                 repair a HTML-escaped source (default: off)\n"
      "  --escapes=auto|off|force             backslash-escaped sources (default: auto)\n"
      "  --loose=auto|off|on                  \"#标题\" and \"-项目\" without a space (default: auto)\n"
      "  --diag                               print what mdt sees of the file/terminal\n"
      "  --compat                             no graphics/sync/protocol probes (troubleshooting)\n"
      "  --code-fit                           code frames hug the code instead of the page\n"
      "\n"
      "non interactive\n"
      "  --dump[=file]                        print a text rendering (no graphics)\n"
      "  --screenshot=out.png                 render the screen to a PNG\n"
      "  --width=N --height=N                 size for --dump / --screenshot\n"
      "  --list-caps                          print detected terminal capabilities\n"
      "  -h, --help / -v, --version\n"
      "\n"
      "keys\n"
          "  wheel        scroll           click on the scrollbar   jump through the file\n"
      "  j k down up   scroll        space b      page        g G     top / bottom\n"
      "  J K           headings      Tab          outline     / n N   search\n"
      "  t             theme         r reload     o       open link      ? help\n"
      "  e             copy text     m            maths on/off    q quit\n");
}

}  // namespace mdt
