#include "term.h"

#include "platform.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace mdt {

const char* gfx_name(GfxProto p) {
  switch (p) {
    case GfxProto::Kitty: return "kitty";
    case GfxProto::Iterm2: return "iterm2";
    case GfxProto::Sixel: return "sixel";
    default: return "none";
  }
}

static void wout(const std::string& s) { plat::write_out(s.data(), s.size()); }

static bool g_compat = false;
void set_compat_mode(bool on) { g_compat = on; }

// ============================================================ Screen ========
void Screen::init(int w, int h, RGB fg, RGB bg) {
  def_fg_ = fg; def_bg_ = bg;
  cells_.assign((size_t)std::max(0, w) * std::max(0, h), Cell{});
  prev_.assign(cells_.size(), Cell{});
  w_ = w; h_ = h;
  for (auto& c : cells_) { c.fg = fg; c.bg = bg; c.cp = ' '; }
  force_full_ = true;
}
void Screen::resize(int w, int h) {
  if (w == w_ && h == h_) return;
  std::vector<Cell> nc((size_t)std::max(0, w) * std::max(0, h));
  for (auto& c : nc) { c.fg = def_fg_; c.bg = def_bg_; c.cp = ' '; }
  for (int y = 0; y < std::min(h, h_); y++)
    for (int x = 0; x < std::min(w, w_); x++) nc[(size_t)y * w + x] = cells_[(size_t)y * w_ + x];
  cells_.swap(nc);
  prev_.assign(cells_.size(), Cell{});
  w_ = w; h_ = h;
  force_full_ = true;
}
void Screen::clear() {
  for (auto& c : cells_) { c.cp = ' '; c.fg = def_fg_; c.bg = def_bg_; c.attr = 0; }
}
Cell& Screen::at(int x, int y) { static Cell dummy; if (x < 0 || y < 0 || x >= w_ || y >= h_) return dummy; return cells_[(size_t)y * w_ + x]; }
const Cell& Screen::at(int x, int y) const { static Cell dummy; if (x < 0 || y < 0 || x >= w_ || y >= h_) return dummy; return cells_[(size_t)y * w_ + x]; }

void Screen::put(int x, int y, uint32_t cp, RGB fg, RGB bg, uint8_t attr) {
  if (x < 0 || y < 0 || x >= w_ || y >= h_) return;
  int w = cp_width(cp);
  if (w == 0) return;  // combining marks are dropped (good enough for terminals)
  Cell& c = cells_[(size_t)y * w_ + x];
  c.cp = cp; c.fg = fg; c.bg = bg; c.attr = attr;
  if (w == 2 && x + 1 < w_) { Cell& c2 = cells_[(size_t)y * w_ + x + 1]; c2.cp = 0; c2.fg = fg; c2.bg = bg; c2.attr = attr; }
}
int Screen::fill_row(int x, int y, int count, uint32_t cp, RGB fg, RGB bg, uint8_t attr) {
  int done = 0, cx = x;
  while (done < count && cx < w_) {
    int cw = std::max(1, cp_width(cp));
    put(cx, y, cp, fg, bg, attr);
    cx += cw; done += cw;
  }
  return cx;
}
void Screen::put_str(int x, int y, const std::string& s, RGB fg, RGB bg, uint8_t attr) {
  int cx = x; size_t i = 0;
  while (i < s.size()) {
    uint32_t cp = utf8_next(s, i);
    if (cp == '\n' || cp == '\r') continue;
    int cw = cp_width(cp);
    if (cw == 0) continue;
    if (cx + cw > w_) break;
    put(cx, y, cp, fg, bg, attr);
    cx += cw;
  }
}
void Screen::fill_rect(int x, int y, int w, int h, RGB bg, uint32_t cp) {
  for (int yy = y; yy < y + h; yy++)
    for (int xx = x; xx < x + w; xx++) put(xx, yy, cp, def_fg_, bg, 0);
}
void Screen::hline(int x, int y, int w, uint32_t cp, RGB fg, RGB bg, uint8_t attr) {
  for (int i = 0; i < w; i++) put(x + i, y, cp, fg, bg, attr);
}
void Screen::vline(int x, int y, int h, uint32_t cp, RGB fg, RGB bg) {
  for (int i = 0; i < h; i++) put(x, y + i, cp, fg, bg, 0);
}
void Screen::box_rounded(int x, int y, int w, int h, RGB fg, RGB bg) {
  if (w < 2 || h < 1) return;
  auto g = [&](int cx, int cy, uint32_t cp) { put(cx, cy, cp, fg, bg, 0); };
  g(x, y, 0x256D); g(x + w - 1, y, 0x256E);
  g(x, y + h - 1, 0x2570); g(x + w - 1, y + h - 1, 0x256F);
  for (int i = 1; i < w - 1; i++) { g(x + i, y, 0x2500); g(x + i, y + h - 1, 0x2500); }
  for (int i = 1; i < h - 1; i++) { g(x, y + i, 0x2502); g(x + w - 1, y + i, 0x2502); }
}

