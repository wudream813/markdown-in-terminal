#include "util.h"

#include "platform.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstring>
#include <limits.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace mdt {

// ---------------------------------------------------------------- UTF-8 ----
uint32_t utf8_next(const std::string& s, size_t& i) {
  if (i >= s.size()) return 0;
  unsigned char c = (unsigned char)s[i];
  if (c < 0x80) { i += 1; return c; }
  int n = 0; uint32_t cp = 0;
  if ((c & 0xE0) == 0xC0) { n = 1; cp = c & 0x1F; }
  else if ((c & 0xF0) == 0xE0) { n = 2; cp = c & 0x0F; }
  else if ((c & 0xF8) == 0xF0) { n = 3; cp = c & 0x07; }
  else { i += 1; return 0xFFFD; }
  if (i + n >= s.size() + 0 && i + (size_t)n >= s.size()) { i += 1; return 0xFFFD; }
  for (int k = 1; k <= n; k++) {
    unsigned char cc = (unsigned char)s[i + k];
    if ((cc & 0xC0) != 0x80) { i += 1; return 0xFFFD; }
    cp = (cp << 6) | (cc & 0x3F);
  }
  i += n + 1;
  return cp;
}

uint32_t utf8_prev(const std::string& s, size_t& i) {
  if (i == 0) return 0;
  size_t j = i - 1;
  while (j > 0 && ((unsigned char)s[j] & 0xC0) == 0x80) j--;
  size_t k = j;
  uint32_t cp = utf8_next(s, k);
  i = j;
  return cp;
}

std::string utf8_encode(uint32_t cp) {
  std::string out;
  if (cp < 0x80) out += (char)cp;
  else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
  else if (cp < 0x10000) {
    out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F));
    out += (char)(0x80 | (cp & 0x3F));
  } else {
    out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F));
    out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F));
  }
  return out;
}

size_t utf8_count(const std::string& s) {
  size_t i = 0, n = 0;
  while (i < s.size()) { utf8_next(s, i); n++; }
  return n;
}

