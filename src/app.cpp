// app.cpp : the interactive reader (event loop, outline panel, search, status bar).
#include "app.h"
#include "encoding.h"
#include "i18n.h"

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
// The three buttons of the link dialog: same keys in both languages, different
// words - and the dialog is measured around them, so they have to be swapped
// before anything is drawn.
static const char* const kLinkBtnEn[3] = {"c copy", "o open", "Esc close"};
static const char* const kLinkBtnZh[3] = {"c 复制", "o 浏览器打开", "Esc 关闭"};
static const char* const* link_buttons() { return language() == Lang::Zh ? kLinkBtnZh : kLinkBtnEn; }

// Geometry of the link dialog, shared by the painter, the image clipper and
// the mouse hit test so the buttons land exactly where they are drawn.
struct LinkDlg {
  int px = 0, py = 0, pw = 0, ph = 0, ulines = 1, by = 0;
  int bx[3] = {0, 0, 0}, bw[3] = {0, 0, 0};
};
static LinkDlg link_dialog_geom(int vcols, int vrows, const std::string& url) {
  LinkDlg g;
  int uw = (int)str_width(url);
  g.pw = std::min(vcols - 4, std::max(46, uw + 6));
  int inner = g.pw - 4;
  g.ulines = std::max(1, (uw + inner - 1) / inner);
  g.ph = std::min(vrows - 2, g.ulines + 6);
  g.px = (vcols - g.pw) / 2;
  g.py = (vrows - g.ph) / 2;
  g.by = g.py + 3 + g.ulines;
  int total = 4;  // two gaps of two cells
  for (int b = 0; b < 3; b++) {
    g.bw[b] = (int)str_width(link_buttons()[b]) + 2;
    total += g.bw[b];
  }
  int x = g.px + std::max(2, (g.pw - total) / 2);
  for (int b = 0; b < 3; b++) {
    g.bx[b] = x;
    x += g.bw[b] + 2;
  }
  return g;
}

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

// Reads a file (or stdin) and converts an 8-bit source to UTF-8.  Declared here
// because App::load_file() below reads through it as well.
static bool read_raw(const std::string& path, std::string& raw, std::string* err);
static bool read_source(const std::string& path, const std::string& want_encoding,
                        std::string& out, std::string* err, DecodedText* info = nullptr);