std::string Screen::emit_sgr(const Cell& c, const Cell& prev) {
  std::string out;
  uint8_t need = c.attr ^ prev.attr;
  bool color_changed = c.fg != prev.fg || c.bg != prev.bg;
  // Nothing changed relative to the state we believe the terminal is in: the
  // caller forces a fresh SGR after every cursor jump, so silence is safe here.
  if (!color_changed && need == 0) return out;
  out += "\x1b[0";
  if (c.attr & A_BOLD) out += ";1";
  if (c.attr & A_DIM) out += ";2";
  if (c.attr & A_ITALIC) out += ";3";
  if (c.attr & A_UNDER) out += ";4";
  if (c.attr & A_STRIKE) out += ";9";
  if (c.attr & A_REVERSE) out += ";7";
  out += fmt(";38;2;%d;%d;%d", c.fg.r, c.fg.g, c.fg.b);
  if (use_default_bg_ && c.bg == def_bg_) out += ";49";   // terminal's own background
  else out += fmt(";48;2;%d;%d;%d", c.bg.r, c.bg.g, c.bg.b);
  out += "m";
  return out;
}

std::string Screen::render_full(bool sync) {
  force_full_ = true;
  return render_diff(sync);
}

std::string Screen::render_diff(bool sync) {
  std::string out;
  out.reserve((size_t)w_ * h_ / 2);
  if (sync && !no_sync_update_) out += "\x1b[?2026h";  // synchronized output
  // Where the terminal's cursor and SGR state are believed to be.  After a jump
  // both are unknown, so the next cell re-emits position and style.
  int cx = -1, cy = -1;
  Cell cur;
  cur.fg = def_fg_; cur.bg = def_bg_;
  for (int y = 0; y < h_; y++) {
    // Bounds of the changed cells in this row: clean rows cost nothing at all.
    int x0 = -1, x1 = -1;
    for (int x = 0; x < w_; x++) {
      if (force_full_ || !(cells_[(size_t)y * w_ + x] == prev_[(size_t)y * w_ + x])) {
        if (x0 < 0) x0 = x;
        x1 = x;
      }
    }
    if (x0 < 0) continue;
    int run_cells = 0;
    if (safe_paint_) {
      // Start at column 0 and keep the whole row in one run: an unchanged cell
      // before the first change is written again, which costs a few bytes and
      // removes every mid-row cursor jump.
      out += fmt("\x1b[%d;1H", y + 1);
      cx = 0; cy = y;
      cur.attr = 0xFF;
      for (int x = 0; x < x0; x++) {
        const Cell& c = cells_[(size_t)y * w_ + x];
        if (c.cp == 0) continue;
        cur.fg = c.fg; cur.bg = c.bg;
        out += emit_sgr(c, cur);
        cur = c;
        out += utf8_encode(c.cp);
        cx += std::max(1, cp_width(c.cp));
      }
    }
    for (int x = x0; x <= x1; x++) {
      const Cell& c = cells_[(size_t)y * w_ + x];
      if (!force_full_ && c == prev_[(size_t)y * w_ + x]) continue;
      if (c.cp == 0) {  // right half of a wide char: its left half draws it
        cx = x + 1;
        cy = y;
        continue;
      }
      if (cx != x || cy != y) {  // one cursor jump per run instead of per cell
        out += fmt(x == 0 ? "\x1b[%d;1H" : "\x1b[%d;%dH", y + 1, x + 1);
        cx = x; cy = y;
        cur.attr = 0xFF;  // style unknown after a jump: force one SGR
        cur.fg = c.fg; cur.bg = c.bg;
        out += emit_sgr(c, cur);
        cur = c;
        out += utf8_encode(c.cp == 0 ? ' ' : c.cp);
        cx += std::max(1, cp_width(c.cp));
        continue;
      }
      out += emit_sgr(c, cur);  // "" when the style already matches
      cur = c;
      out += utf8_encode(c.cp == 0 ? ' ' : c.cp);
      cx += std::max(1, cp_width(c.cp));
      // A long run is re-anchored now and then: if the terminal is narrower
      // than its reported size, a run can never drift far from its column.
      // (Not in safe paint mode: that mode exists to avoid jumps inside a row.)
      if (!safe_paint_ && ++run_cells >= 64) { run_cells = 0; cx = -1; cy = -1; }
    }
  }
  if (!cursor_hidden_) out += "\x1b[?25h"; else out += "\x1b[?25l";
  if (sync && !no_sync_update_) out += "\x1b[?2026l";
  prev_ = cells_;
  force_full_ = false;
  return out;
}