// --------------------------------------------------------------- width -----
namespace {
struct Range { uint32_t lo, hi; };
// Zero-width: combining marks, format chars, variation selectors...
const Range kZero[] = {
  {0x0300,0x036F},{0x0483,0x0489},{0x0591,0x05BD},{0x05BF,0x05BF},{0x05C1,0x05C2},
  {0x05C4,0x05C5},{0x05C7,0x05C7},{0x0610,0x061A},{0x064B,0x065F},{0x0670,0x0670},
  {0x06D6,0x06DC},{0x06DF,0x06E4},{0x06E7,0x06E8},{0x06EA,0x06ED},{0x0711,0x0711},
  {0x0730,0x074A},{0x07A6,0x07B0},{0x07EB,0x07F3},{0x0816,0x0819},{0x081B,0x0823},
  {0x0825,0x0827},{0x0829,0x082D},{0x0859,0x085B},{0x08E3,0x0903},{0x093A,0x093C},
  {0x093E,0x094F},{0x0951,0x0957},{0x0962,0x0963},{0x0981,0x0983},{0x09BC,0x09BC},
  {0x09BE,0x09C4},{0x09C7,0x09C8},{0x09CB,0x09CD},{0x09D7,0x09D7},{0x09E2,0x09E3},
  {0x0A01,0x0A03},{0x0A3C,0x0A3C},{0x0A3E,0x0A42},{0x0A47,0x0A48},{0x0A4B,0x0A4D},
  {0x0A51,0x0A51},{0x0A70,0x0A71},{0x0A75,0x0A75},{0x0A81,0x0A83},{0x0ABC,0x0ABC},
  {0x0ABE,0x0AC5},{0x0AC7,0x0AC9},{0x0ACB,0x0ACD},{0x0AE2,0x0AE3},{0x0B01,0x0B03},
  {0x0B3C,0x0B3C},{0x0B3E,0x0B44},{0x0B47,0x0B48},{0x0B4B,0x0B4D},{0x0B56,0x0B57},
  {0x0B62,0x0B63},{0x0B82,0x0B82},{0x0BBE,0x0BC2},{0x0BC6,0x0BC8},{0x0BCA,0x0BCD},
  {0x0BD7,0x0BD7},{0x0C00,0x0C03},{0x0C3E,0x0C44},{0x0C46,0x0C48},{0x0C4A,0x0C4D},
  {0x0C55,0x0C56},{0x0C62,0x0C63},{0x0C81,0x0C83},{0x0CBC,0x0CBC},{0x0CBE,0x0CC4},
  {0x0CC6,0x0CC8},{0x0CCA,0x0CCD},{0x0CD5,0x0CD6},{0x0CE2,0x0CE3},{0x0D01,0x0D03},
  {0x0D3E,0x0D44},{0x0D46,0x0D48},{0x0D4A,0x0D4D},{0x0D57,0x0D57},{0x0D62,0x0D63},
  {0x0D82,0x0D83},{0x0DCA,0x0DCA},{0x0DCF,0x0DD4},{0x0DD6,0x0DD6},{0x0DD8,0x0DDF},
  {0x0DF2,0x0DF3},{0x0E31,0x0E31},{0x0E34,0x0E3A},{0x0E47,0x0E4E},{0x0EB1,0x0EB1},
  {0x0EB4,0x0EB9},{0x0EBB,0x0EBC},{0x0EC8,0x0ECD},{0x0F18,0x0F19},{0x0F35,0x0F35},
  {0x0F37,0x0F37},{0x0F39,0x0F39},{0x0F3E,0x0F3F},{0x0F71,0x0F84},{0x0F86,0x0F87},
  {0x0F8D,0x0F97},{0x0F99,0x0FBC},{0x0FC6,0x0FC6},{0x102B,0x103E},{0x1056,0x1059},
  {0x105E,0x1060},{0x1062,0x1064},{0x1067,0x106D},{0x1071,0x1074},{0x1082,0x108D},
  {0x108F,0x108F},{0x109A,0x109D},{0x135D,0x135F},{0x1712,0x1714},{0x1732,0x1734},
  {0x1752,0x1753},{0x1772,0x1773},{0x17B4,0x17D3},{0x17DD,0x17DD},{0x180B,0x180D},
  {0x18A9,0x18A9},{0x1920,0x192B},{0x1930,0x193B},{0x1A17,0x1A1B},{0x1A55,0x1A5E},
  {0x1A60,0x1A7C},{0x1A7F,0x1A7F},{0x1AB0,0x1ABE},{0x1B00,0x1B04},{0x1B34,0x1B44},
  {0x1B6B,0x1B73},{0x1B80,0x1B82},{0x1BA1,0x1BAD},{0x1BE6,0x1BF3},{0x1C24,0x1C37},
  {0x1CD0,0x1CD2},{0x1CD4,0x1CE8},{0x1CED,0x1CED},{0x1CF2,0x1CF4},{0x1CF8,0x1CF9},
  {0x1DC0,0x1DF5},{0x1DFC,0x1DFF},{0x20D0,0x20F0},{0x2CEF,0x2CF1},{0x2D7F,0x2D7F},
  {0x2DE0,0x2DFF},{0x302A,0x302F},{0x3099,0x309A},{0xA66F,0xA672},{0xA674,0xA67D},
  {0xA69E,0xA69F},{0xA6F0,0xA6F1},{0xA802,0xA802},{0xA806,0xA806},{0xA80B,0xA80B},
  {0xA823,0xA827},{0xA880,0xA881},{0xA8B4,0xA8C4},{0xA8E0,0xA8F1},{0xA926,0xA92D},
  {0xA947,0xA953},{0xA980,0xA983},{0xA9B3,0xA9C0},{0xA9E5,0xA9E5},{0xAA29,0xAA36},
  {0xAA43,0xAA43},{0xAA4C,0xAA4D},{0xAA7B,0xAA7D},{0xAAB0,0xAAB0},{0xAAB2,0xAAB4},
  {0xAAB7,0xAAB8},{0xAABE,0xAABF},{0xAAC1,0xAAC1},{0xAAEB,0xAAEF},{0xAAF5,0xAAF6},
  {0xABE3,0xABEA},{0xABEC,0xABED},{0xFB1E,0xFB1E},{0xFE00,0xFE0F},{0xFE20,0xFE2F},
  {0x101FD,0x101FD},{0x102E0,0x102E0},{0x10376,0x1037A},{0x10A01,0x10A0F},
  {0x11000,0x11002},{0x11038,0x11046},{0x1107F,0x11082},{0x110B0,0x110BA},
  {0x11100,0x11102},{0x11127,0x11134},{0x11173,0x11173},{0x11180,0x11182},
  {0x111B3,0x111C0},{0x1122C,0x11237},{0x112DF,0x112EA},{0x11301,0x11303},
  {0x1133C,0x1133C},{0x1133E,0x11344},{0x11347,0x11348},{0x1134B,0x1134D},
  {0x11357,0x11357},{0x11362,0x11363},{0x114B0,0x114C3},{0x115AF,0x115B5},
  {0x115B8,0x115C0},{0x11630,0x11640},{0x116AB,0x116B7},{0x1171D,0x1172B},
  {0x16AF0,0x16AF4},{0x16B30,0x16B36},{0x16F51,0x16F7E},{0x16F8F,0x16F92},
  {0x1BC9D,0x1BC9E},{0x1D165,0x1D169},{0x1D16D,0x1D172},{0x1D17B,0x1D182},
  {0x1D185,0x1D18B},{0x1D1AA,0x1D1AD},{0x1D242,0x1D244},{0x1DA00,0x1DA36},
  {0x1DA3B,0x1DA6C},{0x1DA75,0x1DA75},{0x1DA84,0x1DA84},{0x1DA9B,0x1DA9F},
  {0x1DAA1,0x1DAAF},{0x1E000,0x1E006},{0x1E008,0x1E018},{0x1E01B,0x1E021},
  {0x1E023,0x1E024},{0x1E026,0x1E02A},{0x1E8D0,0x1E8D6},{0x1E944,0x1E94A},
  {0xE0100,0xE01EF},{0x200B,0x200F},{0x202A,0x202E},{0x2060,0x2064},
  {0xFEFF,0xFEFF},{0xE0001,0xE0001},{0xE0020,0xE007F},{0x00AD,0x00AD},
};
// Wide: East Asian Wide/Fullwidth.
const Range kWide[] = {
  {0x1100,0x115F},{0x231A,0x231B},{0x2329,0x232A},{0x23E9,0x23EC},{0x23F0,0x23F0},
  {0x23F3,0x23F3},{0x25FD,0x25FE},{0x2614,0x2615},{0x2648,0x2653},{0x267F,0x267F},
  {0x2693,0x2693},{0x26A1,0x26A1},{0x26AA,0x26AB},{0x26BD,0x26BE},{0x26C4,0x26C5},
  {0x26CE,0x26CE},{0x26D4,0x26D4},{0x26EA,0x26EA},{0x26F2,0x26F3},{0x26F5,0x26F5},
  {0x26FA,0x26FA},{0x26FD,0x26FD},{0x2705,0x2705},{0x270A,0x270B},{0x2728,0x2728},
  {0x274C,0x274C},{0x274E,0x274E},{0x2753,0x2755},{0x2757,0x2757},{0x2795,0x2797},
  {0x27B0,0x27B0},{0x27BF,0x27BF},{0x2B1B,0x2B1C},{0x2B50,0x2B50},{0x2B55,0x2B55},
  {0x2E80,0x303E},{0x3041,0x33FF},{0x3400,0x4DBF},{0x4DC0,0x9FFF},
  {0xA000,0xA4CF},{0xA960,0xA97F},{0xAC00,0xD7A3},{0xF900,0xFAFF},
  {0xFE10,0xFE19},{0xFE30,0xFE52},{0xFE54,0xFE66},{0xFE68,0xFE6B},
  {0xFF00,0xFF60},{0xFFE0,0xFFE6},{0x16FE0,0x16FE4},{0x17000,0x187F7},
  {0x18800,0x18CD5},{0x1B000,0x1B2FB},{0x1F004,0x1F004},{0x1F0CF,0x1F0CF},
  {0x1F18E,0x1F18E},{0x1F191,0x1F19A},{0x1F200,0x1F202},{0x1F210,0x1F23B},
  {0x1F240,0x1F248},{0x1F250,0x1F251},{0x1F260,0x1F265},{0x1F300,0x1F320},
  {0x1F32D,0x1F335},{0x1F337,0x1F37C},{0x1F37E,0x1F393},{0x1F3A0,0x1F3CA},
  {0x1F3CF,0x1F3D3},{0x1F3E0,0x1F3F0},{0x1F3F4,0x1F3F4},{0x1F3F8,0x1F43E},
  {0x1F440,0x1F440},{0x1F442,0x1F4FC},{0x1F4FF,0x1F53D},{0x1F54B,0x1F54E},
  {0x1F550,0x1F567},{0x1F57A,0x1F57A},{0x1F595,0x1F596},{0x1F5A4,0x1F5A4},
  {0x1F5FB,0x1F64F},{0x1F680,0x1F6C5},{0x1F6CC,0x1F6CC},{0x1F6D0,0x1F6D2},
  {0x1F6D5,0x1F6D7},{0x1F6EB,0x1F6EC},{0x1F6F4,0x1F6FC},{0x1F7E0,0x1F7EB},
  {0x1F90C,0x1F93A},{0x1F93C,0x1F945},{0x1F947,0x1F978},{0x1F97A,0x1F9CB},
  {0x1F9CD,0x1F9FF},{0x1FA70,0x1FA74},{0x1FA78,0x1FA7A},{0x1FA80,0x1FA86},
  {0x1FA90,0x1FAA8},{0x1FAB0,0x1FAB6},{0x1FAC0,0x1FAC2},{0x1FAD0,0x1FAD6},
  {0x20000,0x2FFFD},{0x30000,0x3FFFD},
};
bool in_ranges(const Range* r, size_t n, uint32_t cp) {
  size_t lo = 0, hi = n;
  while (lo < hi) { size_t mid = (lo + hi) / 2; if (cp < r[mid].lo) hi = mid; else if (cp > r[mid].hi) lo = mid + 1; else return true; }
  return false;
}
}  // namespace

