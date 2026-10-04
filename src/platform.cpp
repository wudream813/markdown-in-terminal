#include "platform.h"

#include <cstdio>
#include <cstring>

// ===========================================================================
//  POSIX
// ===========================================================================
#if !defined(_WIN32)

#include <cerrno>
#include <cstdint>
#include <ctime>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

#include <cstdlib>

namespace mdt {
namespace plat {

static volatile sig_atomic_t g_winch = 0;
static void on_winch(int) { g_winch = 1; }
static struct termios* g_saved = nullptr;
static void (*g_panic)() = nullptr;

// SIGTERM/SIGHUP/SIGINT land here when mdt is killed from the outside (or the
// terminal window is closed).  Restore the tty before dying, otherwise the
// shell is left in raw mode with the alt screen still active.
extern "C" void mdt_panic_handler(int sig) {
  if (g_panic) g_panic();
  if (g_saved) tcsetattr(STDIN_FILENO, TCSAFLUSH, g_saved);
  _exit(128 + sig);
}

void set_panic_hook(void (*fn)()) {
  g_panic = fn;
  signal(SIGINT, mdt_panic_handler);
  signal(SIGTERM, mdt_panic_handler);
  signal(SIGHUP, mdt_panic_handler);
  signal(SIGQUIT, mdt_panic_handler);
}

static void* debug_signal_thread(void* arg) {
  struct timespec ts;
  ts.tv_sec = (time_t)((intptr_t)arg / 1000);
  ts.tv_nsec = (long)(((intptr_t)arg % 1000) * 1000000L);
  nanosleep(&ts, nullptr);
  raise(SIGTERM);
  return nullptr;
}

void maybe_install_debug_signal() {
  const char* v = getenv("MDT_DEBUG_SIGNAL");
  if (!v || !*v) return;
  int ms = atoi(v) > 0 ? atoi(v) : 1000;
  pthread_t t;
  if (pthread_create(&t, nullptr, debug_signal_thread, (void*)(intptr_t)ms) == 0) pthread_detach(t);
}

bool stdin_is_tty() { return isatty(STDIN_FILENO) == 1; }
bool stdout_is_tty() { return isatty(STDOUT_FILENO) == 1; }

bool raw_begin(std::string* err) {
  struct termios tio;
  if (tcgetattr(STDIN_FILENO, &tio) != 0) {
    if (err) *err = "tcgetattr failed";
    return false;
  }
  if (!g_saved) g_saved = new termios(tio);
  struct termios raw = tio;
  raw.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
  raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
  raw.c_oflag &= ~(OPOST);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 0;
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
    if (err) *err = "tcsetattr failed";
    return false;
  }
  signal(SIGWINCH, on_winch);
  signal(SIGPIPE, SIG_IGN);
  return true;
}

void raw_end() {
  if (g_saved) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, g_saved);
    delete g_saved;
    g_saved = nullptr;
  }
}

bool window_size(int& cols, int& rows, int& px_w, int& px_h) {
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
    cols = ws.ws_col;
    rows = ws.ws_row > 0 ? ws.ws_row : rows;
    px_w = ws.ws_xpixel > 0 ? ws.ws_xpixel : px_w;
    px_h = ws.ws_ypixel > 0 ? ws.ws_ypixel : px_h;
    return true;
  }
  const char* c = getenv("COLUMNS");
  const char* r = getenv("LINES");
  bool ok = false;
  if (c) { cols = atoi(c); ok = true; }
  if (r) { rows = atoi(r); ok = true; }
  return ok;
}

bool poll_resize() {
  if (!g_winch) return false;
  g_winch = 0;
  return true;
}

bool wait_input(int timeout_ms) {
  fd_set rf;
  FD_ZERO(&rf);
  FD_SET(STDIN_FILENO, &rf);
  struct timeval tv;
  tv.tv_sec = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;
  int r = select(STDIN_FILENO + 1, &rf, nullptr, nullptr, &tv);
  return r > 0;
}

int read_input(char* buf, size_t n) {
  ssize_t r = read(STDIN_FILENO, buf, n);
  if (r > 0) return (int)r;
  // Negative == closed/EOF, 0 == nothing available yet.  Reporting EOF as 0
  // would make the event loop spin at 100% once the tty disappears.
  if (r == 0) return -1;
  if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return 0;
  return -1;
}

bool write_out(const char* data, size_t n) {
  size_t off = 0;
  while (off < n) {
    ssize_t w = write(STDOUT_FILENO, data + off, n - off);
    if (w <= 0) return false;
    off += (size_t)w;
  }
  return true;
}

