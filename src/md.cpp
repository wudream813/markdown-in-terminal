// md.cpp : Markdown parser implementation.
#include "md.h"

#include <algorithm>
#include <cstring>

namespace mdt {

namespace {

bool is_blank(const std::string& s) { return trim(s).empty(); }

size_t indent_of(const std::string& s) {
  size_t i = 0;
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
  return i;
}

std::string decode_entities(const std::string& s) {
  static const struct { const char* ent; const char* rep; } kEnt[] = {
    {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&apos;", "'"},
    {"&nbsp;", "\u00a0"}, {"&mdash;", "\u2014"}, {"&ndash;", "\u2013"}, {"&hellip;", "\u2026"},
    {"&times;", "\u00d7"}, {"&divide;", "\u00f7"}, {"&plusmn;", "\u00b1"}, {"&le;", "\u2264"},
    {"&ge;", "\u2265"}, {"&ne;", "\u2260"}, {"&rarr;", "\u2192"}, {"&larr;", "\u2190"},
    {"&infin;", "\u221e"}, {"&sum;", "\u2211"}, {"&int;", "\u222b"}, {"&pi;", "\u03c0"},
    {"&#39;", "'"}, {"&copy;", "\u00a9"}, {"&reg;", "\u00ae"}, {"&trade;", "\u2122"},
  };
  std::string out;
  size_t i = 0;
  while (i < s.size()) {
    if (s[i] == '&') {
      size_t semi = s.find(';', i);
      if (semi != std::string::npos && semi - i <= 12) {
        std::string ent = s.substr(i, semi - i + 1);
        bool done = false;
        for (auto& e : kEnt) {
          if (ent == e.ent) { out += e.rep; i = semi + 1; done = true; break; }
        }
        if (done) continue;
        if (ent.size() > 3 && ent[1] == '#') {  // numeric
          long v = 0;
          bool hex = ent[2] == 'x' || ent[2] == 'X';
          v = strtol(ent.substr(hex ? 3 : 2, ent.size() - (hex ? 4 : 3)).c_str(), nullptr, hex ? 16 : 10);
          if (v > 0 && v < 0x110000) { out += utf8_encode((uint32_t)v); i = semi + 1; continue; }
        }
      }
    }
    out += s[i++];
  }
  return out;
}

}  // namespace

std::string spans_plain_text(const std::vector<Span>& spans) {
  std::string out;
  for (auto& s : spans) {
    if (s.kind == Span::Math) out += (s.display_math ? " " : "") + s.tex;
    else out += s.text;
  }
  return out;
}

// ======================================================= inline parser ======
namespace {

struct InlineParser {
  const std::string& s;
  size_t i = 0;
  size_t limit = (size_t)-1;  // never parse beyond this offset
  bool allow_math;
  int line = 0;
  char stop_ch = 0;   // stop when this character is reached (link labels)
  std::vector<Span>* out;

  InlineParser(const std::string& str, bool math, int ln, std::vector<Span>* o)
      : s(str), allow_math(math), line(ln), out(o) {}

  void text(const std::string& t, const Span& proto) {
    if (t.empty()) return;
    if (!out->empty() && out->back().kind == Span::Text && !out->back().is_link &&
        out->back().bold == proto.bold && out->back().italic == proto.italic &&
        out->back().strike == proto.strike && out->back().underline == proto.underline &&
        out->back().has_color == proto.has_color &&
        (!proto.has_color || out->back().color == proto.color)) {
      out->back().text += t;
      return;
    }
    Span sp = proto;
    sp.kind = Span::Text;
    sp.text = t;
    out->push_back(sp);
  }

  // find matching closing run of `ch` of the same length, respecting escapes
  size_t find_closing(const std::string& open, size_t from) {
    size_t p = from;
    while (p < s.size()) {
      size_t f = s.find(open, p);
      if (f == std::string::npos) return std::string::npos;
      // count preceding backslashes
      size_t b = f;
      while (b > 0 && s[b - 1] == '\\') b--;
      if ((f - b) % 2 == 0) return f;
      p = f + open.size();
    }
    return std::string::npos;
  }

  static bool is_intraword_underscore(const std::string& s, size_t pos) {
    if (pos == 0 || pos + 1 >= s.size()) return false;
    unsigned char a = (unsigned char)s[pos - 1], b = (unsigned char)s[pos + 1];
    return isalnum(a) && isalnum(b);
  }