// ======================================================== Terminal ==========
bool Terminal::init(const std::string& gfx_override) {
  caps.term_name = getenv("TERM") ? getenv("TERM") : "";
  if (const char* tp = getenv("TERM_PROGRAM")) caps.term_program = tp;
  if (const char* cv = getenv("COLORTERM")) caps.truecolor = (strstr(cv, "truecolor") || strstr(cv, "24bit"));
#ifdef _WIN32
  // Windows Terminal / conhost do not set TERM; WT_SESSION identifies WT.
  if (caps.term_program.empty() && getenv("WT_SESSION")) caps.term_program = "Windows Terminal";
  if (caps.term_name.empty() && caps.term_program == "Windows Terminal") caps.term_name = "xterm-256color";
#endif

  if (!plat::stdin_is_tty() || !plat::stdout_is_tty()) {
    fprintf(stderr, "mdt: not running on a terminal (stdin/stdout must be a tty)\n");
    return false;
  }
  // Install the emergency restore before the terminal is modified at all, so
  // even a signal that arrives mid-switch leaves a usable shell behind.
  plat::set_panic_hook(&Terminal::panic_restore);
  plat::maybe_install_debug_signal();

  std::string rerr;
  if (!plat::raw_begin(&rerr)) {
    fprintf(stderr, "mdt: cannot switch the terminal to raw mode (%s)\n", rerr.c_str());
    return false;
  }
  raw_saved_ = true;

  wout("\x1b[?1049h\x1b[?25l\x1b[2J\x1b[H");  // alt screen
  // 1002 also reports motion while a button is held, which is what makes a
  // scrollbar drag update live; 1000 alone only reports press and release.
  wout("\x1b[?1002h\x1b[?1006h");              // mouse: wheel + drag + SGR coords
  if (!g_compat) wout("\x1b[>1u");             // kitty keyboard (best effort)

  handle_resize();
  query_capabilities(350);

  // Graphics protocol selection.
  if (!gfx_override.empty() && gfx_override != "auto") {
    if (gfx_override == "kitty") caps.gfx = GfxProto::Kitty;
    else if (gfx_override == "iterm2") caps.gfx = GfxProto::Iterm2;
    else if (gfx_override == "sixel") caps.gfx = GfxProto::Sixel;
    else caps.gfx = GfxProto::None;
  } else {
    bool env_kitty = caps.term_name.find("kitty") != std::string::npos || getenv("KITTY_WINDOW_ID") ||
                     caps.term_program == "WezTerm" || caps.term_program == "ghostty" ||
                     caps.term_program == "Ghostty" || caps.term_name.find("xterm-ghostty") != std::string::npos;
    bool env_iterm = caps.term_program == "iTerm.app" || getenv("ITERM_SESSION_ID");
    if (kitty_ok_) caps.gfx = GfxProto::Kitty;
    else if (env_kitty) caps.gfx = GfxProto::Kitty;
    else if (caps.sixel) caps.gfx = GfxProto::Sixel;
    else if (env_iterm) caps.gfx = GfxProto::Iterm2;
    else caps.gfx = GfxProto::None;
  }
  last_cols_ = caps.cols; last_rows_ = caps.rows;
  initialized_ = true;
  return true;
}

void Terminal::query_capabilities(int timeout_ms) {
  if (g_compat) return;  // probing upsets some terminals, and we do not need it
  // Ask for cell size (16t), window size (14t), DA1 (c), kitty graphics support (a=q).
  std::string q;
  q += "\x1b[16t";        // cell size in pixels  -> CSI 6 ; h ; w t
  q += "\x1b[14t";        // window size in px    -> CSI 4 ; h ; w t
  q += "\x1b[c";          // primary DA           -> CSI ? ... c
  q += "\x1b_Gi=31,s=1,v=1,a=q,f=24,t=d;AAAA\x1b\\";  // kitty graphics probe
  wout(q);
  std::string resp;
  double deadline = now_ms() + timeout_ms;
  while (now_ms() < deadline) {
    if (!plat::wait_input(30)) continue;
    char buf[1024];
    int n = plat::read_input(buf, sizeof(buf));
    if (n <= 0) break;
    resp.append(buf, (size_t)n);
    if (resp.find("\x1b\\") != std::string::npos && resp.find('c') != std::string::npos) {
      // keep reading a little in case several answers are still in flight
      if (now_ms() + 40 > deadline) deadline = now_ms() + 40;
    }
  }
  // Parse responses.
  size_t pos = 0;
  while (pos < resp.size()) {
    size_t esc = resp.find('\x1b', pos);
    if (esc == std::string::npos) {
      inbuf_.append(resp, pos, std::string::npos);  // leftover user input
      break;
    }
    if (esc > pos) inbuf_.append(resp, pos, esc - pos);
    if (resp.compare(esc, 2, "\x1b_") == 0 || resp.compare(esc, 2, "\x1bP") == 0) {
      // APC/DCS payload (kitty answer)
      size_t end = resp.find("\x1b\\", esc);
      if (end == std::string::npos) { inbuf_.append(resp, esc, std::string::npos); break; }
      std::string payload = resp.substr(esc + 2, end - esc - 2);
      if (payload.find("OK") != std::string::npos) kitty_ok_ = true;
      pos = end + 2;
      continue;
    }
    if (resp.compare(esc, 2, "\x1b[") == 0) {
      size_t end = esc + 2;
      while (end < resp.size() && !((resp[end] >= '@' && resp[end] <= '~'))) end++;
      if (end >= resp.size()) break;  // incomplete, wait for more input
      char final = resp[end];
      std::string body = resp.substr(esc + 2, end - esc - 2);
      if (final == 't' && !body.empty() && body[0] == '6') {  // cell size
        auto parts = split(body, ';');
        if (parts.size() >= 3) { caps.cell_h = atoi(parts[1].c_str()); caps.cell_w = atoi(parts[2].c_str()); }
      } else if (final == 't' && !body.empty() && body[0] == '4') {  // window px size
        auto parts = split(body, ';');
        if (parts.size() >= 3) { caps.win_h = atoi(parts[1].c_str()); caps.win_w = atoi(parts[2].c_str()); }
      } else if (final == 'c') {
        if (body.find(";4") != std::string::npos || body.find("?4;") != std::string::npos ||
            (body.size() > 1 && body[1] == '4')) caps.sixel = true;
        // note: xterm reports ";4" among capabilities
        if (body.find("4") != std::string::npos) {
          // conservative: only treat as sixel if 4 appears as a capability token
          auto toks = split(body, ';');
          for (auto& t : toks) { std::string tt = t; if (!tt.empty() && tt[0] == '?') tt = tt.substr(1); if (tt == "4") caps.sixel = true; }
        }
      }
      pos = end + 1;
      continue;
    }
    // Unknown escape: keep it, the input parser will deal with it.
    inbuf_.append(resp, esc, std::string::npos);
    break;
  }
}

