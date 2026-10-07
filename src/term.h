// term.h : terminal capability probing, cell framebuffer, graphics protocols.
#pragma once
#include <cstdint>
#include <tuple>   // libc++'s <map> needs std::forward_as_tuple (Xcode 15+)
#include <map>
#include <string>
#include <vector>

#include "util.h"

namespace mdt {

enum class GfxProto { None, Kitty, Iterm2, Sixel };

// Compatibility mode (--compat): no graphics, no DEC 2026 synchronized output,
// no kitty keyboard protocol, no capability probes - for terminals whose
// support for those is broken or half-implemented.
void set_compat_mode(bool on);

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
  // When on, cells that use the page background are emitted as SGR 49 so the
  // terminal's own background (colour, transparency, image) shows through.
  void set_terminal_bg(bool on) { use_default_bg_ = on; force_full_ = true; }
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
  // sync=false leaves the DEC 2026 window open: the caller wraps text and
  // graphics escapes in one synchronized update so the terminal paints once.
  std::string render_diff(bool sync = true);
  std::string render_full(bool sync = true);
  bool sync_update() const { return !no_sync_update_; }
  void invalidate() { force_full_ = true; }
  bool using_terminal_bg() const { return use_default_bg_; }
  void set_cursor_hidden(bool hidden) { cursor_hidden_ = hidden; }
  // Compatibility painting: each changed row is written from column 0 as one
  // contiguous run, so no cursor jump ever happens inside a row.  For
  // terminals whose cursor handling loses the character after a jump.
  void set_safe_paint(bool on) { safe_paint_ = on; force_full_ = true; }
  // Terminals that mishandle DEC 2026 (synchronized output) can switch it off.
  void set_sync_update(bool on) {
    if (no_sync_update_ != !on) { no_sync_update_ = !on; force_full_ = true; }
  }

 private:
  std::string emit_sgr(const Cell& c, const Cell& prev);
  int w_ = 0, h_ = 0;
  std::vector<Cell> cells_, prev_;
  bool force_full_ = true;
  bool cursor_hidden_ = true;
  bool use_default_bg_ = false;
  bool no_sync_update_ = false;
  bool safe_paint_ = false;
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
  // Probes the terminal (cell size, window size, sixel, kitty graphics) and
  // waits up to timeout_ms for the answers.  Also used by --diag and
  // --list-caps, which need the geometry without entering the reader.
  void query_capabilities(int timeout_ms);

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
  // Emergency restore from a signal / console-control context: leaves the alt
  // screen and gives the tty back.  Only async-signal-safe calls inside.
  static void panic_restore();

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
    bool drag = false;              // motion event with a button held
    bool release = false;           // button-up (SGR final 'm')
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

  std::string kitty_transmit(int id, const std::vector<uint8_t>& png);
  std::string kitty_place(int id, int cols, int rows, int sub_x, int sub_y);
  std::string kitty_delete_all_placements();
  std::string iterm_image(const PlacedImage& im, int px_x, int px_y);
  std::string sixel_image(const PlacedImage& im);
};

// Sixel encoder: RGBA -> sixel DCS string (with background compositing).
std::string sixel_encode(const uint8_t* rgba, int w, int h, RGB bg, int max_colors = 256);

}  // namespace mdt
