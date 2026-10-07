// render.h : document layout + drawing (text grid) with graphics overlays.
#pragma once
#include <map>
#include <tuple>
#include <memory>
#include <string>
#include <vector>

#include "image.h"
#include "math.h"
#include "md.h"
#include "svg.h"
#include "term.h"
#include "util.h"

namespace mdt {

struct Theme {
  RGB bg{24, 26, 31};
  RGB fg{222, 226, 232};
  RGB muted{130, 137, 151};
  RGB heading[7] = {{0, 0, 0},
                    {126, 197, 255},   // h1
                    {139, 210, 214},   // h2
                    {164, 208, 254},   // h3
                    {198, 208, 224},   // h4
                    {186, 190, 200},   // h5
                    {172, 176, 186}};  // h6
  RGB link{102, 170, 245};
  RGB code_fg{222, 178, 128};
  RGB code_bg{32, 35, 42};
  RGB code_frame{58, 63, 74};
  RGB quote_fg{168, 176, 190};
  RGB quote_bar{94, 129, 172};
  RGB bullet{126, 197, 255};
  RGB rule{66, 72, 86};
  RGB table_border{70, 77, 92};
  RGB table_header_bg{38, 42, 53};
  RGB table_header_fg{198, 214, 235};
  RGB math_fg{235, 219, 178};
  RGB status_bg{32, 35, 42};
  RGB status_fg{158, 166, 180};
  RGB marker_bg{58, 63, 74};
  RGB image_frame{60, 66, 80};
  bool flat_bg = true;     // document panels share the page background
  bool syntax = true;      // light syntax highlighting inside code blocks
  bool sprites = true;     // draw block characters as graphics sprites
  double baseline = 0.76;  // font baseline as a fraction of the cell height
};

// Minimal highlighter for common languages (keywords/strings/comments/numbers).
struct CodeHighlight {
  std::vector<std::vector<RGB>> colors;  // per line, per code point (0,0,0 = default)
  bool any = false;
};
CodeHighlight highlight_code(const std::string& code, const std::string& lang, const Theme& th);

struct RenderOptions {
  int content_margin = 1;       // blank columns left/right
  int paragraph_gap = 1;        // blank rows between paragraphs
  int max_image_rows_pct = 60;  // image height cap (% of the viewport)
  bool show_line_numbers = false;
  bool inline_images = true;
  bool remote_images = true;   // fetch http(s):// pictures at all
  bool async_images = false;   // ... on a worker thread (interactive reading)
  bool text_math = false;       // force the plain-Unicode maths fallback
  bool lazy_metrics = true;     // typeset only formulas that are on screen
  int em_px_override = 0;       // 0 = derive from the cell size
  bool code_fit = false;        // code frames hug the code instead of the page
};

class DocView {
 public:
  void set_terminal(Terminal* t) { term_ = t; }
  void set_math(MathRenderer* m) { math_ = m; }
  void set_theme(const Theme& th) { theme_ = th; }
  void set_options(const RenderOptions& o) { opt_ = o; lazy_metrics_ = o.lazy_metrics; }
  const RenderOptions& options() const { return opt_; }
  void set_document(MdDocument doc, const std::string& path, const std::string& base_dir);
  const MdDocument& doc() const { return doc_; }
  const std::string& path() const { return path_; }
  const Theme& theme() const { return theme_; }

  void set_width(int cols);
  int width() const { return cols_; }
  void relayout();
  // True when a formula measured during drawing disagrees with the layout
  // estimate; the caller should relayout once (all metrics are cached by then).
  bool layout_dirty() const { return layout_dirty_; }
  void set_lazy_metrics(bool on) { lazy_metrics_ = on; }
  // Off-screen rendering (--screenshot): there is no terminal to ask, and the
  // caller composites the images itself, so bitmaps are always built.
  void set_offscreen(bool on) { offscreen_ = on; }
  int cell_w() const;
  int cell_h() const;

  int total_rows() const { return total_rows_; }
  int block_count() const { return (int)doc_.blocks.size(); }
  int block_at_row(int row) const;
  int row_of_block(int block) const;
  const Block& block(int i) const { return doc_.blocks[(size_t)i]; }

  void scroll_to(int row);
  void scroll_by(int dy);
  void scroll_page(int dir);
  void scroll_home();
  void scroll_end();
  int scroll_top() const { return scroll_; }
  int view_rows() const { return view_rows_; }
  void set_view_rows(int r) { view_rows_ = r; }

  // Renders the visible part into `scr` and fills images() with placements.
  void draw(Screen& scr, int x0, int y0, int w, int h, int scroll_row);
  const std::vector<PlacedImage>& images() const { return images_; }
  // Downloads that finished since the last call; the event loop then relayouts
  // and redraws.  Never blocks.
  bool poll_images();
  bool images_pending() const { return loader_.busy(); }
  // Forget downloaded pictures (the "reload images" key).
  void clear_image_cache() { loader_.clear(); }

  std::string plain_text() const;

  // cell geometry helpers (used by the app for hit testing)
  int content_cols() const { return content_w_; }

