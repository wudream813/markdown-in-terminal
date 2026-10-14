#include "encoding.h"
#include "i18n.h"

#include <cstring>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <iconv.h>
#include <errno.h>
#endif

namespace mdt {

namespace {

bool utf8_ok(const std::string& s, int* bad) {
  int broken = 0;
  for (size_t i = 0; i < s.size(); i++) {
    unsigned char c = (unsigned char)s[i];
    if (c < 0x80) continue;
    size_t n = 0;
    if ((c & 0xE0) == 0xC0) n = 2;
    else if ((c & 0xF0) == 0xE0) n = 3;
    else if ((c & 0xF8) == 0xF0) n = 4;
    else { broken++; continue; }              // 0x80..0xBF or 0xF8..: not a lead byte
    if (n == 2 && c < 0xC2) { broken++; continue; }         // overlong
    if (n == 3 && (unsigned char)s[i + 1] == 0x80) { /* fine, checked below */ }
    if (i + n > s.size()) { broken++; break; }              // truncated
    bool ok = true;
    for (size_t k = 1; k < n; k++)
      if (((unsigned char)s[i + k] & 0xC0) != 0x80) ok = false;
    if (!ok) { broken++; continue; }
    if (n == 3) {  // reject surrogates and overlongs
      unsigned cp = ((c & 0x0F) << 12) | (((unsigned char)s[i + 1] & 0x3F) << 6) |
                    ((unsigned char)s[i + 2] & 0x3F);
      if (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF)) { broken++; continue; }
    }
    i += n - 1;
  }
  if (bad) *bad = broken;
  return broken == 0;
}

void utf8_append(std::string& out, unsigned cp) {
  if (cp < 0x80) out += (char)cp;
  else if (cp < 0x800) {
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

// Windows-1252 differs from Latin-1 only in 0x80..0x9F, where the typographic
// punctuation lives.  0x81/0x8D/0x8F/0x90/0x9D are unassigned; they are kept
// as their C1 code point so nothing is lost.
const unsigned short kCp1252[32] = {
  0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
  0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
  0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
  0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178,
};

std::string single_byte_to_utf8(const std::string& in, bool cp1252) {
  std::string out;
  out.reserve(in.size());
  for (unsigned char c : in) {
    unsigned cp = c;
    if (c >= 0x80 && c <= 0x9F && cp1252) cp = kCp1252[c - 0x80];
    utf8_append(out, cp);
  }
  return out;
}

#if defined(_WIN32)
// Windows has the code pages built in, and the "ANSI" code page of a Chinese
// system is exactly what these files were saved with (CP936 for GBK).
bool convert_cp(const std::string& in, unsigned cp, bool strict, std::string* out) {
  if (in.empty()) { out->clear(); return true; }
  DWORD flags = (strict && cp != 54936) ? MB_ERR_INVALID_CHARS : 0;
  int wn = MultiByteToWideChar(cp, flags, in.data(), (int)in.size(), nullptr, 0);
  if (wn <= 0) return false;
  std::wstring wide((size_t)wn, L'\0');
  if (MultiByteToWideChar(cp, flags, in.data(), (int)in.size(), &wide[0], wn) <= 0) return false;
  int un = WideCharToMultiByte(CP_UTF8, 0, wide.data(), wn, nullptr, 0, nullptr, nullptr);
  if (un <= 0) return false;
  std::string utf8((size_t)un, '\0');
  if (WideCharToMultiByte(CP_UTF8, 0, wide.data(), wn, &utf8[0], un, nullptr, nullptr) <= 0)
    return false;
  *out = std::move(utf8);
  return true;
}

bool convert_named(const std::string& in, const std::string& enc, bool strict, std::string* out) {
  if (enc == "gbk") return convert_cp(in, 936, strict, out);
  if (enc == "gb18030") return convert_cp(in, 54936, strict, out);
  if (enc == "big5") return convert_cp(in, 950, strict, out);
  if (enc == "cp1252" || enc == "latin1") { *out = single_byte_to_utf8(in, enc == "cp1252"); return true; }
  return false;
}
#else
bool convert_named(const std::string& in, const std::string& enc, bool /*strict*/, std::string* out) {
  if (enc == "cp1252" || enc == "latin1") {
    *out = single_byte_to_utf8(in, enc == "cp1252");
    return true;
  }
  const char* from = enc == "gbk" ? "GBK"
                   : enc == "gb18030" ? "GB18030"
                   : enc == "big5" ? "BIG5"
                   : nullptr;
  if (!from) return false;
  iconv_t cd = iconv_open("UTF-8", from);
  if (cd == (iconv_t)-1) return false;   // no table for it in this libc
  std::string res;
  res.reserve(in.size());
  char buf[4096];
  char* inbuf = const_cast<char*>(in.data());
  size_t inleft = in.size();
  while (inleft > 0) {
    char* outbuf = buf;
    size_t outleft = sizeof(buf);
    size_t r = iconv(cd, &inbuf, &inleft, &outbuf, &outleft);
    res.append(buf, sizeof(buf) - outleft);
    if (r == (size_t)-1) {
      if (errno == E2BIG) continue;
      iconv_close(cd);
      return false;                      // EILSEQ/EINVAL: not this encoding
    }
  }
  iconv_close(cd);
  *out = std::move(res);
  return true;
}
#endif

}  // namespace

bool is_valid_utf8(const std::string& s, int* bad) { return utf8_ok(s, bad); }

DecodedText decode_text(const std::string& raw, const std::string& want) {
  DecodedText r;
  r.text = raw;
  r.encoding = "utf-8";
  if (raw.empty() || want == "off") return r;
  // A BOM is authoritative.
  if (raw.size() >= 3 && (unsigned char)raw[0] == 0xEF && (unsigned char)raw[1] == 0xBB &&
      (unsigned char)raw[2] == 0xBF) {
    return r;
  }
  if (raw.size() >= 2 && (((unsigned char)raw[0] == 0xFF && (unsigned char)raw[1] == 0xFE) ||
                          ((unsigned char)raw[0] == 0xFE && (unsigned char)raw[1] == 0xFF))) {
    r.note = tr("utf-16: not supported, save the file as UTF-8",
                "utf-16：不支持，请把文件另存为 UTF-8");
    return r;
  }
  bool valid = utf8_ok(raw, nullptr);
  if (want == "utf-8") {
    if (!valid)
      r.note = tr("not valid utf-8 (use --encoding=gbk, big5 or cp1252)",
                  "不是合法的 UTF-8（可用 --encoding=gbk、big5 或 cp1252）");
    return r;
  }
  if (valid) return r;   // nothing to do: it already is UTF-8

  // Auto-detection has to make one guess: "GBK first" is the right one for a
  // Chinese Windows file, and GB18030's byte space is a superset of Big5's, so
  // a Big5 document decodes "successfully" as mojibake instead of failing.
  // --encoding=big5 (or cp1252, latin1) is the way out of that corner.
  std::vector<std::string> candidates;
  if (want == "auto") {
    candidates.push_back("gb18030");
    candidates.push_back("big5");
  } else {
    candidates.push_back(want);
  }
  for (const std::string& enc : candidates) {
    std::string utf8;
    if (!convert_named(raw, enc, true, &utf8)) continue;
    if (!utf8_ok(utf8, nullptr)) continue;      // a converter that lied
    r.text = std::move(utf8);
    r.encoding = enc == "gbk" ? "gbk" : enc;
    r.converted = true;
    return r;
  }
  r.note = tr("not valid utf-8 and no conversion worked (try --encoding=gbk, big5 or cp1252)",
              "不是合法的 UTF-8，且自动转码没有成功（试试 --encoding=gbk、big5 或 cp1252）");
  return r;
}

}  // namespace mdt
