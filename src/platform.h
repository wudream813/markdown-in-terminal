// platform.h : the only place in mdt that talks to the OS directly.
//
// POSIX (Linux/macOS): termios raw mode, SIGWINCH, ioctl(TIOCGWINSZ), select().
// Windows (MinGW):     Win32 console modes, WaitForSingleObject + console
//                      input *records* (ReadConsoleInput) translated into the
//                      escape sequences a terminal would have sent, polling for
//                      resizes.  Redirected stdin still reads bytes.
#pragma once
#include <cstddef>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace mdt {
namespace plat {

// Windows: switch the console in/out code pages to UTF-8 and remember the
// originals (raw_end() puts them back).  Called first thing in main(), so
// --dump/--diag/--help and error messages with non-ASCII text are readable.
void console_init();

// Open a file whose path is UTF-8.  Windows console arguments and files are not
// UTF-8 (argv is in the ANSI code page, the file API is UTF-16), so everything
// that touches a path the user gave us goes through here.
FILE* open_file(const std::string& utf8_path, const char* mode);
bool regular_file_exists(const std::string& utf8_path);
// True when the path exists and is a directory (used to say *why* a file could
// not be read: "X is a directory, not a file").
bool is_directory(const std::string& utf8_path);

// The command line as UTF-8 (Windows: rebuilt from GetCommandLineW()).
std::vector<std::string> utf8_args(int argc, char** argv);

// Start a detached worker thread.  Returns false when the platform refuses
// (the caller then has to do the work inline).
bool thread_start(void (*fn)(void*), void* arg);
// Sleep, used by polling loops inside worker threads.
void sleep_ms(int ms);

bool stdin_is_tty();
bool stdout_is_tty();

// Switches the terminal into raw mode with VT input enabled.
bool raw_begin(std::string* err = nullptr);
void raw_end();
// A child process (xdg-open's fallback browser, a pager, ...) may share our
// tty and cook it behind our back.  The event loop polls this and restores.
bool raw_intact();
void raw_reassert();

// Registers a callback that runs when the process is killed from the outside:
// SIGINT/SIGTERM/SIGHUP on POSIX, Ctrl-C/Ctrl-Break/console close/logoff on
// Windows.  It runs in a signal/console context, so it may only use
// plat::write_out() and similar async-signal-safe calls, and must not return.
void set_panic_hook(void (*fn)());

// Test hook: when MDT_DEBUG_SIGNAL=<ms> is set, deliver the platform's "user
// killed us" event after that delay - SIGTERM on POSIX, a Ctrl-Break console
// event on Windows - so the emergency restore path can be tested by a script.
void maybe_install_debug_signal();

// Current text area in cells; pixel sizes are 0 when the terminal does not
// report them (then CSI 16 t / CSI 14 t probing fills them in).
bool window_size(int& cols, int& rows, int& px_w, int& px_h);

// True when the window changed size since the previous call.
bool poll_resize();

// Waits up to timeout_ms for input. Returns true when data may be available.
bool wait_input(int timeout_ms);

// What stdin is being read as, for --diag: "console records" / "byte stream".
// The two paths differ in ways that produced two bugs in issue #3 (mouse
// records and the idle behaviour), so a report should say which one ran.
const char* input_backend();
// Reads pending bytes; returns -1 on EOF, 0 when nothing was available.
int read_input(char* buf, size_t n);

// Writes everything to stdout (loops until done).
bool write_out(const char* data, size_t n);

// Windows: fetch `url` with the system WinINet stack (no libcurl, no shell).
// Linux/macOS return false so the caller can use libcurl or the curl binary.
bool http_get_system(const std::string& url, std::string& out, std::string* err);

// ---------------------------------------------------------------------------
// No shell anywhere.
//
// Until 0.3.7 these two places built a command line and handed it to
// popen()/system(), so a URL that came out of a document was parsed by a
// shell: `![](https://host/a";cmd;#)` turned a picture into a command, and so
// did a link opened with `o`.  Both helpers below pass their arguments
// straight to execvp()/CreateProcess(), where shell metacharacters are ordinary
// characters of one single argument.
// ---------------------------------------------------------------------------

// Runs `argv[0]` with the argument vector `argv` and collects its stdout.
// Returns true when the process ran; *exit_code is its status (-1 when it was
// killed, 127 when it could not be started at all).
bool run_capture(const std::vector<std::string>& argv, std::string& out, int* exit_code,
                 std::string* err);

// Opens `url` in the user's browser (`xdg-open`/`open` exec'd directly,
// ShellExecuteW() on Windows).  Only the schemes a document may reasonably
// point at are passed on - http, https, mailto, ftp, ftps, file, tel - so a
// link cannot ask the OS to execute a local file.  Returns false when there is
// no launcher or the scheme is not one of those.
bool open_url(const std::string& url);

// ------------------------------------------------------------------ watching --
// `--watch` asks the reader to reload the document when the file on disk
// changes.  There is no portable notification, so each platform uses its own
// and the last resort is a stat():
//
//   Linux    inotify, watching the *directory*: an editor saves by writing a
//            temporary file and renaming it over the original, so a watch on
//            the file's inode goes deaf after the first save.
//   macOS    kqueue, EVFILT_VNODE on two descriptors: the directory (saves
//            that replace the file, which is what a rename-over save is) and
//            the file itself (saves that write into it, which change no
//            directory entry and are therefore invisible to a directory
//            watch), moving the file watch to the new inode after a replace.
//   Windows  ReadDirectoryChangesW, also on the directory.
//   other    mtime + size, checked a few times a second.
//
// The notifier only says "something happened near this file"; the change
// itself is decided by comparing the file's stamp, so an unrelated write in
// the same directory does not reload the document.
struct FileStamp {
  long long mtime_ns = 0;
  long long size = 0;
};
// False when the file cannot be stat'ed (deleted, no permissions).
bool file_stamp(const std::string& utf8_path, FileStamp* out);

class Watch {
 public:
  explicit Watch(std::string path);
  virtual ~Watch() = default;
  // True when the file differs from the last call's state (a file that
  // disappeared counts as changed).  Waits at most timeout_ms for the
  // platform's notifier; 0 looks only at what already arrived.
  bool changed(int timeout_ms);

 protected:
  // Platform notification wait.  True when the kernel said something happened.
  virtual bool wait_event(int timeout_ms) = 0;

  std::string path_;

 private:
  FileStamp last_;
  bool had_ = false;
  bool have_ = false;
};

// Nullptr when this path cannot be watched (the reader then simply keeps the
// `r` key).  `utf8_path` must name a regular file.
std::unique_ptr<Watch> watch_file(const std::string& utf8_path);

// Directory holding the document's files (used to resolve relative paths).
std::string path_separator();

// Default font directories to look at for --screenshot.
void system_font_dirs(std::string& windows_fonts, std::string& user_fonts);

}  // namespace plat
}  // namespace mdt