// Byte-for-byte the same sequence shutdown() writes, but in a static buffer so
// it can be emitted from a signal handler.
static const char kPanicExit[] =
    "\x1b[?2026l\x1b[?1000l\x1b[?1002l\x1b[?1006l\x1b[?25h\x1b[0m\x1b[?1049l";

void Terminal::panic_restore() {
  plat::write_out(kPanicExit, sizeof(kPanicExit) - 1);
}

void Terminal::shutdown() {
  if (!initialized_) return;
  clear_images();
  wout("\x1b[?2026l\x1b[?1000l\x1b[?1002l\x1b[?1006l\x1b[?25h\x1b[0m\x1b[?1049l");
  if (raw_saved_) {
    plat::raw_end();
    raw_saved_ = false;
  }
  initialized_ = false;
}
void Terminal::write_raw(const std::string& s) { wout(s); }
void Terminal::set_title(const std::string& t) { wout("\x1b]0;" + t + "\x07"); }
void Terminal::set_clipboard(const std::string& s) { wout("\x1b]52;c;" + base64_encode((const uint8_t*)s.data(), s.size()) + "\x07"); }

void Terminal::handle_resize() {
  int cols = caps.cols, rows = caps.rows, px_w = 0, px_h = 0;
  if (plat::window_size(cols, rows, px_w, px_h)) {
    caps.cols = cols;
    caps.rows = rows;
    if (px_w > 0 && px_h > 0) {
      caps.win_w = px_w;
      caps.win_h = px_h;
      if (caps.cols > 0) caps.cell_w = caps.win_w / caps.cols;
      if (caps.rows > 0) caps.cell_h = caps.win_h / caps.rows;
    }
  }
  if (caps.cols < 20) caps.cols = 20;
  if (caps.rows < 5) caps.rows = 5;
}
bool Terminal::resized() { return plat::poll_resize(); }

bool Terminal::poll_input(std::string& out, int timeout_ms) {
  if (!plat::wait_input(timeout_ms)) return true;  // timeout
  char buf[4096];
  int n = plat::read_input(buf, sizeof(buf));
  if (n < 0) return false;  // EOF
  if (n == 0) return true;
  out.assign(buf, (size_t)n);
  return true;
}

int Terminal::read_key(int timeout_ms) {
  KeyEvent e = read_event(timeout_ms);
  if (e.type == KeyEvent::Char) return (unsigned char)e.ch;
  if (e.type == KeyEvent::Special) return e.code;
  return 0;
}