// ------------------------------------------------------------ app state -----
namespace {

struct OutlineItem {
  int block = 0;
  int level = 1;
  std::string text;
  std::vector<int> path;  // uids of the folding boxes around it, outer first
};

struct SearchHit {
  int block = 0;
  std::string ctx;
  std::vector<int> path;  // like OutlineItem::path
};

// every block of the document in reading order, with the top-level block it
// lives in and the folding boxes around it (nested directive content used to
// be invisible to the outline and the search)
struct Visit {
  int top = 0;
  const Block* b = nullptr;
  std::vector<int> path;
};
void collect_visits(const std::vector<Block>& v, int top, const std::vector<int>& path,
                    std::vector<Visit>* out) {
  for (size_t i = 0; i < v.size(); i++) {
    const Block& b = v[i];
    int my_top = top >= 0 ? top : (int)i;
    out->push_back(Visit{my_top, &b, path});
    std::vector<int> inner = path;
    if (b.type == Block::Directive && !b.dir_leaf && b.uid >= 0) inner.push_back(b.uid);
    for (const auto& nest : b.items) collect_visits(nest, my_top, inner, out);
  }
}

struct App {
  AppOptions opt;
  Terminal term;
  MathRenderer math;
  DocView view;
  Theme theme;
  std::vector<OutlineItem> outline;
  std::vector<SearchHit> search_hits;
  std::string status_msg;
  double status_until = 0;
  bool toc_open = false;
  bool help_open = false;
  bool link_open = false;          // the clicked-link dialog
  std::string link_url;
  int link_hover = -1;             // dialog button under the pointer
  bool search_mode = false;
  bool quit = false;
  std::string search_query;
  int toc_sel = 0, toc_off = 0, toc_panel_w = 0;
  std::string file;
  size_t file_index = 0;
  DecodedText decoding;             // what read_source() did with the bytes
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
  std::string text, why;
  if (!read_source(path, opt.encoding, text, &why, &decoding)) {
    if (err) *err = why.empty() ? (std::string(tr("cannot read file: ", "无法读取文件：")) + path) : why;
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
  std::vector<Visit> visits;
  collect_visits(view.doc().blocks, -1, {}, &visits);
  for (const Visit& v : visits) {
    if (v.b->type != Block::Heading) continue;
    OutlineItem it;
    it.block = v.top;
    it.level = v.b->level;
    it.text = spans_plain_text(v.b->spans);
    it.path = v.path;
    outline.push_back(it);
  }
  toc_sel = 0;
}

void App::do_search(const std::string& q) {
  search_hits.clear();
  if (q.empty()) return;
  std::string needle = to_lower(q);
  std::string buf;
  std::vector<Visit> visits;
  collect_visits(view.doc().blocks, -1, {}, &visits);
  for (const Visit& visit : visits) {
    std::string text;
    const Block& b = *visit.b;
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
      SearchHit hit;
      hit.block = visit.top;
      hit.ctx = collapse_ws(ctx);
      hit.path = visit.path;
      search_hits.push_back(hit);
    }
  }
  if (search_hits.empty()) {
    set_status(tr("no match: ", "没有匹配：") + q);
  } else {
    set_status(fmt(tr("%d match(es) for '%s'", "找到 %d 处匹配：'%s'"), (int)search_hits.size(),
                   q.c_str()));
    view.open_fold_path(search_hits[0].path);
    view.scroll_to(std::max(0, view.row_of_block(search_hits[0].block) - 2));
  }
}

void App::jump_to_next_hit(int dir) {
  if (search_hits.empty()) {
    set_status(tr("no active search", "没有正在进行的搜索"));
    return;
  }
  int cur_row = view.scroll_top();
  int cur = -1;
  for (size_t i = 0; i < search_hits.size(); i++) {
    int r = view.row_of_block(search_hits[i].block);
    if (r <= cur_row + 1) cur = (int)i;
  }
  int next;
  if (dir > 0) next = (cur + 1) % (int)search_hits.size();
  else next = (cur <= 0) ? (int)search_hits.size() - 1 : cur - 1;
  view.open_fold_path(search_hits[(size_t)next].path);
  view.scroll_to(std::max(0, view.row_of_block(search_hits[(size_t)next].block) - 2));
  set_status(fmt(tr("match %d/%d: %s", "第 %d/%d 处：%s"), next + 1, (int)search_hits.size(),
                 search_hits[(size_t)next].ctx.c_str()), 4);
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
    scr.put_str(px + 2, 0, tr("OUTLINE", "大纲"), theme.heading[1], pbg, A_BOLD);
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
    LinkDlg g = link_dialog_geom(vcols, vrows, link_url);
    RGB pbg = theme.status_bg;
    scr.fill_rect(g.px, g.py, g.pw, g.ph, pbg);
    scr.box_rounded(g.px, g.py, g.pw, g.ph, theme.code_frame, pbg);
    scr.put_str(g.px + 2, g.py + 1, tr(" Link ", " 链接 "), theme.muted, pbg);
    for (int k = 0; k < g.ulines; k++) {
      size_t b = (size_t)k * (g.pw - 4);
      scr.put_str(g.px + 2, g.py + 2 + k, link_url.substr(b, g.pw - 4), theme.link, pbg);
    }
    for (int b = 0; b < 3; b++)  // the keys double as clickable buttons
      if (b == link_hover)       // hovered: lit up instead of reversed
        scr.put_str(g.bx[b], g.by, std::string(" ") + link_buttons()[b] + " ", theme.link, pbg,
                    A_BOLD | A_UNDER);
      else
        scr.put_str(g.bx[b], g.by, std::string(" ") + link_buttons()[b] + " ", pbg, theme.link,
                    A_REVERSE);
  }
  if (help_open) {
    int pw = std::min(74, vcols - 4), ph = std::min(20, vrows - 2);
    int px = (vcols - pw) / 2, py = (vrows - ph) / 2;
    RGB pbg = theme.status_bg;
    scr.fill_rect(px, py, pw, ph, pbg);
    scr.box_rounded(px, py, pw, ph, theme.code_frame, pbg);
    static const char* const lines[2][17] = {
        {
            "mdt - markdown in the terminal",
            "",
            "  j / k, arrows     scroll one line",
            "  d / u             half page       space / b   page",
            "  g / G             top / bottom    J / K       next / prev heading",
            "  Tab               toggle outline  Enter       jump to heading",
            "  /                 search          n / N       next / prev match",
            "  o                 open first link in the browser",
            "  click a link      dialog with clickable copy / open / close buttons",
            "  click a \u25b8 title   fold or unfold a callout box ({open} starts open)",
            "  e                 export the document text to clipboard",
            "  r                 reload file     t           cycle theme",
            "  R                 force redraw    m           toggle maths rendering",
            "  ? / Esc           close this help q / Ctrl-C  quit",
            "",
            "  Images and formulas are drawn with the terminal graphics protocol",
            "  (kitty / iTerm2 / sixel); maths is typeset by the embedded TeX engine.",
        },
        {
            "mdt - 终端里的 Markdown 阅读器",
            "",
            "  j / k、方向键      上下滚动一行",
            "  d / u              半页            space / b   整页",
            "  g / G              开头 / 结尾     J / K       上/下一个标题",
            "  Tab                开关大纲        Enter       跳到标题",
            "  /                  搜索            n / N       下一个/上一个匹配",
            "  o                  用浏览器打开文档里的第一个链接",
            "  点链接             弹出「复制 / 打开 / 关闭」对话框",
            "  点 \u25b8 标题        折叠或展开提示框（{open} 表示默认展开）",
            "  e                  把全文复制到剪贴板",
            "  r                  重新读取文件    t           循环配色主题",
            "  R                  强制重绘        m           开关公式排版",
            "  ? / Esc            关闭本帮助      q / Ctrl-C  退出",
            "",
            "  图片和公式用终端图形协议画出来（kitty / iTerm2 / sixel），",
            "  公式由内嵌的 TeX 引擎排版。",
        },
    };
    const int n = 17;
    int li = language() == Lang::Zh ? 1 : 0;
    for (int i = 0; i < n && i + 2 < ph; i++)
      scr.put_str(px + 2, py + 1 + i, lines[li][i], i == 0 ? theme.heading[1] : theme.fg, pbg,
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
  if (decoding.converted) engine += " +" + decoding.encoding;   // 8-bit source, converted
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
    LinkDlg g = link_dialog_geom(vcols, vrows, link_url);
    clip_against(g.px, g.py, g.pw, g.ph);
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
    // plat::open_url execs the launcher directly (ShellExecuteW on Windows):
    // the URL is never given to a shell, so nothing in a document can turn a
    // link into a command.  Closed links are refused by the scheme check.
    bool ok = plat::open_url(url);
    if (!term.mode_intact()) term.reassert_mode();  // the browser may share the tty
    if (ok) set_status(tr("opened: ", "已打开：") + url);
    else set_status(tr("could not open the link: ", "打开失败（链接未打开）：") + url);
  };
  // apply_mouse folds an event into the view and says whether anything moved.
  auto apply_mouse = [&](const Terminal::KeyEvent& e) -> bool {
    if (e.wheel_up) {
      int s0 = view.scroll_top(); view.scroll_by(-3); return view.scroll_top() != s0;
    }
    if (e.wheel_down) {
      int s0 = view.scroll_top(); view.scroll_by(3); return view.scroll_top() != s0;
    }
    if (e.motion && !e.drag) {  // pointer move: hover feedback on the buttons
      if (toc_open && toc_panel_w > 0 && e.my >= 1 &&
          e.mx >= term.caps.cols - toc_panel_w) {  // the outline panel owns it
        int idx = toc_off + (e.my - 1);
        if (idx >= 0 && idx < (int)outline.size() && idx != toc_sel) {
          toc_sel = idx;
          return true;
        }
        return false;
      }
      int h = -1;
      if (link_open && !link_url.empty()) {
        LinkDlg g = link_dialog_geom(term.caps.cols, view_rows(), link_url);
        if (e.my == g.by)
          for (int b = 0; b < 3; b++)
            if (e.mx >= g.bx[b] && e.mx < g.bx[b] + g.bw[b]) h = b;
      }
      if (h != link_hover) {
        link_hover = h;
        view.set_hover_fold(-1);
        return true;
      }
      int hf = link_open ? -1 : view.fold_title_at(e.mx, e.my);
      return view.set_hover_fold(hf);
    }
    if (e.release) { scroll_drag = false; return false; }
    if (!e.drag) {
      if (link_open) {
        LinkDlg g = link_dialog_geom(term.caps.cols, view_rows(), link_url);
        if (e.my == g.by)
          for (int b = 0; b < 3; b++)
            if (e.mx >= g.bx[b] && e.mx < g.bx[b] + g.bw[b]) {
              if (b == 0) {
                term.set_clipboard(link_url);
                set_status(tr("copied: ", "已复制：") + link_url, 2.5);
              } else if (b == 1) {
                open_url_in_browser(link_url);
                link_open = false;
              } else {
                link_open = false;
              }
              return true;
            }
        link_open = false;  // clicking outside dismisses
        return true;
      }
      std::string url;
      if (view.link_at(e.mx, e.my, url)) {
        link_url = url;
        link_open = true;
        return true;
      }
      if (view.toggle_box_at(e.mx, e.my)) return true;  // fold a callout box
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
        view.open_fold_path(outline[(size_t)idx].path);
        view.scroll_to(std::max(0, view.row_of_block(outline[(size_t)idx].block) - 1));
        return true;
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
      if (pending) set_status(tr("loading images…", "正在加载图片…"), 3600);
      else set_status(tr("images loaded", "图片加载完成"), 1.5);
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
        set_status(tr("copied: ", "已复制：") + link_url, 2.5);
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
      case Terminal::K_TAB: toc_open = !toc_open; break;  // parser reports a
        // Special for \t, so the char case below never sees it
      case Terminal::K_ENTER:
        if (toc_open && !outline.empty()) {  // the help promises it since v0.1
          view.open_fold_path(outline[(size_t)toc_sel].path);
          view.scroll_to(
              std::max(0, view.row_of_block(outline[(size_t)toc_sel].block) - 1));
        }
        break;
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
        const OutlineItem* best = nullptr;
        for (auto& it : outline) { if (it.block > cur) { best = &it; break; } }
        if (best) {
          view.open_fold_path(best->path);
          view.scroll_to(std::max(0, view.row_of_block(best->block) - 1));
        } else set_status(tr("last heading", "已经是最后一个标题"));
        break;
      }
      case 'K': {
        int cur = view.block_at_row(view.scroll_top());
        const OutlineItem* best = nullptr;
        for (auto& it : outline) { if (it.block < cur) best = &it; }
        if (best) {
          view.open_fold_path(best->path);
          view.scroll_to(std::max(0, view.row_of_block(best->block) - 1));
        } else set_status(tr("first heading", "已经是第一个标题"));
        break;
      }
      case '/': search_mode = true; search_query.clear(); break;
      case 'n': jump_to_next_hit(1); break;
      case 'N': jump_to_next_hit(-1); break;
      case 'o': {
        const auto& links = view.doc().links;
        if (links.empty()) set_status(tr("no links in this document", "这份文档里没有链接"));
        else open_url_in_browser(links[0].url);
        break;
      }
      case 'e': {
        std::string text = view.plain_text();
        term.set_clipboard(text);
        set_status(fmt(tr("copied %d bytes to the clipboard", "已把 %d 字节复制到剪贴板"),
                       (int)text.size()));
        break;
      }
      case 'r': {
        std::string err;
        if (load_file(file, &err)) set_status(tr("reloaded", "已重新读取"));
        else set_status(err);
        break;
      }
      case 't': {
        static const char* names[] = {"dark", "light", "nord", "monochrome"};
        static int ti = 0;
        ti = (ti + 1) % 4;
        theme = theme_by_name(names[ti]);
        view.set_theme(theme);
        set_status(std::string(tr("theme: ", "主题：")) + names[ti]);
        break;
      }
      case 'm': {
        RenderOptions ro = view.options();
        ro.text_math = !ro.text_math;
        view.set_options(ro);
        view.relayout();
        set_status(ro.text_math ? tr("maths: plain Unicode (press m to typeset)",
                                     "公式：纯 Unicode（按 m 排版）")
                                : tr("maths: typeset via the embedded TeX engine",
                                     "公式：用内嵌的 TeX 引擎排版"));
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

// Reads a file, or stdin when the path is "-", and converts an 8-bit source
// (GBK and friends) to UTF-8 before anything else looks at it.
static bool read_raw(const std::string& path, std::string& raw, std::string* err) {
  if (path == "-") {
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), stdin)) > 0) raw.append(buf, n);
    if (raw.empty() && !feof(stdin)) return false;
    return true;
  }
  // A directory used to be "read" successfully and silently render nothing
  // (exit status 0), which reads like the file was empty rather than wrong.
  std::string why;
  if (!read_file(path, raw, &why)) {
    if (err)
      *err = plat::is_directory(path)
                 ? (path + tr(" is a directory, not a file", " 是目录，不是文件"))
                 : why;
    return false;
  }
  return true;
}