 private:
  struct Run {
    std::string text;
    Span style;
    int x = 0, width = 0;
    int img = -1;  // index into assets_ for maths / inline images
    int rows = 1;
  };
  struct TCell {
    int x = 0, width = 0;
    std::string text;
    Align align = Align::Left;
    bool header = false;
    int pad = 1;
    // Typeset formulas inside the cell.  The cell keeps showing the Unicode
    // transcription (so terminals without graphics, and sixel, still read),
    // and the bitmap - exactly as wide as that transcription and one cell
    // tall, with the page colour behind the ink - is drawn over it.
    struct Piece {
      int col = 0;    // column inside the cell where it starts
      int cols = 1;   // width in cells (= width of the transcription)
      std::string tex;
      bool display = false;
    };
    std::vector<Piece> pieces;
  };
  struct Line {
    enum Kind { Text, Code, Rule, Table, Image } kind = Text;
    std::vector<Run> runs;
    std::vector<TCell> cells;   // kind == Table
    int row = 0;                // row offset inside the block
    std::vector<int> bars;      // blockquote bar columns, outermost first
    int img = -1;               // kind == Image
    int img_rows = 1;
    // How many terminal rows this line occupies.  A text line that carries an
    // inline formula or picture taller than one row has to reserve the extra
    // rows, otherwise the bitmap is drawn over whatever the layout put below
    // it (a multi-line aligned block used to cover the next paragraph).
    int rows = 1;
    int code_index = -1;        // source line index for code blocks
    int x = 0;                  // extra column offset (blocks inside list items)
    std::string code_text;      // wrapped code line (code blocks)
    int code_col = 0;           // byte offset of this segment inside the source line
    std::string marker;         // dimmed prefix drawn before the text (heading "#")
    int marker_w = 0;
  };
  struct BlockLayout {
    int block = 0;
    int row = 0;
    int rows = 1;
    std::vector<Line> lines;
  };
  struct ImageAsset {
    std::string source;
    int cols = 0, rows = 0;
    int px_w = 0, px_h = 0;
    double baseline_px = 0;
    std::vector<uint8_t> rgba;
    std::vector<uint8_t> png;
    bool failed = false;
    bool pending = false;        // size estimated, not typeset yet
    bool is_display = false;
    int max_w = 0, max_h = 0;
    std::string err;
    double natural_w = 0, natural_h = 0;
  };

  Terminal* term_ = nullptr;
  MathRenderer* math_ = nullptr;
  MdDocument doc_;
  Theme theme_;
  RenderOptions opt_;
  std::string path_, base_dir_;
  int cols_ = 80, view_rows_ = 24, content_w_ = 78;
  int total_rows_ = 0, scroll_ = 0;
  bool lazy_metrics_ = true;
  bool offscreen_ = false;
  bool layout_dirty_ = false;
  std::vector<BlockLayout> layout_;
  std::vector<PlacedImage> images_;
  mutable RemoteImageLoader loader_;   // remote pictures, fetched off-thread
  // Keeps the cell-formula bitmaps alive while the frame is being emitted:
  // PlacedImage holds raw pointers into them.
  std::vector<std::shared_ptr<ImageAsset>> cell_assets_;
  std::vector<std::shared_ptr<ImageAsset>> assets_;
  std::map<std::string, std::shared_ptr<ImageAsset>> asset_map_;

  int em_px() const;
  int cell_baseline_px() const;
  std::shared_ptr<ImageAsset> asset_for_math(const std::string& tex, bool display, int max_w_px,
                                             int max_h_px, bool allow_pending = true,
                                             bool force = false);
  void estimate_math_size(const std::string& tex, bool display, int& w, int& h);
  std::shared_ptr<ImageAsset> asset_for_image(const std::string& src, int max_w_px, int max_h_px,
                                              bool inline_mode);
  std::shared_ptr<ImageAsset> resolve_asset(const ImageAsset& a);
  std::shared_ptr<ImageAsset> make_canvas(Image& img, int cols, int rows, int target_w_px,
                                          int target_h_px, double baseline_px,
                                          int ink_y = -1000000);
  // The part of a bitmap that is visible when the viewport cuts it: rows
  // [clip_top, clip_top+vis_h) on a fresh grid-aligned canvas.  Cached, because
  // scrolling revisits the same cut over and over.
  std::shared_ptr<ImageAsset> sub_variant(const ImageAsset& a, int clip_top, int vis_h);
  std::map<std::tuple<const ImageAsset*, int, int>, std::shared_ptr<ImageAsset>> clip_cache_;

  void layout_blocks();
  // Lays a table out into bl, starting at column `indent` with `avail` columns
  // available.  Also used for tables nested in list items and quotes.
  void layout_table(const Block& b, int indent, int avail, BlockLayout& bl, int& row);
  void place_cell_math(Screen* scr, int cell_x, int cell_w, int yy, const TCell& tc, int cw,
                       int chh, int scroll_row, RGB bg);
  // Bitmap for a formula inside a table cell (see TCell::Piece).
  std::shared_ptr<ImageAsset> cell_math_asset(const TCell::Piece& pc, int cw, int chh, RGB bg);
  // Code blocks: the frame hugs the code instead of spanning the page, and long
  // lines are wrapped rather than cut off at the frame edge.
  int code_frame_width(const Block& b) const;
  static void wrap_code_line(const std::string& line, int width,
                             std::vector<std::pair<std::string, int>>& out);
  int layout_paragraph(const std::vector<Span>& spans, int indent, int width, int row_start,
                       const Span& base, std::vector<Line>& out);
  // Lines for one block inside a blockquote (recurses for nested quotes: the
  // nested block gets one more bar and two more columns of indent).
  void quote_lines(std::vector<Line>& out, const Block& qb, int indent, std::vector<int> bars);
  void build_lines(const std::vector<Span>& spans, int indent, int width, const Span& base,
                   std::vector<Line>& out);
  void draw_line(Screen& scr, int x0, int y0, int w, const Line& line, int sy);
  void draw_code_block(Screen& scr, int x0, int y0, int w, int h, const BlockLayout& bl);
  void draw_table(Screen& scr, int x0, int y0, int w, int h, const BlockLayout& bl);
  void draw_list_item(Screen& scr, int x0, int y0, int w, int h, const BlockLayout& bl);
};

}  // namespace mdt