Terminal::KeyEvent Terminal::read_event(int timeout_ms) {
  auto parse_one = [&]() -> KeyEvent {
    KeyEvent k;
    if (inbuf_.empty()) return k;
    unsigned char c = (unsigned char)inbuf_[0];
    // A CSI sequence normally starts with ESC.  Some Windows consoles eat the
    // ESC of a mouse report (their own VT input parser consumes it), so a chunk
    // starting with "[<" / "[M" is accepted as a CSI sequence too - only a
    // well-formed report matches, so ordinary typed text is unaffected.
    if (c != 0x1b && c == '[' && inbuf_.size() >= 2 && (inbuf_[1] == '<' || inbuf_[1] == 'M')) {
      inbuf_.insert(inbuf_.begin(), '\x1b');  // synthesise the swallowed ESC
      c = 0x1b;
    }
    if (c == 0x1b) {
      if (inbuf_.size() == 1) return k;  // need more
      const size_t lead = 1;  // index of '[' or 'O'
      if ((inbuf_[lead] == '[' || inbuf_[lead] == 'O') && inbuf_.size() >= lead + 2 &&
          inbuf_[1] == '[' && inbuf_[lead + 1] == 'M') {
        // X10 mouse report: three raw bytes follow.  Terminals without SGR
        // mouse mode report the wheel this way.
        if (inbuf_.size() < lead + 5) return k;  // need more
        int b = (unsigned char)inbuf_[lead + 2] - 32;
        int mx = (unsigned char)inbuf_[lead + 3] - 33;
        int my = (unsigned char)inbuf_[lead + 4] - 33;
        inbuf_.erase(0, lead + 5);
        k.type = KeyEvent::Mouse; k.code = b; k.mx = mx; k.my = my;
        if (b & 64) { if (b & 1) k.wheel_down = true; else k.wheel_up = true; }
        k.drag = (b & 32) != 0;
        return k;
      }
      {
        size_t i = lead + 1;
        std::string body;
        // CSI grammar: parameters (0x30-0x3F) and intermediates (0x20-0x2F)
        // come first, then exactly one final byte (0x40-0x7E).  Scanning for
        // "anything alphanumeric" instead swallowed the final byte of SGR mouse
        // reports (\x1b[<64;40;12M), so the wheel never reached the app.
        while (i < inbuf_.size()) {
          unsigned char b = (unsigned char)inbuf_[i];
          if (b < 0x20 || b > 0x3f) break;
          body += (char)b;
          i++;
        }
        if (i >= inbuf_.size()) return k;  // incomplete
        char final = inbuf_[i];
        inbuf_.erase(0, i + 1);
        if (!body.empty() && (body[0] == '<' || body[0] == '>')) {  // SGR mouse
          char btn = body[0];
          auto parts = split(body.substr(1), ';');
          if (parts.size() >= 3) {
            int b = atoi(parts[0].c_str());
            k.type = KeyEvent::Mouse; k.code = b;
            k.mx = atoi(parts[1].c_str()) - 1; k.my = atoi(parts[2].c_str()) - 1;
            if (btn == '<' && (b & 64)) { if (b & 1) k.wheel_down = true; else k.wheel_up = true; }
            k.drag = (b & 32) != 0;
            k.release = (final == 'm');   // button-up
            return k;
          }
          return k;
        }
        int mod = 0;
        auto parts = split(body, ';');
        int p1 = parts.size() > 0 && !parts[0].empty() ? atoi(parts[0].c_str()) : 0;
        if (parts.size() > 1) mod = atoi(parts[1].c_str());
        k.type = KeyEvent::Special;
        switch (final) {
          case 'A': k.code = (mod == 2) ? K_SHIFT_UP : (mod == 5 ? K_CTRL_UP : K_UP); break;
          case 'B': k.code = (mod == 2) ? K_SHIFT_DOWN : (mod == 5 ? K_CTRL_DOWN : K_DOWN); break;
          case 'C': k.code = K_RIGHT; break;
          case 'D': k.code = K_LEFT; break;
          case 'H': k.code = K_HOME; break;
          case 'F': k.code = K_END; break;
          case '~':
            switch (p1) {
              case 1: case 7: k.code = K_HOME; break;
              case 4: case 8: k.code = K_END; break;
              case 5: k.code = K_PGUP; break;
              case 6: k.code = K_PGDN; break;
              case 3: k.code = K_DEL; break;
              default: k.code = 0; break;
            }
            break;
          case 'u': k.code = p1 == 13 ? K_ENTER : 0; break;
          case 'Z': k.code = K_TAB; break;  // shift-tab: treat as tab
          default: k.code = 0; break;
        }
        return k;
      }
    }
    if (c == 0x1b) {  // bare ESC or Alt+key
      inbuf_.erase(0, 1);
      k.type = KeyEvent::Special; k.code = K_ESC;
      return k;
    }
    if (c == '\r' || c == '\n') { inbuf_.erase(0, 1); k.type = KeyEvent::Special; k.code = K_ENTER; return k; }
    if (c == 0x7f || c == 0x08) { inbuf_.erase(0, 1); k.type = KeyEvent::Special; k.code = K_BACKSPACE; return k; }
    if (c == '\t') { inbuf_.erase(0, 1); k.type = KeyEvent::Special; k.code = K_TAB; return k; }
    if (c == 3) { inbuf_.erase(0, 1); k.type = KeyEvent::Special; k.code = 'q'; return k; }
    if (c < 32) { inbuf_.erase(0, 1); return k; }
    // UTF-8 char
    size_t i = 0; uint32_t cp = utf8_next(inbuf_, i);
    if (cp == 0xFFFD && inbuf_.size() < 4 && c >= 0xC0) return k;  // maybe incomplete
    inbuf_.erase(0, i);
    k.type = KeyEvent::Char;
    if (cp < 128) { k.ch = (char)cp; return k; }
    // multi-byte: expose as Char with the low codepoint truncated (handled as text input elsewhere)
    k.ch = 0; k.code = (int)cp;
    k.type = KeyEvent::Special;
    return k;
  };

  KeyEvent k = parse_one();
  if (k.type != KeyEvent::None) return k;
  if (inbuf_.empty()) {
    std::string in;
    if (!poll_input(in, timeout_ms)) { KeyEvent e; e.code = -1; e.type = KeyEvent::Special; return e; }
    if (in.empty()) return k;
    inbuf_ += in;
  } else {
    // partial sequence: wait a moment for the rest
    std::string in;
    poll_input(in, 25);
    inbuf_ += in;
  }
  k = parse_one();
  if (k.type == KeyEvent::None && !inbuf_.empty() && (unsigned char)inbuf_[0] == 0x1b) {
    inbuf_.erase(0, 1);
    k.type = KeyEvent::Special; k.code = K_ESC;
  }
  return k;
}