  void run() {
    Span proto;  // styles accumulate here; see parse_runs()
    parse_runs(proto, 0);
  }

  // Parses [i, end) with `style` applied, leaving i at `end` (or beyond).
  void parse_region(size_t end, const Span& style, int depth) {
    size_t saved = limit;
    limit = std::min(limit, end);
    parse_runs(style, depth + 1);
    limit = saved;
    if (i < end) i = end;
  }

  void parse_runs(const Span& base, int depth) {
    if (depth > 12) {
      text(s.substr(i), base);
      i = s.size();
      return;
    }
    std::string buf;
    auto flush = [&]() {
      if (!buf.empty()) {
        text(decode_entities(buf), base);
        buf.clear();
      }
    };
    while (i < s.size() && i < limit) {
      char c = s[i];
      if (stop_ch && c == stop_ch) break;
      // ---------------------------------------------------------- escapes --
      if (c == '\\' && i + 1 < s.size() && strchr("\\`*_{}[]()#+-.!~>|$&#^", s[i + 1])) {
        buf += s[i + 1];
        i += 2;
        continue;
      }
      if (c == '\\' && i + 2 < s.size() && s[i + 1] == '\\') {
        flush();
        Span br = base;
        br.kind = Span::LineBreak;
        out->push_back(br);
        i += 2;
        continue;
      }
      if (c == '\\' && i + 1 < s.size() && (s[i + 1] == '(' || s[i + 1] == '[') && allow_math) {
        // \( ... \) and \[ ... \]
        char open = s[i + 1];
        std::string close = open == '(' ? "\\)" : "\\]";
        size_t e = s.find(close, i + 2);
        if (e != std::string::npos) {
          std::string tex = s.substr(i + 2, e - i - 2);
          flush();
          Span sp = base;
          sp.kind = Span::Math;
          sp.tex = tex;
          sp.display_math = open == '[';
          out->push_back(sp);
          i = e + close.size();
          continue;
        }
      }
      // -------------------------------------------------- hard line break --
      if (c == '\n') {
        bool hard = buf.size() >= 2 && buf[buf.size() - 1] == ' ' && buf[buf.size() - 2] == ' ';
        flush();
        Span br = base;
        br.kind = Span::LineBreak;
        if (hard) out->push_back(br);
        else {
          // soft break: keep a space so wrapped text stays readable
          text(" ", base);
        }
        i++;
        // skip leading spaces of the next line
        while (i < s.size() && s[i] == ' ') i++;
        continue;
      }
      // --------------------------------------------------------------- code -
      if (c == '`') {
        size_t ticks = 1;
        while (i + ticks < s.size() && s[i + ticks] == '`') ticks++;
        std::string open(ticks, '`');
        size_t e = s.find(open, i + ticks);
        if (e != std::string::npos) {
          std::string code = s.substr(i + ticks, e - i - ticks);
          if (code.find('`') == std::string::npos || ticks > 1) {
            // trim one leading/trailing space like CommonMark
            if (code.size() > 1 && code.front() == ' ' && code.back() == ' ' && trim(code) != "")
              code = code.substr(1, code.size() - 2);
            flush();
            Span sp = base;
            sp.kind = Span::Code;
            sp.text = code;
            out->push_back(sp);
            i = e + ticks;
            continue;
          }
        }
      }
      // --------------------------------------------------------------- math -
      if (c == '$' && allow_math) {
        bool display = (i + 1 < s.size() && s[i + 1] == '$');
        std::string delim = display ? "$$" : "$";
        size_t start = i + delim.size();
        // a lone $ is literal; require non-space after opening delimiter
        if (start < s.size() && !isspace((unsigned char)s[start])) {
          size_t e = s.find(delim, start);
          while (e != std::string::npos && e > start && s[e - 1] == '\\') e = s.find(delim, e + delim.size());
          if (e != std::string::npos && e + delim.size() <= s.size()) {
            std::string tex = s.substr(start, e - start);
            bool ok = !tex.empty() && tex.find('\n') == std::string::npos ? true : display;
            if (display) {
              tex = trim(tex);
              ok = !tex.empty();
            } else {
              ok = !tex.empty() && !isspace((unsigned char)tex[0]) && !isspace((unsigned char)tex.back());
            }
            if (ok) {
              flush();
              Span sp = base;
              sp.kind = Span::Math;
              sp.tex = tex;
              sp.display_math = display;
              out->push_back(sp);
              i = e + delim.size();
              continue;
            }
          }
        }
      }
      // ------------------------------------------------------------- images -
      if (c == '!' && i + 1 < s.size() && s[i + 1] == '[') {
        size_t close = s.find(']', i + 2);
        if (close != std::string::npos && close + 1 < s.size() && s[close + 1] == '(') {
          size_t parend = s.find(')', close + 2);
          if (parend != std::string::npos) {
            std::string alt = s.substr(i + 2, close - i - 2);
            std::string dest = trim(s.substr(close + 2, parend - close - 2));
            std::string title;
            size_t sp2 = dest.find_first_of(" \t");
            if (sp2 != std::string::npos) {
              title = trim(dest.substr(sp2));
              dest = trim(dest.substr(0, sp2));
              if (title.size() >= 2 && (title.front() == '"' || title.front() == '\''))
                title = title.substr(1, title.size() - 2);
            }
            if (dest.size() >= 2 && dest.front() == '<' && dest.back() == '>')
              dest = dest.substr(1, dest.size() - 2);
            flush();
            Span sp = base;
            sp.kind = Span::Image;
            sp.text = decode_entities(alt);
            sp.link = decode_entities(dest);
            sp.is_link = true;
            if (!title.empty()) sp.tex = title;  // reuse tex for the title
            out->push_back(sp);
            i = parend + 1;
            continue;
          }
        }
      }
      // --------------------------------------------------------------- link -
      if (c == '[') {
        size_t close = i + 1;
        int depth = 1;
        while (close < s.size() && depth > 0) {
          if (s[close] == '\\') { close += 2; continue; }
          if (s[close] == '[') depth++;
          else if (s[close] == ']') depth--;
          close++;
        }
        if (depth == 0) {
          size_t b = close - 1;  // index of ']'
          if (b + 1 < s.size() && s[b + 1] == '(') {
            size_t parend = b + 2;
            int pd = 1;
            while (parend < s.size() && pd > 0) {
              if (s[parend] == '\\') { parend += 2; continue; }
              if (s[parend] == '(') pd++;
              else if (s[parend] == ')') pd--;
              parend++;
            }
            if (pd == 0) {
              std::string label = s.substr(i + 1, b - i - 1);
              std::string dest = trim(s.substr(b + 2, parend - b - 3));
              std::string title;
              size_t sp = dest.find_first_of(" \t");
              if (sp != std::string::npos) {
                title = trim(dest.substr(sp));
                dest = trim(dest.substr(0, sp));
                if (title.size() >= 2 && (title.front() == '"' || title.front() == '\''))
                  title = title.substr(1, title.size() - 2);
              }
              if (dest.size() >= 2 && dest.front() == '<' && dest.back() == '>')
                dest = dest.substr(1, dest.size() - 2);
              flush();
              Span linkbase = base;
              linkbase.is_link = true;
              linkbase.link = decode_entities(dest);
              linkbase.underline = true;
              size_t label_start = i + 1;
              i = label_start;
              parse_region(b, linkbase, depth);
              i = parend;
              if (i <= label_start) i = label_start + 1;
              continue;
            }
          }
          // reference link [text][ref]
          if (b + 1 < s.size() && s[b + 1] == '[') {
            size_t e2 = s.find(']', b + 2);
            if (e2 != std::string::npos) {
              std::string label = s.substr(i + 1, b - i - 1);
              std::string ref = s.substr(b + 2, e2 - b - 2);
              if (ref.empty()) ref = label;
              flush();
              Span lk = base;
              lk.is_link = true;
              lk.underline = true;
              lk.link = to_lower(ref);  // resolved by the renderer via definitions
              size_t label_start2 = i + 1;
              i = label_start2;
              parse_region(b, lk, depth);
              i = e2 + 1;
              if (i <= label_start2) i = label_start2 + 1;
              continue;
            }
          }
        }
      }
      // ----------------------------------------------------------- autolink -
      if (c == '<') {
        size_t e = s.find('>', i);
        if (e != std::string::npos && e - i < 300) {
          std::string inner = s.substr(i + 1, e - i - 1);
          if (inner.rfind("http://", 0) == 0 || inner.rfind("https://", 0) == 0) {
            flush();
            Span lk = base;
            lk.is_link = true;
            lk.underline = true;
            lk.link = inner;
            text(inner, lk);
            i = e + 1;
            continue;
          }
          if (inner.find('@') != std::string::npos && inner.find(' ') == std::string::npos &&
              inner.find('<') == std::string::npos) {
            flush();
            Span lk = base;
            lk.is_link = true;
            lk.underline = true;
            lk.link = "mailto:" + inner;
            text(inner, lk);
            i = e + 1;
            continue;
          }
          // inline HTML: strip tags
          bool looks_tag = !inner.empty() && (isalpha((unsigned char)inner[0]) || inner[0] == '/' || inner[0] == '!');
          if (looks_tag) {
            i = e + 1;
            continue;
          }
        }
        buf += c;
        i++;
        continue;
      }
      // --------------------------------------------------------- emphasis --
      auto try_emphasis = [&](char ch) -> bool {
        size_t run = 0;
        while (i + run < s.size() && s[i + run] == ch) run++;
        int use = 0;
        bool bold = false, italic = false;
        if (run >= 3) { use = 3; bold = italic = true; }
        else if (run == 2) { use = 2; bold = true; }
        else { use = 1; italic = true; }
        if (ch == '_' && is_intraword_underscore(s, i)) return false;
        // find closing run
        size_t p = i + run;
        while (p < s.size()) {
          size_t f = s.find(std::string(use, ch), p);
          if (f == std::string::npos) {
            if (use == 3) {  // "***x**" -> bold+italic so try 2
              use = 2; bold = true; italic = false;
              p = i + run;
              continue;
            }
            return false;
          }
          size_t b = f;
          while (b > 0 && s[b - 1] == '\\') b--;
          if ((f - b) % 2 != 0) { p = f + use; continue; }
          if (f == i + run) { p = f + use; continue; }  // empty
          if (ch == '_' && is_intraword_underscore(s, f)) { p = f + use; continue; }
          // found
          flush();
          Span nest = base;
          if (bold) nest.bold = true;
          if (italic) nest.italic = true;
          size_t inner_start = i + use;
          i = inner_start;
          parse_region(f, nest, depth);
          i = f + use;
          if (i <= inner_start) return false;
          return true;
        }
        return false;
      };
      if (c == '*' || c == '_') {
        if (try_emphasis(c)) continue;
      }
      // -------------------------------------------------------- strike ------
      if (c == '~' && i + 1 < s.size() && s[i + 1] == '~') {
        size_t e = s.find("~~", i + 2);
        if (e != std::string::npos && e > i + 2) {
          flush();
          Span nest = base;
          nest.strike = true;
          size_t save = i;
          i += 2;
          parse_region(e, nest, depth);
          i = e + 2;
          if (i <= save) { i = save + 1; }
          continue;
        }
      }
      buf += c;
      i++;
    }
    flush();
  }
};

}  // namespace

std::vector<Span> MarkdownParser::parse_inline(const std::string& text, int line_no) {
  std::vector<Span> out;
  InlineParser p(text, opt_.math, line_no, &out);
  p.run();
  // strip a trailing soft space
  if (!out.empty() && out.back().kind == Span::Text) {
    out.back().text = rtrim(out.back().text);
    if (out.back().text.empty()) out.pop_back();
  }
  return out;
}

// ======================================================= block parser ======
namespace {
bool is_hr_line(const std::string& line) {
  std::string t = trim(line);
  if (t.size() < 3) return false;
  char c = t[0];
  if (c != '-' && c != '*' && c != '_') return false;
  int count = 0;
  for (char x : t) {
    if (x == c) count++;
    else if (x != ' ' && x != '\t') return false;
  }
  return count >= 3;
}
int heading_level(const std::string& line, std::string& text) {
  size_t i = 0;
  while (i < line.size() && line[i] == ' ') i++;
  size_t h = 0;
  while (i + h < line.size() && line[i + h] == '#') h++;
  if (h == 0 || h > 6) return 0;
  if (i + h < line.size() && line[i + h] != ' ' && line[i + h] != '\t') return 0;
  text = trim(line.substr(i + h));
  while (!text.empty() && text.back() == '#') text.pop_back();
  text = trim(text);
  return (int)h;
}
}  // namespace

bool MarkdownParser::is_list_item(const std::string& line, size_t indent, bool* ordered, int* num,
                                  size_t* marker_len) {
  size_t i = indent;
  if (i >= line.size()) return false;
  char c = line[i];
  if (c == '-' || c == '*' || c == '+') {
    if (i + 1 < line.size() && (line[i + 1] == ' ' || line[i + 1] == '\t')) {
      *ordered = false;
      *num = 0;
      *marker_len = 2;
      return true;
    }
    return false;
  }
  if (isdigit((unsigned char)c)) {
    size_t j = i;
    while (j < line.size() && isdigit((unsigned char)line[j])) j++;
    if (j < line.size() && (line[j] == '.' || line[j] == ')') && j + 1 < line.size() &&
        (line[j + 1] == ' ' || line[j + 1] == '\t')) {
      *ordered = true;
      *num = atoi(line.substr(i, j - i).c_str());
      *marker_len = j - i + 2;
      return true;
    }
    return false;
  }
  return false;
}

std::vector<Block> MarkdownParser::parse_blocks(int depth) {
  std::vector<Block> blocks;
  if (depth > 8) return blocks;
  while (li_ < lines_.size()) {
    const std::string& line = lines_[li_];
    if (is_blank(line)) { li_++; continue; }

    Block b;
    b.src_line = (int)li_ + 1;

    // ------------------------------------------------------------- fences --
    {
      std::string t = ltrim(line);
      size_t ind = indent_of(line);
      if (ind <= 3 && (t.rfind("```", 0) == 0 || t.rfind("~~~", 0) == 0)) {
        char fence = t[0];
        size_t flen = 0;
        while (flen < t.size() && t[flen] == fence) flen++;
        std::string info = trim(t.substr(flen));
        b.type = Block::CodeBlock;
        b.lang = info;
        size_t sp = b.lang.find_first_of(" \t{");
        if (sp != std::string::npos) b.lang = b.lang.substr(0, sp);
        li_++;
        while (li_ < lines_.size()) {
          std::string lt = ltrim(lines_[li_]);
          if (lt.size() >= flen && lt.rfind(std::string(flen, fence), 0) == 0) { li_++; break; }
          b.code += lines_[li_];
          b.code += "\n";
          li_++;
        }
        if (!b.code.empty() && b.code.back() == '\n') b.code.pop_back();
        std::string low = to_lower(b.lang);
        if (low == "math" || low == "latex" || low == "tex" || low == "katex") b.code_is_math = true;
        blocks.push_back(b);
        continue;
      }
    }

    // ------------------------------------------------------- math $$ block -
    if (opt_.math) {
      std::string t = trim(line);
      if (t.rfind("$$", 0) == 0) {
        std::string rest = trim(t.substr(2));
        std::string tex;
        if (rest.size() >= 2 && rest.substr(rest.size() - 2) == "$$") {
          tex = trim(rest.substr(0, rest.size() - 2));
          li_++;
        } else {
          tex = rest;
          li_++;
          while (li_ < lines_.size()) {
            std::string l2 = lines_[li_];
            size_t p = l2.find("$$");
            if (p != std::string::npos) {
              tex += "\n" + l2.substr(0, p);
              li_++;
              break;
            }
            tex += "\n" + l2;
            li_++;
          }
        }
        b.type = Block::MathBlock;
        b.code = trim(tex);
        blocks.push_back(b);
        continue;
      }
    }

    // ----------------------------------------------------------- headings --
    {
      std::string text;
      int lv = heading_level(line, text);
      if (lv > 0) {
        b.type = Block::Heading;
        b.level = lv;
        b.spans = parse_inline(text, (int)li_);
        li_++;
        blocks.push_back(b);
        continue;
      }
    }

    // ------------------------------------------------------------ setext ---
    if (indent_of(line) <= 3 && !is_hr_line(line) && li_ + 1 < lines_.size() && !is_blank(line)) {
      std::string nxt = trim(lines_[li_ + 1]);
      bool all_eq = !nxt.empty(), all_dash = !nxt.empty();
      for (char c : nxt) if (c != '=') all_eq = false;
      for (char c : nxt) if (c != '-') all_dash = false;
      if ((all_eq || all_dash) && nxt.size() >= 1) {
        b.type = Block::Heading;
        b.level = all_eq ? 1 : 2;
        b.spans = parse_inline(trim(line), (int)li_);
        li_ += 2;
        blocks.push_back(b);
        continue;
      }
    }

    // ----------------------------------------------------------------- hr ---
    if (is_hr_line(line)) {
      b.type = Block::Hr;
      li_++;
      blocks.push_back(b);
      continue;
    }

    // ---------------------------------------------------------- blockquote -
    if (trim(line).size() > 0 && trim(line)[0] == '>') {
      std::vector<std::string> inner;
      int src_start = (int)li_;
      while (li_ < lines_.size()) {
        std::string l2 = lines_[li_];
        if (is_blank(l2)) {
          // blank line only continues the quote if the next line is a quote line
          size_t nxt = li_ + 1;
          bool cont = false;
          while (nxt < lines_.size() && is_blank(lines_[nxt])) nxt++;
          if (nxt < lines_.size() && trim(lines_[nxt]).size() && trim(lines_[nxt])[0] == '>') cont = true;
          if (!cont) break;
          inner.push_back("");
          li_++;
          continue;
        }
        std::string t = ltrim(l2);
        if (!t.empty() && t[0] == '>') {
          t = t.substr(1);
          if (!t.empty() && t[0] == ' ') t = t.substr(1);
          inner.push_back(t);
          li_++;
          continue;
        }
        break;
      }
      b.type = Block::Quote;
      b.level = depth + 1;
      b.src_line = src_start + 1;
      MarkdownParser sub;
      sub.opt_ = opt_;
      sub.lines_ = inner;
      sub.src_ = src_;
      sub.doc_ = doc_;
      sub.li_ = 0;
      b.items.push_back(sub.parse_blocks(depth + 1));
      blocks.push_back(b);
      continue;
    }

    // --------------------------------------------------------------- list --
    {
      bool ordered = false;
      int num = 0;
      size_t mlen = 0;
      size_t ind = indent_of(line);
      if (ind <= 3 && is_list_item(line, ind, &ordered, &num, &mlen)) {
        b.type = Block::List;
        b.ordered = ordered;
        b.start_num = num;
        b.marker_width = (int)mlen;
        while (li_ < lines_.size()) {
          const std::string& l2 = lines_[li_];
          if (is_blank(l2)) {
            // blank line: list continues if the next non-blank line is indented
            size_t nxt = li_ + 1;
            while (nxt < lines_.size() && is_blank(lines_[nxt])) nxt++;
            if (nxt >= lines_.size()) { li_ = nxt; break; }
            if (indent_of(lines_[nxt]) >= ind + 2) { li_ = nxt; continue; }
            li_ = nxt;
            break;
          }
          bool o2 = false;
          int n2 = 0;
          size_t m2 = 0;
          size_t ind2 = indent_of(l2);
          bool is_item = ind2 <= ind + 1 && is_list_item(l2, ind2, &o2, &n2, &m2) && o2 == ordered;
          if (is_item && ind2 <= ind + 1) {
            // new item
            std::vector<std::string> item_lines;
            std::string first = l2.substr(ind2 + m2);
            item_lines.push_back(first);
            size_t item_start = li_;
            li_++;
            std::string item_indent(ind2 + m2, ' ');
            while (li_ < lines_.size()) {
              const std::string& l3 = lines_[li_];
              if (is_blank(l3)) {
                size_t nxt = li_ + 1;
                while (nxt < lines_.size() && is_blank(lines_[nxt])) nxt++;
                if (nxt < lines_.size()) {
                  size_t ni = indent_of(lines_[nxt]);
                  bool o3 = false; int n3 = 0; size_t m3 = 0;
                  bool newitem = ni <= ind + 1 && is_list_item(lines_[nxt], ni, &o3, &n3, &m3) && o3 == ordered;
                  if (newitem || ni < ind + 2) {
                    li_ = nxt;
                    break;
                  }
                  item_lines.push_back("");
                  li_++;
                  continue;
                }
                li_ = nxt;
                break;
              }
              size_t ni = indent_of(l3);
              bool o3 = false; int n3 = 0; size_t m3 = 0;
              if (ni <= ind + 1 && is_list_item(l3, ni, &o3, &n3, &m3) && o3 == ordered) break;
              if (ni < ind + 2 && ni > 0) break;
              // strip the item indentation
              size_t strip = std::min(l3.size(), std::max(ind + m2, (size_t)1));
              size_t k = 0;
              while (k < strip && k < l3.size() && (l3[k] == ' ' || l3[k] == '\t')) k++;
              item_lines.push_back(l3.substr(k));
              li_++;
            }
            (void)item_start;
            // task list?
            bool task = false, checked = false;
            if (!item_lines.empty()) {
              std::string& f = item_lines[0];
              if (f.size() >= 3 && f[0] == '[' && (f[2] == ']') && (f[1] == ' ' || f[1] == 'x' || f[1] == 'X')) {
                task = true;
                checked = (f[1] == 'x' || f[1] == 'X');
                f = f.size() > 3 ? ltrim(f.substr(3)) : "";
              }
            }
            MarkdownParser sub;
            sub.opt_ = opt_;
            sub.lines_ = item_lines;
            sub.src_ = src_;
            sub.doc_ = doc_;
            sub.li_ = 0;
            b.items.push_back(sub.parse_blocks(depth + 1));
            b.item_is_task.push_back(task);
            b.item_checked.push_back(checked);
            continue;
          }
          // continuation line that is not a new item
          if (indent_of(l2) >= ind + 2) {
            // belongs to the current item: handled inside the item loop; here treat as lazy wrap
            li_++;
            continue;
          }
          break;
        }
        if (b.items.empty()) {
          // not actually a list (e.g. "*" alone)
          b.type = Block::Paragraph;
          std::vector<std::string> para;
          para.push_back(line);
          li_++;
          while (li_ < lines_.size() && !is_blank(lines_[li_])) { para.push_back(lines_[li_]); li_++; }
          b.spans = parse_inline(join(para, "\n"), b.src_line);
          blocks.push_back(b);
          continue;
        }
        blocks.push_back(b);
        continue;
      }
    }

    // -------------------------------------------------------------- table --
    if (opt_.tables && line.find('|') != std::string::npos && li_ + 1 < lines_.size() &&
        lines_[li_ + 1].find('-') != std::string::npos) {
      auto split_row = [&](const std::string& l, std::vector<std::string>& cells) {
        std::string row = trim(l);
        if (!row.empty() && row.front() == '|') row = row.substr(1);
        if (!row.empty() && row.back() == '|') row.pop_back();
        std::string cur;
        bool esc = false;
        for (char c : row) {
          if (esc) { cur += c; esc = false; continue; }
          if (c == '\\') { esc = true; cur += c; continue; }
          if (c == '|') { cells.push_back(trim(cur)); cur.clear(); }
          else cur += c;
        }
        cells.push_back(trim(cur));
      };
      std::vector<std::string> header_cells, delim_cells;
      split_row(line, header_cells);
      split_row(lines_[li_ + 1], delim_cells);
      bool valid = delim_cells.size() >= 1 && delim_cells.size() >= header_cells.size();
      std::vector<Align> aligns;
      if (valid) {
        for (auto& d : delim_cells) {
          std::string t = trim(d);
          if (t.empty()) { valid = false; break; }
          bool left = t.front() == ':';
          bool right = t.back() == ':';
          std::string body = t;
          if (left) body = body.substr(1);
          if (right && !body.empty()) body.pop_back();
          if (body.empty() || body.find_first_not_of('-') != std::string::npos) { valid = false; break; }
          aligns.push_back(left && right ? Align::Center : (right ? Align::Right : Align::Left));
        }
      }
      if (valid) {
        b.type = Block::Table;
        b.has_header = true;
        int tl = (int)li_;
        auto add_row = [&](const std::vector<std::string>& cells, bool header) {
          std::vector<TableCell> row;
          for (size_t ci = 0; ci < cells.size(); ci++) {
            TableCell tc;
            tc.header = header;
            tc.align = ci < aligns.size() ? aligns[ci] : Align::Left;
            tc.spans = parse_inline(cells[ci], tl);
            row.push_back(tc);
          }
          b.rows.push_back(row);
        };
        add_row(header_cells, true);
        li_ += 2;
        while (li_ < lines_.size() && !is_blank(lines_[li_]) && lines_[li_].find('|') != std::string::npos) {
          std::vector<std::string> cells;
          split_row(lines_[li_], cells);
          add_row(cells, false);
          li_++;
        }
        blocks.push_back(b);
        continue;
      }
    }

    // --------------------------------------------------------- html block --
    {
      std::string t = trim(line);
      if (!t.empty() && t[0] == '<' && t.size() > 2 &&
          (isalpha((unsigned char)t[1]) || t[1] == '/' || t[1] == '!')) {
        std::string low = to_lower(t);
        static const char* kBlockTags[] = {"<div", "<p", "<table", "<pre", "<details", "<summary",
                                           "<ul", "<ol", "<li", "<section", "<article", "<figure",
                                           "<svg", "<img", "<video", "<iframe", "<blockquote", "<!--"};
        bool is_block = false;
        for (auto* tag : kBlockTags) {
          if (low.rfind(tag, 0) == 0) { is_block = true; break; }
        }
        if (is_block) {
          b.type = Block::Html;
          b.code = line;
          li_++;
          while (li_ < lines_.size() && !is_blank(lines_[li_])) {
            std::string lt = trim(lines_[li_]);
            b.code += "\n" + lines_[li_];
            li_++;
            if (!lt.empty() && lt.back() == '>') {
              // stop once the tag closes and there is a blank line after
              if (li_ < lines_.size() && is_blank(lines_[li_])) break;
            }
          }
          if (to_lower(trim(line)).rfind("<!--", 0) == 0) {
            // HTML comments are not shown
            continue;
          }
          blocks.push_back(b);
          continue;
        }
      }
    }

    // ---------------------------------------------------------- paragraph --
    {
      std::vector<std::string> para;
      int start = (int)li_;
      while (li_ < lines_.size()) {
        const std::string& l2 = lines_[li_];
        if (is_blank(l2)) break;
        std::string t = ltrim(l2);
        if (!para.empty()) {
          if (indent_of(l2) <= 3 && (t.rfind("```", 0) == 0 || t.rfind("~~~", 0) == 0)) break;
          if (indent_of(l2) <= 3 && !t.empty() && t[0] == '#') { std::string dummy; if (heading_level(l2, dummy) > 0) break; }
          if (trim(l2)[0] == '>') break;
          if (is_hr_line(l2)) break;
          size_t ind2 = indent_of(l2);
          bool o2 = false; int n2 = 0; size_t m2 = 0;
          if (ind2 <= 3 && is_list_item(l2, ind2, &o2, &n2, &m2)) break;
        }
        para.push_back(trim(l2));
        li_++;
      }
      if (para.empty()) { li_++; continue; }
      b.type = Block::Paragraph;
      b.src_line = start + 1;
      b.spans = parse_inline(join(para, "\n"), start);
      blocks.push_back(b);
      continue;
    }
  }
  return blocks;
}

void collect_links(const Block& b, std::vector<LinkRef>& out) {
  for (auto& sp : b.spans) {
    if (sp.is_link && sp.kind != Span::Image) {
      LinkRef lr;
      lr.text = sp.kind == Span::Text ? sp.text : sp.tex;
      lr.url = sp.link;
      out.push_back(lr);
    } else if (sp.kind == Span::Image) {
      LinkRef lr;
      lr.text = sp.text.empty() ? "[image]" : sp.text;
      lr.url = sp.link;
      out.push_back(lr);
    }
  }
  for (auto& row : b.rows)
    for (auto& cell : row)
      for (auto& sp : cell.spans)
        if (sp.is_link) {
          LinkRef lr;
          lr.text = sp.text;
          lr.url = sp.link;
          out.push_back(lr);
        }
  for (auto& item : b.items)
    for (auto& ib : item) collect_links(ib, out);
}

MdDocument MarkdownParser::parse(const std::string& text, const MdOptions& opt) {
  opt_ = opt;
  MdDocument doc;
  doc_ = &doc;
  (void)doc_;
  // normalise line endings and expand tabs
  std::string norm;
  norm.reserve(text.size());
  for (size_t i = 0; i < text.size(); i++) {
    if (text[i] == '\r') continue;
    if (text[i] == '\t') { norm += "    "; continue; }
    norm += text[i];
  }
  lines_ = split_lines(norm);
  lines_.push_back("");  // sentinel
  li_ = 0;
  doc.blocks = parse_blocks(0);

  // collect outline + links + title
  for (size_t i = 0; i < doc.blocks.size(); i++) {
    Block& b = doc.blocks[i];
    if (b.type == Block::Heading) {
      int lvl = b.level;
      doc.outline.emplace_back((int)i, spans_plain_text(b.spans));
      if (lvl == 1 && doc.title.empty()) doc.title = spans_plain_text(b.spans);
    }
    collect_links(b, doc.links);
  }
  if (doc.title.empty() && !doc.blocks.empty()) {
    for (auto& b : doc.blocks) {
      if (b.type == Block::Paragraph) { doc.title = spans_plain_text(b.spans); break; }
    }
  }
  return doc;
}

}  // namespace mdt
