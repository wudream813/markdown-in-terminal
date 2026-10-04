// platform.h : the only place in mdt that talks to the OS directly.
//
// POSIX (Linux/macOS): termios raw mode, SIGWINCH, ioctl(TIOCGWINSZ), select().
// Windows (MinGW):     Win32 console modes with ENABLE_VIRTUAL_TERMINAL_*,
//                      WaitForSingleObject/ReadFile, polling for resizes.
#pragma once
#include <cstddef>
#include <string>

namespace mdt {
namespace plat {

bool stdin_is_tty();
bool stdout_is_tty();

// Switches the terminal into raw mode with VT input enabled.
bool raw_begin(std::string* err = nullptr);
void raw_end();

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
// Reads pending bytes; returns -1 on EOF, 0 when nothing was available.
int read_input(char* buf, size_t n);

// Writes everything to stdout (loops until done).
bool write_out(const char* data, size_t n);

// Command that opens `url` in the user's browser.
std::string open_url_command(const std::string& url);

// Directory holding the document's files (used to resolve relative paths).
std::string path_separator();

// Default font directories to look at for --screenshot.
void system_font_dirs(std::string& windows_fonts, std::string& user_fonts);

}  // namespace plat
}  // namespace mdt