static bool read_source(const std::string& path, const std::string& want_encoding,
                        std::string& out, std::string* err, DecodedText* info) {
  std::string raw;
  if (!read_raw(path, raw, err)) return false;
  DecodedText dec = decode_text(raw, want_encoding);
  out = std::move(dec.text);
  if (info) *info = dec;
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
    fprintf(stderr, tr("mdt: no input file\n\n", "mdt: 没有输入文件\n\n"));
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
  if (!read_source(path, opts.encoding, text, &rerr)) {
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
    fprintf(stderr, tr("mdt: maths engine unavailable (%s), using unicode\n",
                       "mdt: 公式引擎不可用（%s），改用 Unicode\n"),
            err.c_str());

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
  // fmt() is a printf wrapper: building the middle field first keeps it a
  // const char* (handing it a std::string is undefined behaviour and printed
  // garbage in the status bar).
  std::string encoding_note;
  if (app.decoding.converted)
    encoding_note = fmt(tr("  |  %s source converted to UTF-8", "  |  已把 %s 源文件转成 UTF-8"),
                        app.decoding.encoding.c_str());
  else if (!app.decoding.note.empty())
    encoding_note = fmt("  |  %s", app.decoding.note.c_str());
  app.set_status(fmt(tr("%s%s%s  |  ? for help", "%s%s%s  |  ? 查看帮助"),
                     gfx_name(app.term.caps.gfx),
                     encoding_note.c_str(),
                     app.view.doc().entities_unescaped
                          ? tr("  |  decoded HTML entities", "  |  已解码 HTML 实体")
                          : (app.view.doc().backslashes_removed > 0
                                 ? tr("  |  removed backslash escapes", "  |  已去掉反斜杠转义")
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
  std::string raw;
  if (!path.empty() && !read_raw(path, raw, err)) return false;
  DecodedText dec = decode_text(raw, opt.encoding);
  std::string text = dec.text;

  printf("mdt diag\n");
  printf("  file            : %s (%zu bytes)\n", path.empty() ? "-" : path.c_str(), raw.size());
  // encoding
  std::string enc = "utf-8, no BOM";
  std::string body = raw;
  if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB &&
      (unsigned char)text[2] == 0xBF) { enc = "utf-8 with BOM"; body = text.substr(3); }
  else if (text.size() >= 2 && (unsigned char)text[0] == 0xFF && (unsigned char)text[1] == 0xFE)
    enc = "utf-16le (BOM) - NOT SUPPORTED, save the file as UTF-8";
  else if (raw.size() >= 2 && (unsigned char)raw[0] == 0xFE && (unsigned char)raw[1] == 0xFF)
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
  if (dec.converted)
    printf("                    -> converted from %s to UTF-8 for rendering\n",
           dec.encoding.c_str());
  else if (!dec.note.empty())
    printf("                    -> left as it is: %s\n", dec.note.c_str());
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
  if (doc.front_matter_lines > 0 || doc.link_defs_found > 0)
    printf("  skipped         : %d front matter line(s), %d link reference definition(s)\n",
           doc.front_matter_lines, doc.link_defs_found);
  if (doc.footnotes_defined > 0 || !doc.footnote_order.empty() || doc.definition_items > 0)
    printf("  markdown        : %d footnote(s) defined, %d referenced, %d definition list item(s)\n",
           doc.footnotes_defined, (int)doc.footnote_order.size(), doc.definition_items);
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
  printf("  config          : %s\n", opt.config_path.empty() ? "(none)" : opt.config_path.c_str());
  printf("  language        : %s (%s)\n", lang_name(language()), language_source().c_str());
  printf("  theme           : %s%s\n", opt.theme.c_str(), opt.no_color ? "  (NO_COLOR/--no-color)" : "");
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
    // What the document costs.  Laid out the way the reader lays it out (a few
  // hundred milliseconds on a multi-megabyte file), because "it uses a lot of
  // memory" is only actionable with numbers next to it.
  {
    size_t b_sz = 0, s_sz = 0, l_sz = 0, r_sz = 0, c_sz = 0;
    DocView::struct_sizes(&b_sz, &s_sz, &l_sz, &r_sz, &c_sz);
    printf("  block struct    : %zu bytes (span %zu, line %zu, run %zu, table cell %zu)\n", b_sz,
           s_sz, l_sz, r_sz, c_sz);
    DocView probe;
    RenderOptions pro;
    pro.text_math = true;
    pro.inline_images = false;
    pro.fold_ui = false;
    probe.set_options(pro);
    probe.set_width(opt.width > 0 ? opt.width : 100);
    probe.set_document(std::move(doc), path, dir_name(abs_path(path)));
    probe.relayout();
    DocView::LayoutStats st = probe.layout_stats();
    printf("  layout          : %d rows, %zu lines, %zu runs, %zu table cells\n", st.rows, st.lines,
           st.runs, st.cells);
    double rss = peak_rss_mb();
    if (rss > 0)
      printf("  memory          : %.1f MB peak RSS for the whole document\n", rss);
    else
      printf("  memory          : (this platform does not report the peak)\n");
  }

return true;
}

static bool render_dump(const AppOptions& opt, std::string* err) {
  std::string path = opt.files[0];
  std::string text;
  if (!read_source(path, opt.encoding, text, err)) return false;
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
  ro.fold_ui = false;   // ... and it must carry the full content of every box
  ro.lazy_metrics = false;
  ro.code_fit = opt.code_fit;
  ro.inline_images = false;  // a text dump needs no pixels (and no network)
  view.set_options(ro);
  view.set_width(width);
  view.set_document(std::move(doc), path, dir_name(abs_path(path)));
  int rows = view.total_rows();
  view.set_view_rows(rows);
  // A screen holds two cell buffers (current + previous frame), so one giant
  // screen the height of the whole document cost 2 * 12 bytes * width * rows:
  // 190 MB for a 4 MB file.  Draw it in viewport-sized slices instead and read
  // each slice out - the cells for a row do not depend on the rows around them.
  const int kSlice = 512;
  Screen scr;
  scr.init(width, std::min(rows, kSlice) > 0 ? std::min(rows, kSlice) : 1, view.theme().fg,
           view.theme().bg);
  std::string out;
  for (int y0 = 0; y0 < rows; y0 += kSlice) {
    int h = std::min(kSlice, rows - y0);
    scr.resize(width, h);
    scr.clear();
    view.draw(scr, 0, 0, width, h, y0);
    for (int y = 0; y < h; y++) {
      std::string line;
      for (int x = 0; x < width; x++) {
        const Cell& c = scr.at(x, y);
        if (c.cp == 0) continue;
        line += utf8_encode(c.cp);
      }
      line = rtrim(line);
      out += line + "\n";
    }
  }
  if (opt.dump == "-" || opt.dump.empty()) {
    printf("%s", out.c_str());
  } else {
    FILE* f = plat::open_file(opt.dump, "wb");
    if (!f) { *err = std::string(tr("cannot write ", "无法写入 ")) + opt.dump; return false; }
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
  if (!read_source(path, opt.encoding, text, err)) return false;
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
  if (!f) { *err = std::string(tr("cannot write ", "无法写入 ")) + out; return false; }
  fwrite(png.data(), 1, png.size(), f);
  fclose(f);
  printf("wrote %s (%dx%d px, font %s)\n", out.c_str(), img.w, img.h, fr.path().c_str());
  return true;
}

void print_usage() {
  fputs(
      tr(
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
      "  --encoding=auto|utf-8|gbk|big5|cp1252|off\n"
      "                                       how to read a non-UTF-8 (ANSI) file (default: auto)\n"
      "  --entities=off|force                 repair a HTML-escaped source (default: off)\n"
      "  --escapes=auto|off|force             backslash-escaped sources (default: auto)\n"
      "  --loose=auto|off|on                  \"#title\" and \"-item\" without a space (default: auto)\n"
      "  --diag                               print what mdt sees of the file/terminal\n"
      "  --compat                             no graphics/sync/protocol probes (troubleshooting)\n"
      "  --code-fit                           code frames hug the code instead of the page\n"
      "\n"
      "language\n"
      "  --lang=auto|en|zh                    interface language, also \"lang = zh\" in the\n"
      "                                       config file (default: auto, follows LANG)\n"
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
      "  e             copy text     m            maths on/off    q quit\n"
      ,
      "mdt - 在终端里阅读 Markdown，公式和图片都是画出来的\n"
      "\n"
      "用法: mdt [选项] <文件.md>\n"
      "\n"
      "图形\n"
      "  --gfx=auto|kitty|iterm2|sixel|none   终端图形协议（默认 auto）\n"
      "  --math=katex|unicode|off             公式引擎（默认 katex，内嵌）\n"
      "  --no-color                           单色输出\n"
      "  --line-numbers                       代码块里显示行号\n"
      "  --no-syntax                          代码块不做语法高亮\n"
      "  --font-px=N                          覆盖推导出来的公式字号\n"
      "\n"
      "外观\n"
      "  --theme=dark|light|nord|monochrome   配色主题（默认 dark）\n"
      "  --toc                                启动时展开大纲面板\n"
      "  --panels                             代码/引用/表格用底色块\n"
      "  --terminal-bg                        保留终端自己的背景色\n"
      "  --no-scrollbar                       不显示滚动条\n"
      "  --encoding=auto|utf-8|gbk|big5|cp1252|off\n"
      "                                       怎么读非 UTF-8（ANSI）文件（默认 auto）\n"
      "  --entities=off|force                 修复被 HTML 转义的源文件（默认 off）\n"
      "  --escapes=auto|off|force             反斜杠转义的源文件（默认 auto）\n"
      "  --loose=auto|off|on                  \"#标题\"、\"-项目\" 不带空格（默认 auto）\n"
      "  --diag                               打印 mdt 看到的文件/终端信息\n"
      "  --compat                             不做图形/同步/协议探测（排障用）\n"
      "  --code-fit                           代码框贴合代码，而不是整页宽\n"
      "\n"
      "语言\n"
      "  --lang=auto|en|zh                    界面语言，也可以写进配置文件：\n"
      "                                       lang = zh（默认 auto，跟随 LANG）\n"
      "\n"
      "非交互\n"
      "  --dump[=文件]                        输出纯文本渲染结果（不带图形）\n"
      "  --screenshot=out.png                 把屏幕渲染成 PNG\n"
      "  --width=N --height=N                 --dump / --screenshot 的尺寸\n"
      "  --list-caps                          打印探测到的终端能力\n"
      "  -h, --help / -v, --version\n"
      "\n"
      "按键\n"
      "  滚轮         滚动          点滚动条     在文件里跳转\n"
      "  j k 下 上    滚动        space b      翻页        g G     开头 / 结尾\n"
      "  J K          标题        Tab          大纲        / n N   搜索\n"
      "  t            主题        r 重新读取    o           打开链接      ? 帮助\n"
      "  e            复制文本    m            公式开关    q 退出\n"
      ),
      stdout);
}

}  // namespace mdt