int cp_width(uint32_t cp) {
  if (cp == 0) return 0;
  if (cp < 32 || (cp >= 0x7F && cp < 0xA0)) return 0;  // control
  if (in_ranges(kZero, sizeof(kZero) / sizeof(kZero[0]), cp)) return 0;
  if (cp < 0x1100) return 1;
  if (in_ranges(kWide, sizeof(kWide) / sizeof(kWide[0]), cp)) return 2;
  return 1;
}

int str_width(const std::string& s) {
  int w = 0; size_t i = 0;
  while (i < s.size()) w += cp_width(utf8_next(s, i));
  return w;
}

int str_cp_len(const std::string& s) { return (int)utf8_count(s); }

size_t byte_off_for_cells(const std::string& s, size_t from, int cells) {
  size_t i = from; int w = 0;
  while (i < s.size() && w < cells) w += cp_width(utf8_next(s, i));
  return i;
}

std::string substr_cells(const std::string& s, int start, int cells) {
  size_t i = 0; int w = 0;
  while (i < s.size() && w < start) w += cp_width(utf8_next(s, i));
  size_t b = i; int taken = 0;
  while (i < s.size() && taken < cells) taken += cp_width(utf8_next(s, i));
  return s.substr(b, i - b);
}

// ------------------------------------------------------------- strings -----
bool starts_with(const std::string& s, const std::string& p) {
  return s.size() >= p.size() && memcmp(s.data(), p.data(), p.size()) == 0;
}
bool ends_with(const std::string& s, const std::string& p) {
  return s.size() >= p.size() && memcmp(s.data() + s.size() - p.size(), p.data(), p.size()) == 0;
}
static bool is_space_c(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; }
std::string rtrim(const std::string& s) { size_t e = s.size(); while (e > 0 && is_space_c(s[e - 1])) e--; return s.substr(0, e); }
std::string ltrim(const std::string& s) { size_t b = 0; while (b < s.size() && is_space_c(s[b])) b++; return s.substr(b); }
std::string trim(const std::string& s) { return rtrim(ltrim(s)); }
std::string to_lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }
std::string replace_all(std::string s, const std::string& from, const std::string& to) {
  if (from.empty()) return s;
  size_t pos = 0;
  while ((pos = s.find(from, pos)) != std::string::npos) { s.replace(pos, from.size(), to); pos += to.size(); }
  return s;
}
std::vector<std::string> split(const std::string& s, char sep) {
  std::vector<std::string> out; std::string cur;
  for (char c : s) { if (c == sep) { out.push_back(cur); cur.clear(); } else cur += c; }
  out.push_back(cur); return out;
}
std::vector<std::string> split_lines(const std::string& s) {
  std::vector<std::string> out; std::string cur;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '\n') {
      std::string t = rtrim(cur);
      // CommonMark hard break: two trailing spaces.  They must survive the
      // rtrim, so a sentinel stands in for them until inline parsing.
      if (t.size() < cur.size() && cur.size() - t.size() >= 2 &&
          cur[cur.size() - 1] == ' ')
        t += '\x01';
      out.push_back(t);
      cur.clear();
    }
    else if (s[i] != '\r') cur += s[i];
  }
  out.push_back(cur);
  return out;
}
std::string join(const std::vector<std::string>& v, const std::string& sep) {
  std::string o; for (size_t i = 0; i < v.size(); i++) { if (i) o += sep; o += v[i]; } return o;
}
std::string fmt(const char* f, ...) {
  va_list ap; va_start(ap, f);
  char stack[1024];
  int n = vsnprintf(stack, sizeof(stack), f, ap);
  va_end(ap);
  if (n < 0) return "";
  if ((size_t)n < sizeof(stack)) return std::string(stack, n);
  std::string big(n + 1, '\0');
  va_start(ap, f);
  vsnprintf(&big[0], big.size(), f, ap);
  va_end(ap);
  big.resize(n);
  return big;
}
std::string collapse_ws(const std::string& s) {
  std::string o; bool sp = false;
  for (unsigned char c : s) {
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { sp = !o.empty(); }
    else { if (sp) { o += ' '; sp = false; } o += (char)c; }
  }
  return o;
}