// ------------------------------------------------------------- graphics ----
void Terminal::drop_image_cache() { kitty_cache_.clear(); }
void Terminal::clear_images() {
  if (caps.gfx == GfxProto::Kitty) wout("\x1b_Ga=d,d=A,q=2\x1b\\");  // placements + data
  kitty_cache_.clear();
}
std::string Terminal::kitty_transmit(int id, const std::vector<uint8_t>& png) {
  std::string out;
  const size_t CHUNK = 3072;
  size_t off = 0;
  bool first = true;
  while (off < png.size()) {
    size_t n = std::min(CHUNK, png.size() - off);
    bool last = (off + n >= png.size());
    std::string b64 = base64_encode(png.data() + off, n);
    out += "\x1b_G";
    if (first) out += fmt("a=t,f=100,i=%d,q=2,m=%d;", id, last ? 0 : 1);
    else out += fmt("m=%d;", last ? 0 : 1);
    out += b64;
    out += "\x1b\\";
    off += n;
    first = false;
  }
  return out;
}
std::string Terminal::kitty_place(int id, const PlacedImage& im, int cols, int rows) {
  std::string s;
  if (cols > 0 && rows > 0)
    s = fmt("\x1b_Ga=p,i=%d,p=1,c=%d,r=%d,X=%d,Y=%d", id, cols, rows, im.sub_x, im.sub_y);
  else
    s = fmt("\x1b_Ga=p,i=%d,p=1,X=%d,Y=%d", id, im.sub_x, im.sub_y);
  // source crop (lowercase x,y,w,h): shows only part of the already-cached
  // image, so clipping at a viewport edge costs no re-transmission
  if (im.src_w > 0 && im.src_h > 0 && (im.src_y > 0 || im.src_h < im.px_h))
    s += fmt(",x=%d,y=%d,w=%d,h=%d", im.src_x, im.src_y, im.src_w, im.src_h);
  s += ",z=0,q=2\x1b\\";
  return s;
}
std::string Terminal::kitty_delete_all_placements() { return "\x1b_Ga=d,d=a,q=2\x1b\\"; }  // placements only

std::string Terminal::iterm_image(const PlacedImage& im, int px_x, int px_y) {
  (void)px_x; (void)px_y;
  std::string out;
  out += "\x1b]1337;File=inline=1;";
  if (im.px_w > 0 && im.px_h > 0) out += fmt("width=%dpx;height=%dpx;", im.px_w, im.px_h);
  out += "preserveAspectRatio=1;";
  out += "name=" + base64_encode((const uint8_t*)"mdt.png", 7) + ":";
  out += base64_encode(im.png->data(), im.png->size());
  out += "\x07";
  return out;
}

size_t Terminal::hash_bytes(const uint8_t* p, size_t n) {
  size_t h = 1469598103934665603ull;
  for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ull; }
  return h;
}

