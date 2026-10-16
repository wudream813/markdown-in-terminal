#include "platform.h"
#include "util.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

// ===========================================================================
//  shared by both platforms
// ===========================================================================
namespace mdt {
namespace plat {
namespace {

// The schemes a document is allowed to hand to the OS launcher.  A Markdown
// link is user data: `ShellExecuteW` would happily run a bare `setup.exe`
// sitting next to the document, and a custom protocol handler is another way
// out of the reader, so anything that is not a plain web/mail/file URL is
// refused instead of passed on.
bool url_scheme_allowed(const std::string& url) {
  size_t colon = url.find(':');
  if (colon == std::string::npos || colon == 0) return false;  // relative path
  std::string scheme;
  for (size_t i = 0; i < colon; i++) {
    char c = url[i];
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    else if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.'))
      return false;
    scheme += c;
  }
  return scheme == "http" || scheme == "https" || scheme == "mailto" || scheme == "ftp" ||
         scheme == "ftps" || scheme == "file" || scheme == "tel";
}

// ------------------------------------------------------------- input log -----
// MDT_KEY_LOG=/path/to/file makes mdt append every chunk of input it receives,
// hex-dumped, together with what the read returned.  It exists because a bug
// report that says "the reader exits when I press an arrow key in <terminal>"
// cannot be diagnosed from the outside: the answer is either in the bytes the
// terminal sent or in the fact that none arrived, and this is the difference.
// It writes to a file the user names, never to the terminal (which the reader
// has taken over), and appends so several runs can be compared.
bool key_log_enabled() {
  const char* path = getenv("MDT_KEY_LOG");
  return path && *path;
}

static FILE* key_log_file() {
  static FILE* f = nullptr;
  static bool tried = false;
  if (!tried) {
    tried = true;
    const char* path = getenv("MDT_KEY_LOG");
    if (path && *path) f = fopen(path, "ab");
    if (f) fprintf(f, "--- mdt %s key log (build " __DATE__ " " __TIME__ ")\n", MDT_VERSION);
  }
  return f;
}

// One free-form line (mode readback, what the console decided to keep, ...).
void key_log_note(const char* text) {
  FILE* f = key_log_file();
  if (!f) return;
  fprintf(f, "          %s\n", text);
  fflush(f);
}

void key_log(const char* what, const char* data, size_t n) {
  FILE* f = key_log_file();
  if (!f) return;
  static double t0 = -1;
  double t = now_ms();
  if (t0 < 0) t0 = t;
  // Seconds since the first line of this run: two logs can then be compared
  // line by line, which an absolute timestamp does not make any easier here.
  fprintf(f, "[%8.3f] %s", (t - t0) / 1000.0, what);
  if (n) {
    fputs(":", f);
    for (size_t i = 0; i < n; i++) {
      unsigned char c = (unsigned char)data[i];
      if (c >= 0x20 && c < 0x7f) fprintf(f, " %c", c);
      else fprintf(f, " %02x", c);
    }
  }
  fputc('\n', f);
  fflush(f);
}

}  // namespace
}  // namespace plat
}  // namespace mdt

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
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include <cstdlib>