std::string open_url_command(const std::string& url) {
#ifdef __APPLE__
  return "open '" + url + "' >/dev/null 2>&1 &";
#else
  return "xdg-open '" + url + "' >/dev/null 2>&1 &";
#endif
}

std::string path_separator() { return "/"; }

void system_font_dirs(std::string& windows_fonts, std::string& user_fonts) {
  windows_fonts.clear();
  const char* home = getenv("HOME");
  if (!home) { user_fonts.clear(); return; }
#ifdef __APPLE__
  user_fonts = std::string(home) + "/Library/Fonts";
#else
  user_fonts = std::string(home) + "/.fonts";
#endif
}

}  // namespace plat
}  // namespace mdt

// ===========================================================================
//  Windows
// ===========================================================================
#else

#include <windows.h>

#include <cstdint>
#include <io.h>
#include <process.h>
#include <stdlib.h>

#include <algorithm>
#include <vector>

namespace mdt {
namespace plat {

static HANDLE g_in = INVALID_HANDLE_VALUE;
static HANDLE g_out = INVALID_HANDLE_VALUE;
static DWORD g_saved_in_mode = 0;
static DWORD g_saved_out_mode = 0;
static UINT g_saved_out_cp = 0;
static UINT g_saved_in_cp = 0;
static bool g_modes_saved = false;
static int g_last_cols = 0, g_last_rows = 0;
static bool g_have_last = false;
static void (*g_panic)() = nullptr;

// Windows calls this from a separate thread when the user hits Ctrl-C /
// Ctrl-Break, closes the console window or logs off.  Without it the console
// would stay in raw mode + alt screen with echo disabled.
static bool g_panic_ran = false;

static BOOL WINAPI console_ctrl_handler(DWORD type) {
  switch (type) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
      g_panic_ran = true;
      if (g_panic) g_panic();
      raw_end();
      // Ctrl-C/Ctrl-Break mean "stop": same 128+n convention as POSIX.
      if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT) ExitProcess(130);
      return TRUE;  // close/logoff/shutdown: Windows terminates us right after
    default:
      return FALSE;
  }
}

void set_panic_hook(void (*fn)()) {
  g_panic = fn;
  SetConsoleCtrlHandler(console_ctrl_handler, TRUE);
}

static DWORD WINAPI debug_signal_thread(LPVOID arg) {
  Sleep((DWORD)(uintptr_t)arg);
  // Ask the console for a Ctrl-Break, which is what a user pressing the key
  // combination produces (and unlike Ctrl-C it is not swallowed by input modes).
  GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, 0);
  Sleep(500);
  // Wine (and any host without working console events) reports success without
  // dispatching to the handler; call it directly so the restore path is still
  // exercised there.  On Windows this line is never reached.
  if (!g_panic_ran) console_ctrl_handler(CTRL_BREAK_EVENT);
  return 0;
}

void maybe_install_debug_signal() {
  const char* v = getenv("MDT_DEBUG_SIGNAL");
  if (!v || !*v) return;
  DWORD ms = (DWORD)(atoi(v) > 0 ? atoi(v) : 1000);
  HANDLE h = CreateThread(nullptr, 0, debug_signal_thread, (LPVOID)(uintptr_t)ms, 0, nullptr);
  if (h) CloseHandle(h);
}

bool stdin_is_tty() { return _isatty(_fileno(stdin)) != 0; }
bool stdout_is_tty() { return _isatty(_fileno(stdout)) != 0; }

bool raw_begin(std::string* err) {
  g_in = GetStdHandle(STD_INPUT_HANDLE);
  g_out = GetStdHandle(STD_OUTPUT_HANDLE);
  if (g_in == INVALID_HANDLE_VALUE || g_in == nullptr) {
    if (err) *err = "no console input handle";
    return false;
  }
  if (!GetConsoleMode(g_in, &g_saved_in_mode) || !GetConsoleMode(g_out, &g_saved_out_mode)) {
    if (err) *err = "stdin/stdout is not a Windows console";
    return false;
  }
  g_modes_saved = true;
  // UTF-8 in and out, VT sequences in both directions.
  g_saved_out_cp = GetConsoleOutputCP();
  g_saved_in_cp = GetConsoleCP();
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);
  DWORD in_mode = g_saved_in_mode;
  in_mode |= ENABLE_VIRTUAL_TERMINAL_INPUT | ENABLE_EXTENDED_FLAGS;
  in_mode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT |
               ENABLE_MOUSE_INPUT | ENABLE_QUICK_EDIT_MODE | ENABLE_INSERT_MODE);
  DWORD out_mode = g_saved_out_mode;
  out_mode |= ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING |
              DISABLE_NEWLINE_AUTO_RETURN;
  if (!SetConsoleMode(g_in, in_mode) || !SetConsoleMode(g_out, out_mode)) {
    if (err)
      *err = "cannot enable virtual terminal processing (needs Windows 10 1703+, "
             "or Windows Terminal / WezTerm / mintty)";
    return false;
  }
  return true;
}