std::string Terminal::emit_images(const std::vector<PlacedImage>& imgs) {
  if (caps.gfx == GfxProto::None || imgs.empty()) {
    if (!imgs.empty()) return "";
    // nothing visible: make sure stale placements disappear
    if (caps.gfx == GfxProto::Kitty && !kitty_cache_.empty()) {
      for (auto& e : kitty_cache_) e.last_seen = -1;
      return kitty_delete_all_placements();
    }
    return "";
  }
  frame_no_++;
  std::string out;
  switch (caps.gfx) {
    case GfxProto::Kitty: {
      // clear placements (image data stays cached inside the terminal)
      out += kitty_delete_all_placements();
      for (auto& im : imgs) {
        if (!im.png || im.png->empty()) continue;
        size_t h = 1469598103934665603ull;
        for (uint8_t b : *im.png) { h ^= b; h *= 1099511628211ull; }
        h ^= (size_t)im.px_w * 1315423911u + (size_t)im.px_h;
        ImgEntry* hit = nullptr;
        for (auto& e : kitty_cache_) {
          if (e.hash == h && e.len == im.png->size()) { hit = &e; break; }
        }
        int id;
        if (hit) {
          id = hit->id;
          hit->last_seen = frame_no_;
        } else {
          id = next_img_id_++;
          ImgEntry e;
          e.id = id; e.hash = h; e.len = im.png->size(); e.last_seen = frame_no_;
          kitty_cache_.push_back(e);
          out += kitty_transmit(id, *im.png);
        }
        out += fmt("\x1b[%d;%dH", im.y + 1, im.x + 1);
        // With a known cell size the image is placed as cols x rows cells and
        // drawn 1:1; without it, leave c/r out so kitty uses the pixel size.
        bool know_cells = caps.cell_w > 0 && caps.cell_h > 0;
        int pc = know_cells ? im.cols : 0, pr = know_cells ? im.rows : 0;
        out += kitty_place(id, im, pc, pr);
      }
      // evict images that have not been used for a while
      for (size_t i = 0; i < kitty_cache_.size();) {
        if (frame_no_ - kitty_cache_[i].last_seen > 120) {
          out += fmt("\x1b_Ga=d,d=i,i=%d,q=2\x1b\\", kitty_cache_[i].id);
          kitty_cache_.erase(kitty_cache_.begin() + (long)i);
        } else {
          i++;
        }
      }
      break;
    }
    case GfxProto::Iterm2: {
      for (auto& im : imgs) {
        if (!im.png) continue;
        out += fmt("\x1b[%d;%dH", im.y + 1, im.x + 1);
        out += iterm_image(im, 0, 0);
      }
      break;
    }
    case GfxProto::Sixel: {
      for (auto& im : imgs) {
        if (!im.rgba) continue;
        size_t h = hash_bytes(im.rgba->data(), im.rgba->size()) ^ ((size_t)im.px_w << 20) ^ im.px_h;
        auto it = sixel_cache_.find(h);
        if (it != sixel_cache_.end()) {
          it->second.last_seen = frame_no_;
          out += fmt("\x1b[%d;%dH", im.y + 1, im.x + 1);
          out += it->second.payload;
        } else {
          std::string payload = sixel_encode(im.rgba->data(), im.px_w, im.px_h, image_bg);
          SixelEntry e;
          e.payload = payload;
          e.last_seen = frame_no_;
          sixel_cache_[h] = e;
          out += fmt("\x1b[%d;%dH", im.y + 1, im.x + 1);
          out += payload;
        }
      }
      if (sixel_cache_.size() > 96) {
        for (auto it = sixel_cache_.begin(); it != sixel_cache_.end();) {
          if (frame_no_ - it->second.last_seen > 60) it = sixel_cache_.erase(it);
          else ++it;
        }
      }
      break;
    }
    default: break;
  }
  return out;
}

// ======================================================== sixel encoding =====
namespace {
struct Color {
  int r, g, b;
  long count = 0;
  int map = -1;
};
int color_dist(const Color& a, const Color& b) {
  int dr = a.r - b.r, dg = a.g - b.g, db = a.b - b.b;
  return dr * dr * 2 + dg * dg * 4 + db * db;
}
}  // namespace