namespace mdt {
namespace plat {

static volatile sig_atomic_t g_winch = 0;
static void on_winch(int) { g_winch = 1; }
static struct termios* g_saved = nullptr;
static struct termios g_raw;      // the raw settings we want in force
static bool g_have_raw = false;
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

namespace {
struct ThreadStart {
  void (*fn)(void*);
  void* arg;
};
void* thread_trampoline(void* p) {
  ThreadStart ts = *(ThreadStart*)p;
  delete (ThreadStart*)p;
  ts.fn(ts.arg);
  return nullptr;
}
}  // namespace

bool thread_start(void (*fn)(void*), void* arg) {
  pthread_t t;
  auto* ts = new ThreadStart{fn, arg};
  if (pthread_create(&t, nullptr, thread_trampoline, ts) != 0) {
    delete ts;
    return false;
  }
  pthread_detach(t);
  return true;
}

void sleep_ms(int ms) {
  struct timespec ts;
  ts.tv_sec = ms / 1000;
  ts.tv_nsec = (long)(ms % 1000) * 1000000L;
  nanosleep(&ts, nullptr);
}

FILE* open_file(const std::string& path, const char* mode) { return fopen(path.c_str(), mode); }

bool regular_file_exists(const std::string& path) {
  struct stat st;
  return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool is_directory(const std::string& path) {
  struct stat st;
  return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

void console_init() {}  // nothing to do: the terminal is UTF-8 already

std::vector<std::string> utf8_args(int argc, char** argv) {
  return std::vector<std::string>(argv, argv + argc);
}

bool stdin_is_tty() { return isatty(STDIN_FILENO) == 1; }
bool stdout_is_tty() { return isatty(STDOUT_FILENO) == 1; }

bool raw_begin(std::string* err) {
  if (key_log_enabled()) {
    // One line about the tty in the same log the key bytes go to: "the keys look
    // wrong" reports usually come down to whether raw mode was really on.
    char note[120];
    snprintf(note, sizeof note, "termios: switching to raw mode (%s), ECHO/ICANON off",
             isatty(STDIN_FILENO) ? "stdin is a tty" : "stdin is NOT a tty");
    key_log_note(note);
  }
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
  g_raw = raw;
  g_have_raw = true;
  signal(SIGWINCH, on_winch);
  signal(SIGPIPE, SIG_IGN);
  return true;
}

bool raw_intact() {
  if (!g_have_raw) return true;
  // Test hook, Windows section has the long explanation: pretend the terminal
  // will not keep the modes, which used to make the event loop redraw forever.
  if (getenv("MDT_FAKE_STALE_MODE")) return false;
  struct termios cur;
  if (tcgetattr(STDIN_FILENO, &cur) != 0) return true;
  return cur.c_lflag == g_raw.c_lflag && cur.c_iflag == g_raw.c_iflag &&
         cur.c_oflag == g_raw.c_oflag && cur.c_cc[VMIN] == g_raw.c_cc[VMIN] &&
         cur.c_cc[VTIME] == g_raw.c_cc[VTIME];
}

void raw_reassert() {
  if (g_have_raw) tcsetattr(STDIN_FILENO, TCSANOW, &g_raw);
}

void raw_end() {
  g_have_raw = false;
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

const char* input_backend() { return "posix tty (bytes)"; }

int read_input(char* buf, size_t n) {
  ssize_t r = read(STDIN_FILENO, buf, n);
  if (key_log_enabled())
    key_log(r > 0 ? "read" : (r == 0 ? "read EOF" : "read nothing"), r > 0 ? buf : "",
            r > 0 ? (size_t)r : 0);
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

// POSIX has no system HTTP stack: the caller uses libcurl or the curl binary.
bool http_get_system(const std::string& url, std::string& out, std::string* err) {
  (void)url; (void)out; (void)err;
  return false;
}

// Neither helper builds a command line: argv goes to execvp() as it is, so a
// `"`, `;`, `$` or backtick in a URL stays part of that one argument.
bool run_capture(const std::vector<std::string>& argv, std::string& out, int* exit_code,
                 std::string* err) {
  if (exit_code) *exit_code = -1;
  if (argv.empty()) {
    if (err) *err = "empty command";
    return false;
  }
  // Prepare everything before fork(): after fork() a multi-threaded program may
  // only call async-signal-safe functions in the child (this runs on the image
  // download worker thread too), so no allocation happens down there.
  std::vector<char*> cargv;
  cargv.reserve(argv.size() + 1);
  for (const std::string& a : argv) cargv.push_back(const_cast<char*>(a.c_str()));
  cargv.push_back(nullptr);

  int fds[2];
  if (pipe(fds) != 0) {
    if (err) *err = "pipe() failed";
    return false;
  }
  pid_t pid = fork();
  if (pid < 0) {
    close(fds[0]);
    close(fds[1]);
    if (err) *err = "fork() failed";
    return false;
  }
  if (pid == 0) {
    // child: stdout into the pipe, stdin/stderr to /dev/null (the program must
    // not be able to read from the reader's terminal)
    int devnull = open("/dev/null", O_RDWR);
    dup2(fds[1], STDOUT_FILENO);
    if (devnull >= 0) {
      dup2(devnull, STDIN_FILENO);
      dup2(devnull, STDERR_FILENO);
    }
    close(fds[0]);
    if (fds[1] != STDOUT_FILENO) close(fds[1]);
    if (devnull > STDERR_FILENO) close(devnull);
    execvp(cargv[0], cargv.data());
    _exit(127);  // 127 == could not be started
  }
  close(fds[1]);
  char buf[65536];
  ssize_t n;
  while ((n = read(fds[0], buf, sizeof buf)) > 0) out.append(buf, (size_t)n);
  close(fds[0]);
  int st = 0;
  while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {
  }
  int code = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
  if (exit_code) *exit_code = code;
  if (code == 127) {
    if (err) *err = "cannot run " + argv[0];
    return false;
  }
  return true;
}

bool open_url(const std::string& url) {
  if (!url_scheme_allowed(url)) return false;
#ifdef __APPLE__
  const char* launcher = "open";
#else
  const char* launcher = "xdg-open";
#endif
  char* cargv[3] = {const_cast<char*>(launcher), const_cast<char*>(url.c_str()), nullptr};
  int ep[2];  // CLOEXEC: EOF means "the launcher is running", a write means "it failed"
  if (pipe(ep) != 0) return false;
  fcntl(ep[0], F_SETFD, FD_CLOEXEC);
  fcntl(ep[1], F_SETFD, FD_CLOEXEC);
  pid_t pid = fork();
  if (pid < 0) {
    close(ep[0]);
    close(ep[1]);
    return false;
  }
  if (pid == 0) {
    setsid();  // the browser must not hold (or keep open) our terminal
    pid_t middle = fork();
    if (middle != 0) _exit(0);  // the middle child leaves at once, so the
                                // browser is never our child (no zombie, no wait)
    int devnull = open("/dev/null", O_RDWR);
    if (devnull >= 0) {
      dup2(devnull, STDIN_FILENO);
      dup2(devnull, STDOUT_FILENO);
      dup2(devnull, STDERR_FILENO);
      if (devnull > STDERR_FILENO) close(devnull);
    }
    execvp(cargv[0], cargv);
    int e = errno;
    ssize_t ignored = write(ep[1], &e, sizeof e);
    (void)ignored;
    _exit(127);
  }
  close(ep[1]);
  int child_errno = 0;
  ssize_t got = read(ep[0], &child_errno, sizeof child_errno);
  close(ep[0]);
  int st = 0;
  while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {
  }
  (void)child_errno;
  return got == 0;
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

#include <shellapi.h>
#include <wininet.h>  // WinINet: remote pictures

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
// True when stdin is a console we read with ReadConsoleInput (see the long
// comment above read_input).  A console is never at EOF.
static bool g_in_is_console = false;
// Did the console keep ENABLE_VIRTUAL_TERMINAL_INPUT?  Both answers are useful:
// with it, the records carry the VT byte stream (one byte per record) and an
// incoming mouse report or capability answer stays readable; without it, the
// records are real key events and the key table below turns them into
// sequences.  The translation accepts either (see key_event_to_vt).
static bool g_vt_input_kept = false;

// Real Windows console, or Wine's?  Wine exports wine_get_version() from
// ntdll, which is the documented way to ask.  Nothing depends on the answer any
// more (the input path below copes with either), but the key log says which
// host it was talking to, which is what one wants to know when a report says
// "the keys did nothing".
static bool console_is_wine() {
  HMODULE ntdll = GetModuleHandleA("ntdll.dll");
  return ntdll != nullptr && GetProcAddress(ntdll, "wine_get_version") != nullptr;
}
static std::string g_win_pending;   // translated bytes waiting to be handed out
static DWORD g_saved_in_mode = 0;
static DWORD g_saved_out_mode = 0;
static DWORD g_raw_in_mode = 0, g_raw_out_mode = 0;
static bool g_have_raw_modes = false;
static UINT g_saved_out_cp = 0;
static bool g_cp_saved = false;
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

static std::wstring utf8_to_wide(const std::string& s) {
  if (s.empty()) return std::wstring();
  int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
  if (n <= 0) return std::wstring();
  std::wstring w((size_t)n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
  return w;
}
static std::string wide_to_utf8(const wchar_t* w, int len) {
  if (!w || len <= 0) return std::string();
  int n = WideCharToMultiByte(CP_UTF8, 0, w, len, nullptr, 0, nullptr, nullptr);
  if (n <= 0) return std::string();
  std::string out((size_t)n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w, len, &out[0], n, nullptr, nullptr);
  return out;
}

// Windows console arguments are in the ANSI code page, not UTF-8: a Chinese
// file name arrives as GBK bytes and comes out as mojibake.  Rebuild the whole
// command line from its wide form instead.
std::vector<std::string> utf8_args(int argc, char** argv) {
  std::vector<std::string> out;
  int n = 0;
  LPWSTR* w = CommandLineToArgvW(GetCommandLineW(), &n);
  if (w) {
    for (int i = 0; i < n; i++) out.push_back(wide_to_utf8(w[i], (int)wcslen(w[i])));
    LocalFree(w);
    if (!out.empty()) return out;
  }
  for (int i = 0; i < argc; i++) out.push_back(argv[i]);
  return out;
}

struct ThreadStart {
  void (*fn)(void*);
  void* arg;
};
static DWORD WINAPI win_thread_trampoline(LPVOID p) {
  ThreadStart ts = *(ThreadStart*)p;
  delete (ThreadStart*)p;
  ts.fn(ts.arg);
  return 0;
}

bool thread_start(void (*fn)(void*), void* arg) {
  auto* ts = new ThreadStart{fn, arg};
  HANDLE h = CreateThread(nullptr, 0, win_thread_trampoline, ts, 0, nullptr);
  if (!h) {
    delete ts;
    return false;
  }
  CloseHandle(h);
  return true;
}

void sleep_ms(int ms) { Sleep((DWORD)ms); }

FILE* open_file(const std::string& path, const char* mode) {
  std::wstring wpath = utf8_to_wide(path);
  std::wstring wmode(mode, mode + std::strlen(mode));
  return _wfopen(wpath.c_str(), wmode.c_str());
}

bool regular_file_exists(const std::string& path) {
  DWORD a = GetFileAttributesW(utf8_to_wide(path).c_str());
  return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool is_directory(const std::string& path) {
  DWORD a = GetFileAttributesW(utf8_to_wide(path).c_str());
  return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

// Do this before anything is printed: --dump, --diag and error messages go
// through the CRT, which encodes stdout with the console code page.
void console_init() {
  if (!g_cp_saved) {
    g_saved_out_cp = GetConsoleOutputCP();
    g_saved_in_cp = GetConsoleCP();
    g_cp_saved = true;
  }
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);
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
  if (!g_cp_saved) {  // console_init() usually got there first
    g_saved_out_cp = GetConsoleOutputCP();
    g_saved_in_cp = GetConsoleCP();
    g_cp_saved = true;
  }
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);
  // Input: ask the console for INPUT_RECORDs rather than a character stream.
  //
  // This used to enable ENABLE_VIRTUAL_TERMINAL_INPUT and read bytes with
  // ReadFile, which is what Windows Terminal and mintty need.  The console's
  // own documentation is blunt about the limits of that path: ReadFile
  // "does not return keyboard events involving the function keys or arrow
  // keys", and mouse, window and focus events "are discarded" - and even with
  // ENABLE_MOUSE_INPUT set, "these events are discarded by ReadFile ... The
  // ReadConsoleInput function can be used to read MOUSE_EVENT input records".
  // So on a host whose VT input translation is missing or partial (WezTerm and
  // other ConPTY users have been through this) an arrow key or a click can
  // arrive as nothing at all - or, worse, as a read that reports no bytes,
  // which used to look like end-of-input and quietly ended the session with
  // status 0 (issue #3).
  //
  // Reading records ourselves fixes both halves: every key and mouse event is
  // translated into the same VT sequences the parser already understands, and
  // a console can no longer look like a closed pipe.
  // ENABLE_MOUSE_INPUT is what puts MOUSE_EVENT records in the buffer at all
  // (without it the console throws mouse events away before anyone can read
  // them).  Window-buffer-size events are deliberately not asked for: mdt
  // polls the size itself, and every bit asked for is a bit some host may
  // refuse to keep.
  // ENABLE_VIRTUAL_TERMINAL_INPUT is what makes a Windows terminal hand mouse
  // reports over as VT bytes: WezTerm's ConPTY does not turn them into
  // MOUSE_EVENT records, so with the bit off a click never arrives (issue #3).
  // Both record shapes are translated either way (see key_event_to_vt).
  DWORD in_mode = g_saved_in_mode;
  in_mode |= ENABLE_EXTENDED_FLAGS | ENABLE_MOUSE_INPUT | ENABLE_VIRTUAL_TERMINAL_INPUT;
  in_mode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT |
               ENABLE_QUICK_EDIT_MODE | ENABLE_INSERT_MODE | ENABLE_WINDOW_INPUT);
  DWORD out_mode = g_saved_out_mode;
  out_mode |= ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING |
              DISABLE_NEWLINE_AUTO_RETURN;
  if (!SetConsoleMode(g_in, in_mode) || !SetConsoleMode(g_out, out_mode)) {
    if (err)
      *err = "cannot enable virtual terminal processing (needs Windows 10 1703+, "
             "or Windows Terminal / WezTerm / mintty)";
    return false;
  }
  // Store what the console says it kept, not what was asked for.  A console
  // does not have to keep every bit (ConPTY drops some), and comparing against
  // the request made raw_intact() call the mode damaged on every single pass of
  // the event loop: the reader then re-sent "enter the alternate screen" about
  // six hundred times a second, which is the flickering reported in issue #3.
  DWORD in_now = in_mode, out_now = out_mode;
  GetConsoleMode(g_in, &in_now);
  GetConsoleMode(g_out, &out_now);
  g_raw_in_mode = in_now;
  g_raw_out_mode = out_now;
  g_vt_input_kept = (in_now & ENABLE_VIRTUAL_TERMINAL_INPUT) != 0;
  g_have_raw_modes = true;
  if (key_log_enabled()) {
    char note[200];
    snprintf(note, sizeof note,
             "console modes: asked in=0x%08lx out=0x%08lx -> kept in=0x%08lx out=0x%08lx "
             "(mouse events %s, vt input %s, host %s)",
             (unsigned long)in_mode, (unsigned long)out_mode, (unsigned long)in_now,
             (unsigned long)out_now, (in_now & ENABLE_MOUSE_INPUT) ? "on" : "REFUSED",
             g_vt_input_kept ? "on" : "REFUSED", console_is_wine() ? "wine" : "windows");
    key_log_note(note);
  }
  g_in_is_console = true;   // GetConsoleMode() succeeded on stdin above
  g_win_pending.clear();
  return true;
}

bool raw_intact() {
  if (!g_have_raw_modes) return true;
  // Test hook: pretend the host silently drops mode bits, which is what turned
  // the "did a child cook my tty" check into a redraw storm on Windows
  // (issue #3).  Used by tools/idle_frame_test.py, not documented for users.
  if (getenv("MDT_FAKE_STALE_MODE")) return false;
  DWORD in_mode = 0, out_mode = 0;
  if (!GetConsoleMode(g_in, &in_mode) || !GetConsoleMode(g_out, &out_mode)) return true;
  return in_mode == g_raw_in_mode && out_mode == g_raw_out_mode;
}

void raw_reassert() {
  if (g_have_raw_modes) {
    SetConsoleMode(g_in, g_raw_in_mode);
    SetConsoleMode(g_out, g_raw_out_mode);
  }
}

static void fall_back_to_bytes() {
  g_in_is_console = false;
  g_win_pending.clear();
  HANDLE h = g_in != INVALID_HANDLE_VALUE ? g_in : GetStdHandle(STD_INPUT_HANDLE);
  DWORD mode = g_have_raw_modes ? g_raw_in_mode : 0;
  if (!mode) return;
  mode |= ENABLE_VIRTUAL_TERMINAL_INPUT;
  mode &= ~(ENABLE_MOUSE_INPUT | ENABLE_WINDOW_INPUT);
  SetConsoleMode(h, mode);
}

void raw_end() {
  g_have_raw_modes = false;
  g_in_is_console = false;
  g_win_pending.clear();
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
  if (!g_in_is_console) {
    DWORD r = WaitForSingleObject(h, (DWORD)timeout_ms);
    return r == WAIT_OBJECT_0;
  }
  // A console input handle reports itself as ready even when its buffer is
  // empty, so WaitForSingleObject is useless here: trusting it turned the
  // event loop into a spin that polled ~600 times a second (issue #3, and the
  // reason the reader looked like it was flickering).  The buffer count is the
  // only honest answer, so this polls it and sleeps in between.  The sleep
  // grows to a few milliseconds, which keeps a key press feeling immediate
  // while an idle reader costs a handful of wake-ups per second-pause.
  if (timeout_ms < 0) timeout_ms = 0;
  DWORD deadline = GetTickCount() + (DWORD)timeout_ms;
  for (int nap = 1;;) {
    DWORD available = 0;
    if (!GetNumberOfConsoleInputEvents(h, &available)) return true;  // read_input decides
    if (available > 0) return true;
    // The count can be a lie: under Wine 9 (the one CI's runner installs) it
    // stays 0 while keys and mouse reports are already waiting, so the reader
    // never woke up and CI saw "no input at all - 0 empty reads".  PeekConsole-
    // Input is the second opinion; it does not consume a record, so on a
    // console that answers honestly this is one extra syscall per nap and
    // nothing else changes.
    INPUT_RECORD rec;
    DWORD peeked = 0;
    if (PeekConsoleInputW(h, &rec, 1, &peeked) && peeked > 0) return true;
    if ((long)(GetTickCount() - deadline) >= 0) return false;
    Sleep((DWORD)nap);
    if (nap < 8) nap++;
  }
}

// ---------------------------------------------------------------- records ----
// One code point, as UTF-8.
static void append_utf8(std::string& out, unsigned int cp) {
  if (cp < 0x80) {
    out += (char)cp;
  } else if (cp < 0x800) {
    out += (char)(0xC0 | (cp >> 6));
    out += (char)(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += (char)(0xE0 | (cp >> 12));
    out += (char)(0x80 | ((cp >> 6) & 0x3F));
    out += (char)(0x80 | (cp & 0x3F));
  } else {
    out += (char)(0xF0 | (cp >> 18));
    out += (char)(0x80 | ((cp >> 12) & 0x3F));
    out += (char)(0x80 | ((cp >> 6) & 0x3F));
    out += (char)(0x80 | (cp & 0x3F));
  }
}

// KEY_EVENT -> the bytes the parser expects: the character itself when the
// console gave one (letters, digits, Enter as CR, Tab, Backspace, Escape as
// 0x1b, Ctrl-C as 0x03), otherwise the VT sequence for the named key.
static void key_event_to_vt(std::string& out, const KEY_EVENT_RECORD& k) {
  if (!k.bKeyDown) return;                     // key release: the reader is edge-driven
  if (k.uChar.UnicodeChar) {
    // One character.  Values below 0x100 are appended as a byte: with
    // ENABLE_VIRTUAL_TERMINAL_INPUT the console queues the VT *byte* stream one
    // byte per record (so a UTF-8 character is several records, and adding up
    // their bytes rebuilds it exactly), and an ordinary key press carries its
    // own character anyway.  Anything wider is a wide character typed on a
    // console without VT input, which gets encoded properly.
    if (k.uChar.UnicodeChar < 0x100) out += (char)(k.uChar.UnicodeChar & 0xFF);
    else append_utf8(out, k.uChar.UnicodeChar);
    return;
  }
  const DWORD s = k.dwControlKeyState;
  int mod = 1;
  if (s & SHIFT_PRESSED) mod += 1;
  if (s & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) mod += 2;
  if (s & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) mod += 4;
  char final = 0;
  int tilde = 0;
  switch (k.wVirtualKeyCode) {
    case VK_UP: final = 'A'; break;
    case VK_DOWN: final = 'B'; break;
    case VK_RIGHT: final = 'C'; break;
    case VK_LEFT: final = 'D'; break;
    case VK_HOME: final = 'H'; break;
    case VK_END: final = 'F'; break;
    case VK_PRIOR: tilde = 5; break;           // Page Up
    case VK_NEXT: tilde = 6; break;            // Page Down
    case VK_DELETE: tilde = 3; break;
    case VK_INSERT: tilde = 2; break;
    case VK_F1: case VK_F2: case VK_F3: case VK_F4:
      out += "\x1bO";
      out += (char)('P' + (k.wVirtualKeyCode - VK_F1));
      return;
    case VK_F5: tilde = 15; break;
    case VK_F6: tilde = 17; break;
    case VK_F7: tilde = 18; break;
    case VK_F8: tilde = 19; break;
    case VK_F9: tilde = 20; break;
    case VK_F10: tilde = 21; break;
    case VK_F11: tilde = 23; break;
    case VK_F12: tilde = 24; break;
    case VK_BACK: out += "\x7f"; return;
    case VK_ESCAPE: out += "\x1b"; return;
    case VK_RETURN: out += "\r"; return;
    case VK_TAB: out += (s & SHIFT_PRESSED) ? "\x1b[Z" : "\t"; return;
    default: return;                           // nothing the reader knows
  }
  char buf[32];
  if (tilde) {
    if (mod == 1) snprintf(buf, sizeof buf, "\x1b[%d~", tilde);
    else snprintf(buf, sizeof buf, "\x1b[%d;%d~", tilde, mod);
  } else {
    if (mod == 1) snprintf(buf, sizeof buf, "\x1b[%c", final);
    else snprintf(buf, sizeof buf, "\x1b[1;%d%c", mod, final);
  }
  out += buf;
}

// MOUSE_EVENT -> SGR mouse report.  Windows already reports the position in
// character cells, and SGR counts from 1, which is the same convention the
// terminal path uses - so the parser cannot tell the two apart.
static void mouse_event_to_vt(std::string& out, const MOUSE_EVENT_RECORD& m) {
  int x = m.dwMousePosition.X + 1, y = m.dwMousePosition.Y + 1;
  DWORD flags = m.dwEventFlags, buttons = m.dwButtonState;
  int code = 0;
  char final = 'M';
  if (flags & MOUSE_WHEELED) {
    code = ((short)HIWORD(buttons) > 0) ? 64 : 65;   // away from the user = up
  } else if (flags & MOUSE_HWHEELED) {
    code = ((short)HIWORD(buttons) > 0) ? 66 : 67;
  } else {
    if (buttons & FROM_LEFT_1ST_BUTTON_PRESSED) code = 0;
    else if (buttons & FROM_LEFT_2ND_BUTTON_PRESSED) code = 1;
    else if (buttons & RIGHTMOST_BUTTON_PRESSED) code = 2;
    else code = 3;                                   // no button held
    if (flags & MOUSE_MOVED) code |= 32;             // motion (hover or drag)
    else if (code == 3) final = 'm';                 // a button went up
    if (code == 3 + 32) final = 'M';
  }
  char buf[48];
  snprintf(buf, sizeof buf, "\x1b[<%d;%d;%d%c", code, x, y, final);
  out += buf;
}

// Last resort: give up on records and read bytes, switching the console's own
// VT translation back on so the behaviour is what it was before records were
// used at all.  Reached when the handle claims to be a console but refuses to
// give us records; carrying on regardless could spin on a handle that stays
// readable.
static void fall_back_to_bytes();

// Drain what the console has, translating it.  Never reports EOF: a console
// has no end of input, and pretending it did is what made an arrow key end the
// session (issue #3).  Returns -1 when the console cannot be read this way.
static int win_console_read(char* buf, size_t n) {
  HANDLE h = g_in != INVALID_HANDLE_VALUE ? g_in : GetStdHandle(STD_INPUT_HANDLE);
  if (!g_win_pending.empty()) {
    size_t take = g_win_pending.size() < n ? g_win_pending.size() : n;
    memcpy(buf, g_win_pending.data(), take);
    g_win_pending.erase(0, take);
    return (int)take;
  }
  // Take everything the console has queued, not just one batch: when the
  // console delivers the VT byte stream, one key press is *several* records,
  // and handing them out in separate reads would let the parser meet a lone
  // ESC - which is the bug this whole path exists to avoid.
  DWORD available = 0;
  if (!GetNumberOfConsoleInputEvents(h, &available)) return -1;
  if (available == 0) return 0;
  INPUT_RECORD recs[128];
  for (int round = 0; round < 8; round++) {
    DWORD want = available < 128 ? available : 128;
    DWORD got = 0;
    if (!ReadConsoleInputW(h, recs, want, &got)) return -1;  // not a usable console
    for (DWORD i = 0; i < got; i++) {
      switch (recs[i].EventType) {
        case KEY_EVENT: key_event_to_vt(g_win_pending, recs[i].Event.KeyEvent); break;
        case MOUSE_EVENT: mouse_event_to_vt(g_win_pending, recs[i].Event.MouseEvent); break;
        case WINDOW_BUFFER_SIZE_EVENT: break;  // poll_resize() compares the size itself
        case FOCUS_EVENT: break;
        case MENU_EVENT: break;
        default: break;
      }
    }
    if (got == 0 || g_win_pending.size() >= 4096) break;
    if (!GetNumberOfConsoleInputEvents(h, &available) || available == 0) break;
  }
  if (g_win_pending.empty()) return 0;
  return win_console_read(buf, n);
}

const char* input_backend() {
  return g_in_is_console ? "windows console records" : "windows byte stream";
}

int read_input(char* buf, size_t n) {
  if (g_in_is_console) {
    int got = win_console_read(buf, n);
    if (got >= 0) {
      if (key_log_enabled()) {
        if (got > 0) key_log("console input", buf, (size_t)got);
        else key_log("console input: nothing this time", nullptr, 0);
      }
      return got;                   // 0 == nothing right now, never EOF
    }
    if (key_log_enabled()) key_log("console replies only to bytes, falling back", nullptr, 0);
    fall_back_to_bytes();
  }
  HANDLE h = g_in != INVALID_HANDLE_VALUE ? g_in : GetStdHandle(STD_INPUT_HANDLE);
  DWORD got = 0;
  if (!ReadFile(h, buf, (DWORD)n, &got, nullptr)) {
    DWORD e = GetLastError();
    if (key_log_enabled()) key_log(e == ERROR_BROKEN_PIPE || e == ERROR_HANDLE_EOF
                                       ? "read EOF (redirected stdin)"
                                       : "read error",
                                   nullptr, 0);
    if (e == ERROR_BROKEN_PIPE || e == ERROR_HANDLE_EOF) return -1;
    return 0;
  }
  if (key_log_enabled()) key_log(got ? "read" : "read nothing", buf, (size_t)got);
  return (int)got;
}

bool write_out(const char* data, size_t n) {
  HANDLE h = g_out != INVALID_HANDLE_VALUE ? g_out : GetStdHandle(STD_OUTPUT_HANDLE);
  size_t off = 0;
  while (off < n) {
    // A whole frame is one WriteFile, and a console write of many megabytes is
    // not something a console can take in one gulp: the host will display what
    // it managed to buffer and mdt keeps going, which a reader scrolling past a
    // big document feels as flicker (issue #3).  A few kilobytes per call is
    // what a terminal keeps up with.  ConPTY's own input buffer is 4 KB, and
    // this stays clear of it.
    DWORD chunk = (DWORD)std::min<size_t>(n - off, (size_t)(4 * 1024));
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

// Remote pictures on Windows go through WinINet: it is part of the system, so
// the reader needs no libcurl and no curl.exe on the PATH.
bool http_get_system(const std::string& url, std::string& out, std::string* err) {
  HINTERNET net = InternetOpenA("mdt/0.1 (+terminal markdown reader)",
                                INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
  if (!net) { if (err) *err = "cannot open the internet session"; return false; }
  HINTERNET req = InternetOpenUrlA(net, url.c_str(), nullptr, 0,
                                   INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
  if (!req) {
    DWORD e = GetLastError();
    if (err) *err = "http failed (wininet " + std::to_string((long)e) + ")";
    InternetCloseHandle(net);
    return false;
  }
  // The response length is known from the headers: a short read is a broken
  // download, not an end of data (a truncated PNG decodes to "chunk not known",
  // which looks like a decoder bug).
  long long want = -1;
  {
    char len[64] = {0};
    DWORD ln = sizeof(len), idx = 0;
    if (HttpQueryInfoA(req, HTTP_QUERY_CONTENT_LENGTH, len, &ln, &idx))
      want = _strtoi64(len, nullptr, 10);
  }
  char buf[65536];
  bool ok = true;
  for (;;) {
    DWORD n = 0;
    if (!InternetReadFile(req, buf, sizeof(buf), &n)) {
      DWORD e = GetLastError();
      if (e != ERROR_SUCCESS && e != ERROR_HANDLE_EOF) {
        ok = false;
        if (err) *err = "download failed (wininet " + std::to_string((long)e) + ")";
      }
      break;
    }
    if (n == 0) break;  // end of data
    out.append(buf, n);
    if (out.size() > 32u * 1024 * 1024) { ok = false; if (err) *err = "picture too large"; break; }
  }
  if (ok && want > 0 && (long long)out.size() != want) {
    ok = false;
    if (err) *err = "short download (" + std::to_string((long long)out.size()) + "/" +
                    std::to_string(want) + " bytes)";
  }
  InternetCloseHandle(req);
  InternetCloseHandle(net);
  if (out.empty() && ok) { if (err) *err = "empty response"; return false; }
  return ok;
}

// Quotes one argument the way the CRT's command line parser expects it again
// (backslashes before a quote and trailing backslashes are doubled).  With this
// the URL stays a single argument and still reaches the program byte for byte.
static std::wstring win_quote_arg(const std::string& a) {
  std::wstring w = utf8_to_wide(a);
  std::wstring r(1, L'"');
  size_t backslashes = 0;
  for (size_t i = 0; i < w.size(); i++) {
    if (w[i] == L'\\') {
      backslashes++;
      continue;
    }
    if (w[i] == L'"') {
      r.append(backslashes * 2 + 1, L'\\');
      r.push_back(L'"');
    } else {
      r.append(backslashes, L'\\');
      r.push_back(w[i]);
    }
    backslashes = 0;
  }
  r.append(backslashes * 2, L'\\');
  r.push_back(L'"');
  return r;
}

bool run_capture(const std::vector<std::string>& argv, std::string& out, int* exit_code,
                 std::string* err) {
  if (exit_code) *exit_code = -1;
  if (argv.empty()) {
    if (err) *err = "empty command";
    return false;
  }
  SECURITY_ATTRIBUTES sa;
  sa.nLength = sizeof sa;
  sa.lpSecurityDescriptor = nullptr;
  sa.bInheritHandle = TRUE;
  HANDLE rd = INVALID_HANDLE_VALUE, wr = INVALID_HANDLE_VALUE;
  if (!CreatePipe(&rd, &wr, &sa, 0)) {
    if (err) *err = "CreatePipe failed";
    return false;
  }
  SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
  HANDLE nul = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           &sa, OPEN_EXISTING, 0, nullptr);
  std::wstring line;
  for (size_t i = 0; i < argv.size(); i++) {
    if (i) line.push_back(L' ');
    line += win_quote_arg(argv[i]);
  }
  std::vector<wchar_t> cmd(line.begin(), line.end());
  cmd.push_back(L'\0');  // CreateProcessW may modify the buffer it is given
  STARTUPINFOW si;
  ZeroMemory(&si, sizeof si);
  si.cb = sizeof si;
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = nul;
  si.hStdOutput = wr;
  si.hStdError = nul;
  PROCESS_INFORMATION pi;
  ZeroMemory(&pi, sizeof pi);
  BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                           nullptr, &si, &pi);
  CloseHandle(wr);
  if (!ok) {
    CloseHandle(rd);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (err) *err = "cannot run " + argv[0];
    return false;
  }
  char buf[65536];
  DWORD got = 0;
  while (ReadFile(rd, buf, (DWORD)sizeof buf, &got, nullptr) && got > 0) out.append(buf, got);
  CloseHandle(rd);
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 0;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
  if (exit_code) *exit_code = (int)code;
  return true;
}

bool open_url(const std::string& url) {
  if (!url_scheme_allowed(url)) return false;
  std::wstring w = utf8_to_wide(url);
  // ShellExecuteW takes the URL as an argument - `start` in a shell cannot be
  // used for this, cmd.exe would parse `&`, `|` and `^` inside it.
  HINSTANCE r = ShellExecuteW(nullptr, L"open", w.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
  return (INT_PTR)r > 32;
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