// -------------------------------------------------------------- base64 -----
std::string base64_encode(const uint8_t* data, size_t len) {
  static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out; out.reserve((len + 2) / 3 * 4);
  size_t i = 0;
  for (; i + 2 < len; i += 3) {
    uint32_t v = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
    out += T[(v >> 18) & 63]; out += T[(v >> 12) & 63]; out += T[(v >> 6) & 63]; out += T[v & 63];
  }
  if (i + 1 == len) {
    uint32_t v = data[i] << 16;
    out += T[(v >> 18) & 63]; out += T[(v >> 12) & 63]; out += '='; out += '=';
  } else if (i + 2 == len) {
    uint32_t v = (data[i] << 16) | (data[i + 1] << 8);
    out += T[(v >> 18) & 63]; out += T[(v >> 12) & 63]; out += T[(v >> 6) & 63]; out += '=';
  }
  return out;
}

// ---------------------------------------------------------------- misc -----
static struct { const char* name; RGB c; } kNamed[] = {
  {"black",{0,0,0}},{"red",{205,49,49}},{"green",{13,188,121}},{"yellow",{229,229,16}},
  {"blue",{36,114,200}},{"magenta",{188,63,188}},{"cyan",{17,168,205}},{"white",{229,229,229}},
  {"gray",{102,102,102}},{"grey",{102,102,102}},{"orange",{255,165,0}},{"purple",{160,32,240}},
  {"pink",{255,192,203}},{"brown",{165,42,42}},{"navy",{0,0,128}},{"teal",{0,128,128}},
  {"silver",{192,192,192}},{"lime",{0,255,0}},{"olive",{128,128,0}},{"maroon",{128,0,0}},
  {"gold",{255,215,0}},{"skyblue",{135,206,235}},{"crimson",{220,20,60}},{"indigo",{75,0,130}},
  {"transparent",{0,0,0}},
};
RGB parse_color(const std::string& in, RGB fallback) {
  std::string s = to_lower(trim(in));
  if (s.empty()) return fallback;
  if (s[0] == '#') {
    s = s.substr(1);
    if (s.size() == 3) return RGB{(uint8_t)(strtol(std::string(1, s[0]).c_str(), 0, 16) * 17),
                                 (uint8_t)(strtol(std::string(1, s[1]).c_str(), 0, 16) * 17),
                                 (uint8_t)(strtol(std::string(1, s[2]).c_str(), 0, 16) * 17)};
    if (s.size() >= 6) {
      long v = strtol(s.substr(0, 6).c_str(), nullptr, 16);
      return RGB{(uint8_t)((v >> 16) & 255), (uint8_t)((v >> 8) & 255), (uint8_t)(v & 255)};
    }
    return fallback;
  }
  if (s.size() == 6 && s.find_first_not_of("0123456789abcdef") == std::string::npos) {
    long v = strtol(s.c_str(), nullptr, 16);
    return RGB{(uint8_t)((v >> 16) & 255), (uint8_t)((v >> 8) & 255), (uint8_t)(v & 255)};
  }
  if (starts_with(s, "rgb(")) {
    auto parts = split(s.substr(4, s.find(')') - 4), ',');
    if (parts.size() == 3) return RGB{(uint8_t)atoi(parts[0].c_str()), (uint8_t)atoi(parts[1].c_str()), (uint8_t)atoi(parts[2].c_str())};
  }
  for (auto& n : kNamed) if (s == n.name) return n.c;
  return fallback;
}
std::string abs_path(const std::string& path) {
#ifdef _WIN32
  char buf[_MAX_PATH];
  if (_fullpath(buf, path.c_str(), _MAX_PATH)) return std::string(buf);
  return path;
#else
  char buf[PATH_MAX];
  if (realpath(path.c_str(), buf)) return std::string(buf);
  return path;
#endif
}
std::string dir_name(const std::string& path) {
  size_t p = path.find_last_of("/\\");
  if (p == std::string::npos) return ".";
  if (p == 0) return "/";
  return path.substr(0, p);
}
bool file_exists(const std::string& path) { return plat::regular_file_exists(path); }
bool read_file(const std::string& path, std::string& out) {
  FILE* f = plat::open_file(path, "rb");  // Windows: the path is UTF-8
  if (!f) return false;
  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  out.resize((size_t)std::max(0L, n));
  size_t got = n > 0 ? fread(&out[0], 1, (size_t)n, f) : 0;
  fclose(f);
  out.resize(got);
  return true;
}
double now_ms() {
  using namespace std::chrono;
  return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

}  // namespace mdt