std::string sixel_encode(const uint8_t* rgba, int w, int h, RGB bg, int max_colors) {
  if (w <= 0 || h <= 0) return "";
  // Composite over background + count unique colors.
  std::vector<Color> pal;
  std::vector<int> idx((size_t)w * h);
  std::vector<uint8_t> rgb((size_t)w * h * 3);
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      const uint8_t* p = rgba + ((size_t)y * w + x) * 4;
      int a = p[3];
      int r = (p[0] * a + bg.r * (255 - a) + 127) / 255;
      int g = (p[1] * a + bg.g * (255 - a) + 127) / 255;
      int b = (p[2] * a + bg.b * (255 - a) + 127) / 255;
      size_t o = ((size_t)y * w + x) * 3;
      rgb[o] = (uint8_t)r; rgb[o + 1] = (uint8_t)g; rgb[o + 2] = (uint8_t)b;
    }
  }
  // Build palette: exact match up to max_colors, otherwise median-cut.
  std::vector<Color> uniq;
  {
    int buckets[4096] = {0};
    std::vector<int> bucket_color(4096, -1);
    for (size_t i = 0; i < (size_t)w * h; i++) {
      int r = rgb[i * 3] >> 4, g = rgb[i * 3 + 1] >> 4, b = rgb[i * 3 + 2] >> 4;
      int key = (r << 8) | (g << 4) | b;
      if (!buckets[key]) { Color c; c.r = rgb[i * 3]; c.g = rgb[i * 3 + 1]; c.b = rgb[i * 3 + 2]; uniq.push_back(c); bucket_color[key] = (int)uniq.size() - 1; }
      buckets[key]++;
      uniq[bucket_color[key]].count++;
    }
  }
  if ((int)uniq.size() <= max_colors) {
    pal = uniq;
  } else {
    // median cut over the (bucket) colors
    struct Box { std::vector<int> items; };
    std::vector<Box> boxes;
    Box all; for (int i = 0; i < (int)uniq.size(); i++) all.items.push_back(i);
    boxes.push_back(all);
    while ((int)boxes.size() < max_colors) {
      int best = -1, best_range = -1, best_ch = 0;
      for (int i = 0; i < (int)boxes.size(); i++) {
        if (boxes[i].items.size() < 2) continue;
        int mn[3] = {255, 255, 255}, mx[3] = {0, 0, 0};
        for (int ii : boxes[i].items) {
          int v[3] = {uniq[ii].r, uniq[ii].g, uniq[ii].b};
          for (int c = 0; c < 3; c++) { mn[c] = std::min(mn[c], v[c]); mx[c] = std::max(mx[c], v[c]); }
        }
        for (int c = 0; c < 3; c++) if (mx[c] - mn[c] > best_range) { best_range = mx[c] - mn[c]; best = i; best_ch = c; }
      }
      if (best < 0 || best_range <= 0) break;
      Box& b = boxes[best];
      std::sort(b.items.begin(), b.items.end(), [&](int a, int c) {
        int va = best_ch == 0 ? uniq[a].r : (best_ch == 1 ? uniq[a].g : uniq[a].b);
        int vc = best_ch == 0 ? uniq[c].r : (best_ch == 1 ? uniq[c].g : uniq[c].b);
        return va < vc;
      });
      Box b2;
      size_t half = b.items.size() / 2;
      b2.items.assign(b.items.begin() + half, b.items.end());
      b.items.resize(half);
      boxes.push_back(b2);
    }
    for (auto& b : boxes) {
      long r = 0, g = 0, bl = 0, n = 0;
      for (int i : b.items) { r += (long)uniq[i].r * uniq[i].count; g += (long)uniq[i].g * uniq[i].count; bl += (long)uniq[i].b * uniq[i].count; n += uniq[i].count; }
      Color c; c.count = n;
      c.r = n ? (int)(r / n) : 0; c.g = n ? (int)(g / n) : 0; c.b = n ? (int)(bl / n) : 0;
      for (int i : b.items) uniq[i].map = (int)pal.size();
      pal.push_back(c);
    }
    // colors not assigned during split
    for (auto& u : uniq) {
      if (u.map >= 0) continue;
      int best = 0, bd = 1 << 30;
      for (int i = 0; i < (int)pal.size(); i++) { int d = color_dist(u, pal[i]); if (d < bd) { bd = d; best = i; } }
      u.map = best;
    }
  }
  // map pixels through a 16^3 lookup table
  std::vector<uint8_t> lut(4096, 0);
  for (int r = 0; r < 16; r++)
    for (int g = 0; g < 16; g++)
      for (int b = 0; b < 16; b++) {
        Color cc; cc.r = r * 17; cc.g = g * 17; cc.b = b * 17;
        int best = 0, bd = 1 << 30;
        for (int j = 0; j < (int)pal.size(); j++) { int d = color_dist(cc, pal[j]); if (d < bd) { bd = d; best = j; } }
        lut[(r << 8) | (g << 4) | b] = (uint8_t)best;
      }
  for (size_t i = 0; i < (size_t)w * h; i++) {
    int r = rgb[i * 3] >> 4, g = rgb[i * 3 + 1] >> 4, b = rgb[i * 3 + 2] >> 4;
    idx[i] = lut[(r << 8) | (g << 4) | b];
  }
  // emit
  std::string out;
  out += fmt("\x1bPq\"1;1;%d;%d", w, h);
  for (size_t i = 0; i < pal.size(); i++) {
    out += fmt("#%d;2;%d;%d;%d", (int)i,
               (pal[i].r * 100 + 127) / 255, (pal[i].g * 100 + 127) / 255, (pal[i].b * 100 + 127) / 255);
  }
  int bands = (h + 5) / 6;
  for (int b = 0; b < bands; b++) {
    int y0 = b * 6;
    // colors used in this band
    std::vector<int> used;
    std::vector<char> seen(pal.size(), 0);
    for (int y = y0; y < std::min(h, y0 + 6); y++)
      for (int x = 0; x < w; x++) {
        int c = idx[(size_t)y * w + x];
        if (c >= 0 && c < (int)seen.size() && !seen[c]) { seen[c] = 1; used.push_back(c); }
      }
    for (size_t ui = 0; ui < used.size(); ui++) {
      int c = used[ui];
      out += fmt("#%d", c);
      int run = 0; char last = 0;
      std::string line;
      for (int x = 0; x < w; x++) {
        int bits = 0;
        for (int k = 0; k < 6; k++) {
          int y = y0 + k;
          if (y < h && idx[(size_t)y * w + x] == c) bits |= (1 << k);
        }
        char ch = (char)(0x3F + bits);
        if (ch == last) { run++; }
        else {
          if (run > 0) {
            if (run >= 4) line += fmt("!%d%c", run, last);
            else line.append((size_t)run, last);
          }
          last = ch; run = 1;
        }
      }
      if (run > 0) { if (run >= 4) line += fmt("!%d%c", run, last); else line.append((size_t)run, last); }
      out += line;
      if (ui + 1 < used.size()) out += "$";
    }
    if (b + 1 < bands) out += "-";
  }
  out += "\x1b\\";
  return out;
}

}  // namespace mdt
