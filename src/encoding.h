// encoding.h : 8-bit ("ANSI") sources -> UTF-8.
//
// A Markdown file saved by a Chinese Windows editor is usually GBK, not UTF-8:
// every non-ASCII character then arrives as two bytes that are not a valid
// UTF-8 sequence, and the reader showed mojibake ("���ı���").  mdt detects that
// and converts the text before parsing; --encoding=... forces a choice.
#pragma once
#include <string>

namespace mdt {

struct DecodedText {
  std::string text;      // UTF-8 (or the input, unchanged)
  std::string encoding;  // "utf-8", "gb18030", "big5", "cp1252", "latin1", ...
  bool converted = false;
  std::string note;      // why nothing was converted (empty when all is well)
};

// want: "auto" | "utf-8" | "gbk" | "gb18030" | "big5" | "cp1252" | "latin1"
//       | "off" ("off" and "utf-8" never convert, they only report).
DecodedText decode_text(std::string raw, const std::string& want);

// True when the whole buffer is valid UTF-8 (a truncated sequence counts as
// invalid; "bad" receives the number of broken characters found, up to 200000
// bytes in, which is where a normal document has told us all it needs to).
bool is_valid_utf8(const std::string& s, int* bad = nullptr);

}  // namespace mdt