void raw_end() {
  if (!g_modes_saved) return;
  SetConsoleMode(g_in, g_saved_in_mode);
  SetConsoleMode(g_out, g_saved_out_mode);
  if (g_saved_out_cp) SetConsoleOutputCP(g_saved_out_cp);
  if (g_saved_in_cp) SetConsoleCP(g_saved_in_cp);
  g_modes_saved = false;
}

bool window_size(int& cols, int& rows, int& px_w, int& px_h) {
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  HANDLE h = g_out != INVALID_HANDLE_VALUE ? g_out : GetStdHandle(STD_OUTPUT_HANDLE);
  if (!GetConsoleScreenBufferInfo(h, &csbi)) return false;
  int c = csbi.srWindow.Right - csbi.srWindow.Left + 1;
  int r = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
  if (c <= 0 || r <= 0) return false;
  cols = c;
  rows = r;
  if (px_w <= 0 || px_h <= 0) {
    // Windows consoles do not report pixel geometry; the CSI 16 t / 14 t probe
    // fills these in when the terminal answers (Windows Terminal does).
    CONSOLE_FONT_INFO cfi;
    if (GetCurrentConsoleFont(h, FALSE, &cfi)) {
      COORD sz = GetConsoleFontSize(h, cfi.nFont);
      if (sz.X > 0 && sz.Y > 0) {
        if (px_w <= 0) px_w = sz.X * cols;
        if (px_h <= 0) px_h = sz.Y * rows;
      }
    }
  }
  return true;
}

bool poll_resize() {
  int cols = 0, rows = 0, px = 0, py = 0;
  window_size(cols, rows, px, py);
  if (!g_have_last) {
    g_have_last = true;
    g_last_cols = cols;
    g_last_rows = rows;
    return false;
  }
  if (cols != g_last_cols || rows != g_last_rows) {
    g_last_cols = cols;
    g_last_rows = rows;
    return true;
  }
  return false;
}

bool wait_input(int timeout_ms) {
  HANDLE h = g_in != INVALID_HANDLE_VALUE ? g_in : GetStdHandle(STD_INPUT_HANDLE);
  DWORD r = WaitForSingleObject(h, (DWORD)timeout_ms);
  return r == WAIT_OBJECT_0;
}

int read_input(char* buf, size_t n) {
  HANDLE h = g_in != INVALID_HANDLE_VALUE ? g_in : GetStdHandle(STD_INPUT_HANDLE);
  DWORD got = 0;
  if (!ReadFile(h, buf, (DWORD)n, &got, nullptr)) {
    DWORD e = GetLastError();
    if (e == ERROR_BROKEN_PIPE || e == ERROR_HANDLE_EOF) return -1;
    return 0;
  }
  return (int)got;
}

bool write_out(const char* data, size_t n) {
  HANDLE h = g_out != INVALID_HANDLE_VALUE ? g_out : GetStdHandle(STD_OUTPUT_HANDLE);
  size_t off = 0;
  while (off < n) {
    // conhost refuses (or crawls on) very large console writes; 64 KB is the
    // size its own buffered writer uses.
    DWORD chunk = (DWORD)std::min<size_t>(n - off, (size_t)(64 * 1024));
    DWORD written = 0;
    if (!WriteFile(h, data + off, chunk, &written, nullptr) || written == 0) {
      // fall back to the CRT for redirected output
      size_t left = n - off;
      if (fwrite(data + off, 1, left, stdout) != left) return false;
      fflush(stdout);
      return true;
    }
    off += written;
  }
  return true;
}

std::string open_url_command(const std::string& url) {
  return "start \"\" \"" + url + "\"";
}

std::string path_separator() { return "\\"; }

void system_font_dirs(std::string& windows_fonts, std::string& user_fonts) {
  char win[MAX_PATH] = {0};
  UINT n2 = GetWindowsDirectoryA(win, MAX_PATH);
  windows_fonts = n2 ? std::string(win) + "\\Fonts" : "C:\\Windows\\Fonts";
  const char* local = getenv("LOCALAPPDATA");
  user_fonts = local ? std::string(local) + "\\Microsoft\\Windows\\Fonts" : "";
}

}  // namespace plat
}  // namespace mdt

#endif
