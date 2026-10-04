// mdt - markdown-terminal
// util.h : UTF-8 helpers, display width, small string utilities.
#pragma once
#include <cstdarg>
#include <cstdint>
#include <string>
#include <vector>

namespace mdt {

// ---------------------------------------------------------------- UTF-8 ----
// Decode the next code point starting at byte offset i (i is advanced).
// Returns 0xFFFD on malformed input (and advances by one byte).
uint32_t utf8_next(const std::string& s, size_t& i);
// Code point that ends at byte offset i (i is moved backwards).
uint32_t utf8_prev(const std::string& s, size_t& i);
std::string utf8_encode(uint32_t cp);
size_t utf8_count(const std::string& s);

// Terminal cell width of a code point (wcwidth-like):
//   0 = combining / zero width, 2 = wide (CJK, emoji), else 1.
int cp_width(uint32_t cp);
// Sum of cell widths of a UTF-8 string.
int str_width(const std::string& s);
// Number of code points.
int str_cp_len(const std::string& s);
// Byte offset after `cells` display columns starting at byte offset `from`.
size_t byte_off_for_cells(const std::string& s, size_t from, int cells);
// Substring by display columns [start, start+cells) (clamped).
std::string substr_cells(const std::string& s, int start, int cells);

// ------------------------------------------------------------- strings -----
bool starts_with(const std::string& s, const std::string& p);
bool ends_with(const std::string& s, const std::string& p);
std::string trim(const std::string& s);
std::string rtrim(const std::string& s);
std::string ltrim(const std::string& s);
std::string to_lower(std::string s);
std::string replace_all(std::string s, const std::string& from, const std::string& to);
std::vector<std::string> split(const std::string& s, char sep);
std::vector<std::string> split_lines(const std::string& s);
std::string join(const std::vector<std::string>& v, const std::string& sep);
std::string fmt(const char* f, ...) __attribute__((format(printf, 1, 2)));
// Collapse runs of whitespace into single spaces (for word wrapping input).
std::string collapse_ws(const std::string& s);

// -------------------------------------------------------------- base64 -----
std::string base64_encode(const uint8_t* data, size_t len);

// ---------------------------------------------------------------- misc -----
struct RGB { uint8_t r, g, b; };
inline bool operator==(const RGB& a, const RGB& b) { return a.r == b.r && a.g == b.g && a.b == b.b; }
inline bool operator!=(const RGB& a, const RGB& b) { return !(a == b); }
RGB parse_color(const std::string& s, RGB fallback);       // "#rrggbb" / "rrggbb" / "name"
std::string abs_path(const std::string& path);             // realpath-ish
std::string dir_name(const std::string& path);
bool file_exists(const std::string& path);
bool read_file(const std::string& path, std::string& out);
double now_ms();

}  // namespace mdt
