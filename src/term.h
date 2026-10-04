// term.h : terminal capability probing, cell framebuffer, graphics protocols.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "util.h"

namespace mdt {

enum class GfxProto { None, Kitty, Iterm2, Sixel };

const char* gfx_name(GfxProto p);

struct TermCaps {
  int cols = 80, rows = 24;      // text area in cells
  int cell_w = 0, cell_h = 0;    // cell size in px (0 = unknown)
  int win_w = 0, win_h = 0;      // window size in px (0 = unknown)
  GfxProto gfx = GfxProto::None;
  bool truecolor = true;
  bool kitty_keyboard = false;
  bool sync_output = false;      // supports DEC 2026
  bool sixel = false;
  std::string term_name, term_program;

  // Pixel geometry with sane fallbacks (assumes a 1:2 cell aspect if unknown).
  int px_cell_w() const { return cell_w > 0 ? cell_w : 8; }
  int px_cell_h() const { return cell_h > 0 ? cell_h : 16; }
  int px_width() const { return win_w > 0 ? win_w : cols * px_cell_w(); }
  int px_height() const { return win_h > 0 ? win_h : rows * px_cell_h(); }
  bool can_show_images() const { return gfx != GfxProto::None; }
};

// ------------------------------------------------------------------ Screen --
// A cell-based framebuffer. Attributes are bit flags; colors are RGB.
enum Attr : uint8_t { A_BOLD = 1, A_DIM = 2, A_ITALIC = 4, A_UNDER = 8, A_STRIKE = 16, A_REVERSE = 32 };

struct Cell {
  uint32_t cp = ' ';
  RGB fg{220, 223, 228};
  RGB bg{24, 26, 31};
  uint8_t attr = 0;
  bool operator==(const Cell& o) const {
    return cp == o.cp && fg == o.fg && bg == o.bg && attr == o.attr;
  }
};

class Screen {
 public:
  void init(int w, int h, RGB fg, RGB bg);
  void resize(int w, int h);
  void clear();
  int width() const { return w_; }
  int height() const { return h_; }

  void put(int x, int y, uint32_t cp, RGB fg, RGB bg, uint8_t attr = 0);
  void put_str(int x, int y, const std::string& utf8, RGB fg, RGB bg, uint8_t attr = 0);
  int  fill_row(int x, int y, int count, uint32_t cp, RGB fg, RGB bg, uint8_t attr = 0);
  void fill_rect(int x, int y, int w, int h, RGB bg, uint32_t cp = ' ');
  void hline(int x, int y, int w, uint32_t cp, RGB fg, RGB bg, uint8_t attr = 0);
  void vline(int x, int y, int h, uint32_t cp, RGB fg, RGB bg);
  void box_rounded(int x, int y, int w, int h, RGB fg, RGB bg);
  const Cell& at(int x, int y) const;
  Cell& at(int x, int y);

  // Emits a full frame; only changed cells are written (diffing vs. last frame).
  std::string render_diff();
  std::string render_full();
  void invalidate() { force_full_ = true; }
  void set_cursor_hidden(bool hidden) { cursor_hidden_ = hidden; }

 private:
  std::string emit_sgr(const Cell& c, const Cell& prev);
  int w_ = 0, h_ = 0;
  std::vector<Cell> cells_, prev_;
  bool force_full_ = true;
  bool cursor_hidden_ = true;
  RGB def_fg_, def_bg_;
};

// ------------------------------------------------------------------ Images --
// A placed image in the cell grid; `key` identifies the content (cache key).
struct PlacedImage {
  int x = 0, y = 0;        // top-left cell
  int cols = 1, rows = 1;  // size in cells
  std::string key;         // content id used for caching
  int px_w = 0, px_h = 0;
  int sub_x = 0, sub_y = 0;                    // sub-cell pixel offset (kitty)
  const std::vector<uint8_t>* png = nullptr;   // encoded PNG bytes (kitty / iTerm2)
  const std::vector<uint8_t>* rgba = nullptr;  // raw RGBA, px_w*px_h*4 (sixel)
};

class Terminal {
 public:
  TermCaps caps;
  RGB image_bg{24, 26, 31};  // colour used to composite transparent images

  bool init(const std::string& gfx_override);  // raw mode + alt screen + probing
  void shutdown();
  bool ok() const { return initialized_; }

  void clear_images();
  // Emit placed images (called after the text frame has been written).
  std::string emit_images(const std::vector<PlacedImage>& imgs);
  void drop_image_cache();

  // Non-blocking read of pending bytes (input + resize); returns false on EOF.
  bool poll_input(std::string& out, int timeout_ms);

  bool resized();
  void handle_resize();
  int  read_key(int timeout_ms);  // 0 = timeout, -1 = EOF

  // Low-level helpers
  void set_title(const std::string& t);
  void set_clipboard(const std::string& s);
  std::string enter_alt_screen();
  std::string leave_alt_screen();

  // Direct escape output (bypasses the framebuffer).
  void write_raw(const std::string& s);

  struct KeyEvent {
    enum Type { None, Char, Special, Mouse } type = None;
    int code = 0;      // Special: see Key enum; Mouse: button
    char ch = 0;
    int mx = 0, my = 0;  // mouse cell coords
    bool wheel_up = false, wheel_down = false;
  };
  enum Key {
    K_UP = 1000, K_DOWN, K_LEFT, K_RIGHT, K_PGUP, K_PGDN, K_HOME, K_END, K_DEL,
    K_BACKSPACE, K_ENTER, K_TAB, K_ESC, K_F1, K_SHIFT_UP, K_SHIFT_DOWN, K_CTRL_UP, K_CTRL_DOWN
  };
  KeyEvent read_event(int timeout_ms);

 private:
  bool initialized_ = false;
  bool raw_saved_ = false;
  bool kitty_ok_ = false;
  void* old_termios_ = nullptr;
  std::string inbuf_;
  int last_cols_ = 0, last_rows_ = 0;

  // graphics id bookkeeping (kitty images are transmitted once and cached by the
  // terminal; placements are re-issued every frame)
  int next_img_id_ = 1;
  struct ImgEntry { int id = 0; size_t hash = 0; size_t len = 0; int last_seen = 0; };
  std::vector<ImgEntry> kitty_cache_;
  // sixel encoding is expensive: cache the payload by content hash
  struct SixelEntry { std::string payload; int last_seen = 0; };
  std::map<size_t, SixelEntry> sixel_cache_;
  static size_t hash_bytes(const uint8_t* p, size_t n);
  int frame_no_ = 0;
  void query_capabilities(int timeout_ms);
  std::string kitty_transmit(int id, const std::vector<uint8_t>& png);
  std::string kitty_place(int id, int cols, int rows, int sub_x, int sub_y);
  std::string kitty_delete_all_placements();
  std::string iterm_image(const PlacedImage& im, int px_x, int px_y);
  std::string sixel_image(const PlacedImage& im);
};

// Sixel encoder: RGBA -> sixel DCS string (with background compositing).
std::string sixel_encode(const uint8_t* rgba, int w, int h, RGB bg, int max_colors = 256);

}  // namespace mdt
