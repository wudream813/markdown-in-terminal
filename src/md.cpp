// md.cpp : Markdown parser implementation.
#include "md.h"

#include <algorithm>
#include <cstring>

#include <map>
#include <set>

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
    {"&nbsp;", "\u00a0"}, {"&thinsp;", "\u2009"}, {"&minus;", "\u2212"}, {"&middot;", "\u00b7"},
    {"&bull;", "\u2022"}, {"&deg;", "\u00b0"}, {"&micro;", "\u00b5"}, {"&para;", "\u00b6"},
    {"&sect;", "\u00a7"}, {"&euro;", "\u20ac"}, {"&pound;", "\u00a3"}, {"&yen;", "\u00a5"},
    {"&cent;", "\u00a2"}, {"&laquo;", "\u00ab"}, {"&raquo;", "\u00bb"}, {"&ldquo;", "\u201c"},
    {"&rdquo;", "\u201d"}, {"&lsquo;", "\u2018"}, {"&rsquo;", "\u2019"}, {"&dagger;", "\u2020"},
    {"&prime;", "\u2032"}, {"&Prime;", "\u2033"}, {"&nabla;", "\u2207"}, {"&isin;", "\u2208"},
    {"&notin;", "\u2209"}, {"&forall;", "\u2200"}, {"&exist;", "\u2203"}, {"&empty;", "\u2205"},
    {"&radic;", "\u221a"}, {"&prop;", "\u221d"}, {"&ang;", "\u2220"}, {"&and;", "\u2227"},
    {"&or;", "\u2228"}, {"&cap;", "\u2229"}, {"&cup;", "\u222a"}, {"&there4;", "\u2234"},
    {"&sim;", "\u223c"}, {"&cong;", "\u2245"}, {"&asymp;", "\u2248"}, {"&equiv;", "\u2261"},
    {"&sub;", "\u2282"}, {"&sup;", "\u2283"}, {"&sube;", "\u2286"}, {"&supe;", "\u2287"},
    {"&oplus;", "\u2295"}, {"&otimes;", "\u2297"}, {"&perp;", "\u22a5"}, {"&sdot;", "\u22c5"},
    {"&lceil;", "\u2308"}, {"&rceil;", "\u2309"}, {"&lfloor;", "\u230a"}, {"&rfloor;", "\u230b"},
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

// A file that has been through an HTML pipeline ("copy as markdown", CMS
// exports, some converters) arrives with its punctuation escaped as character
// references: tabs are &#x09;, and "#", "*", "_", "|", "-" become &#35;, &#42;,
// &#95;, &#124;, &#45;.  Just decoding entities per CommonMark is not enough
// there - the structure is gone, because block parsing looks at the literal
// characters.  Detect that shape and decode the whole source first.
bool looks_entity_escaped(const std::string& s) {
  static const char* kSyntax = "#*_`|>-[]()!~=+.:\\/ \t\n\"'";
  size_t total = 0, syntax = 0;
  for (size_t i = 0; i + 2 < s.size(); i++) {
    if (s[i] != '&' || s[i + 1] != '#') {
      // named reference?
      if (s[i] == '&') {
        size_t semi = s.find(';', i);
        if (semi != std::string::npos && semi - i <= 9) {
          std::string name = s.substr(i + 1, semi - i - 1);
          if (name == "amp" || name == "lt" || name == "gt" || name == "quot" || name == "apos" ||
              name == "nbsp") {
            total++;
            // "&amp;#35;" is a numeric reference that was escaped twice - a
            // strong hint that the whole document came through such a pipeline.
            if (name == "amp" && semi + 1 < s.size() && s[semi + 1] == '#') syntax++;
          }
        }
      }
      continue;
    }
    size_t semi = s.find(';', i);
    if (semi == std::string::npos || semi - i > 9) continue;
    bool hex = i + 2 < s.size() && (s[i + 2] == 'x' || s[i + 2] == 'X');
    std::string digits = s.substr(i + (hex ? 3 : 2), semi - i - (hex ? 3 : 2));
    if (digits.empty()) continue;
    for (char c : digits) {
      bool ok = hex ? isxdigit((unsigned char)c) != 0 : isdigit((unsigned char)c) != 0;
      if (!ok) { digits.clear(); break; }
    }
    if (digits.empty()) continue;
    long v = strtol(digits.c_str(), nullptr, hex ? 16 : 10);
    if (v <= 0 || v > 0x10ffff) continue;
    total++;
    if (v < 128 && strchr(kSyntax, (char)v)) syntax++;
    i = semi;
  }
  // Escaped punctuation in a line of text is a strong signal; a few &amp; are not.
  return syntax >= 3 || total >= 40;
}



// Text copied out of a chat client, a PDF or a web page carries invisible
// characters: a byte-order mark, zero width spaces, language marks, non
// breaking spaces.  They make the first character of a line look eaten (the
// glyph is there but zero cells wide), and they stop "#" or "-" from being
// recognised as a marker because the line does not start with them any more.
std::string strip_invisible(const std::string& s, int* removed) {
  std::string o;
  o.reserve(s.size());
  int n = 0;
  for (size_t i = 0; i < s.size();) {
    unsigned char c = (unsigned char)s[i];
    if (c == 0xEF && i + 2 < s.size() && (unsigned char)s[i + 1] == 0xBB &&
        (unsigned char)s[i + 2] == 0xBF) {  // U+FEFF byte order mark
      i += 3;
      n++;
      continue;
    }
    if (c == 0xC2 && i + 1 < s.size() && (unsigned char)s[i + 1] == 0xA0) {  // U+00A0
      o += ' ';
      i += 2;
      n++;
      continue;
    }
    if (c == 0xE2 && i + 2 < s.size() && (unsigned char)s[i + 1] == 0x80) {
      unsigned char t = (unsigned char)s[i + 2];
      if (t == 0x8B || t == 0x8C || t == 0x8D ||              // ZWSP, ZWNJ, ZWJ?
          (t >= 0x8E && t <= 0x8F) || (t >= 0xAA && t <= 0xAE)) {  // LRM/RLM, bidi
        // ZWSP, ZWNJ, LRM, RLM, the bidi controls: they carry no width
        i += 3;
        n++;
        continue;
      }
    }
    o += s[i++];
  }
  if (removed) *removed = n;
  return o;
}

// Whether a document was run through a markdown generator that escaped every
// syntax character ("\#", "\-", "\*", "1\.") - such a file parses as one long
// paragraph of punctuation-spotted prose, which is what the reader then shows.
// Escaped markdown examples exist in the wild too, so the test is deliberately
// conservative and can be switched off with --escapes=off.
bool looks_backslash_escaped(const std::string& text) {
  static const std::string kPunct = "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";
  static const std::string kSyntax = "#-*+>|`_[]()!~.";
  int total = 0, syntax = 0, block = 0, lines_with = 0, nonblank = 0;
  for (const std::string& ln : split_lines(text)) {
    if (trim(ln).empty()) continue;
    nonblank++;
    bool has = false;
    for (size_t i = 0; i + 1 < ln.size(); i++) {
      if (ln[i] != '\\') continue;
      if (kPunct.find(ln[i + 1]) == std::string::npos) continue;
      total++;
      has = true;
      if (kSyntax.find(ln[i + 1]) != std::string::npos) syntax++;
      i++;
    }
    if (has) lines_with++;
    std::string t = trim(ln);
    if (t.size() >= 2 && t[0] == '\\' && std::string("#-*+>").find(t[1]) != std::string::npos) {
      block++;  // an escaped heading marker or bullet at the start of a line
    } else {
      size_t d = 0;
      while (d < t.size() && isdigit((unsigned char)t[d])) d++;
      if (d > 0 && d + 1 < t.size() && t[d] == '\\' && t[d + 1] == '.') block++;  // "1\. item"
    }
  }
  if (block >= 2) return true;   // headings/bullets are escaped: structure is lost
  if (syntax >= 6) return true;  // plenty of escaped syntax characters
  if (total >= 12 && lines_with * 2 >= nonblank) return true;  // escaped line by line
  return false;
}

// Remove the backslashes of markdown escapes (CommonMark: a backslash escapes
// ASCII punctuation).  "\a" or a Windows path "C:\Users" is left alone.
std::string unescape_backslashes(const std::string& s, int* removed) {
  static const std::string kPunct = "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";
  std::string o;
  o.reserve(s.size());
  int n = 0;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '\\' && i + 1 < s.size() && kPunct.find(s[i + 1]) != std::string::npos) {
      i++;
      o += s[i];
      n++;
      continue;
    }
    o += s[i];
  }
  if (removed) *removed = n;
  return o;
}

// ---------------------------------------------------------------- HTML ------
// Markdown files that came out of a web page, a word processor or a "copy as
// markdown" tool are often HTML inside a .md file: <p>, <ul><li>, <table>,
// <pre>, <strong>, <a href>.  Left alone they render as one dim blob (or, for
// block level tags, as nothing at all).  The converter below turns that HTML
// back into the Markdown it stands for, so the normal renderer applies the
// structure, and the receiver re-parses it.
std::string html_attr(const std::string& attrs, const char* name) {
  std::string low = to_lower(attrs);
  size_t p = low.find(std::string(name) + "=");
  if (p == std::string::npos) return "";
  size_t v = p + strlen(name) + 1;
  if (v >= attrs.size()) return "";
  char q = attrs[v];
  if (q == '"' || q == '\'') {
    size_t e = attrs.find(q, v + 1);
    if (e == std::string::npos) return "";
    return attrs.substr(v + 1, e - v - 1);
  }
  size_t e = attrs.find_first_of(" \t\r\n>", v);
  return attrs.substr(v, e == std::string::npos ? std::string::npos : e - v);
}

// The inline stripper must not eat prose placeholders such as <that>, <T> or
// "a <not a tag> b": only names that are actually HTML tags are treated as
// markup, everything else stays visible.
bool is_known_html_tag(const std::string& name) {
  static const std::set<std::string> kTags = {
      "a", "abbr", "address", "area", "article", "aside", "audio", "b", "base",
      "bdi", "bdo", "blockquote", "body", "br", "button", "canvas", "caption",
      "cite", "code", "col", "colgroup", "data", "dd", "del", "details", "dfn",
      "dialog", "div", "dl", "dt", "em", "fieldset", "figcaption", "figure",
      "footer", "form", "h1", "h2", "h3", "h4", "h5", "h6", "head", "header",
      "hgroup", "hr", "html", "i", "iframe", "img", "input", "ins", "kbd",
      "label", "legend", "li", "link", "main", "map", "mark", "menu", "meta",
      "meter", "nav", "noscript", "object", "ol", "optgroup", "option", "output",
      "p", "picture", "pre", "progress", "q", "rp", "rt", "ruby", "s", "samp",
      "script", "section", "select", "slot", "small", "source", "span", "strong",
      "style", "sub", "summary", "sup", "svg", "table", "tbody", "td",
      "template", "textarea", "tfoot", "th", "thead", "time", "title", "tr",
      "track", "u", "ul", "var", "video", "wbr"};
  return kTags.count(name) > 0;
}

// What follows the tag name must look like a real attribute list - name=value
// pairs like href="x", src="y" or width=3, or nothing at all (as in <br> or
// </em>).  Prose such as "a < b and c > d" then cannot be taken for markup,
// because its pretend attributes have no '='.
bool html_tag_syntax_ok(const std::string& inner) {
  std::string t = trim(inner);
  if (t.empty()) return false;
  if (t[0] == '/') t = trim(t.substr(1));
  size_t sp = t.find_first_of(" \t\r\n");
  if (sp == std::string::npos) return true;        // <br>, </em>, <that>
  std::string rest = t.substr(sp);
  size_t i = 0;
  bool any = false;
  while (i < rest.size()) {
    while (i < rest.size() && isspace((unsigned char)rest[i])) i++;
    if (i >= rest.size()) break;
    if (rest[i] == '/') { i++; continue; }         // self closing slash
    size_t st = i;
    while (i < rest.size() && (isalnum((unsigned char)rest[i]) || rest[i] == '-' ||
                               rest[i] == '_' || rest[i] == ':' || rest[i] == '.')) i++;
    if (i == st) return false;
    while (i < rest.size() && isspace((unsigned char)rest[i])) i++;
    if (i >= rest.size() || rest[i] != '=') return false;  // valueless: not a tag
    i++;
    while (i < rest.size() && isspace((unsigned char)rest[i])) i++;
    if (i < rest.size() && (rest[i] == '"' || rest[i] == '\'')) {
      char q = rest[i++];
      while (i < rest.size() && rest[i] != q) i++;
      if (i >= rest.size()) return false;
      i++;
    } else {
      size_t vs = i;
      while (i < rest.size() && !isspace((unsigned char)rest[i])) i++;
      if (i == vs) return false;
    }
    any = true;
  }
  return any;
}

// One inline tag -> the markdown equivalent ("" when it carries no meaning).
std::string html_inline_tag(const std::string& tag) {
  std::string low = to_lower(trim(tag));
  bool closing = !low.empty() && low[0] == '/';
  std::string name = closing ? trim(low.substr(1)) : low;
  std::string attrs;
  size_t sp = name.find_first_of(" \t\r\n/");
  if (sp != std::string::npos) { attrs = name.substr(sp); name = name.substr(0, sp); }
  static std::map<std::string, const char*> kMap = {
      {"strong", "**"}, {"b", "**"}, {"em", "*"}, {"i", "*"}, {"del", "~~"},
      {"s", "~~"}, {"strike", "~~"}, {"code", "`"}, {"u", ""}, {"span", ""},
      {"sup", "~"}, {"sub", "~"}, {"br", "\n"}, {"hr", "\n---\n"},
      {"mark", ""}, {"kbd", "`"}, {"var", "*"}, {"cite", "*"}, {"q", "\""},
  };
  auto it = kMap.find(name);
  if (it != kMap.end()) return it->second;
  if (name == "a") {
    if (closing) return "";
    std::string href = html_attr(attrs, "href");
    return "[";
  }
  if (name == "img") {
    std::string src = html_attr(attrs, "src");
    std::string alt = html_attr(attrs, "alt");
    if (src.empty()) return "";
    return "![" + alt + "](" + src + ")";
  }
  return "";  // unknown tag: drop it, keep the text
}

std::string html_to_markdown(const std::string& html) {
  std::string out;
  std::string href, alt, img_src;
  bool in_pre = false;
  bool pre_skip_nl = false;  // swallow the newline right after <pre>
  size_t i = 0;
  int list_depth = 0;
  std::vector<int> list_count;
  std::vector<char> list_kind;  // 'u' = <ul>, 'o' = <ol>
  int row_cells = 0, first_row_cells = 0;

  // collapse runs of whitespace but KEEP one leading/trailing space, so that
  // "with <strong>bold</strong> and" does not become "withboldand".
  auto collapse_text = [](const std::string& s2) {
    std::string o; bool sp = false;
    for (unsigned char c2 : s2) {
      if (c2 == ' ' || c2 == '\t' || c2 == '\n' || c2 == '\r') sp = true;
      else { if (sp) o += ' '; sp = false; o += (char)c2; }
    }
    if (sp) o += ' ';
    return o;
  };
  // drop spaces/tabs left dangling at the end of the current output line
  auto rtrim_line = [&]() {
    size_t nl = out.find_last_of('\n');
    size_t start = (nl == std::string::npos) ? 0 : nl + 1;
    size_t e2 = out.size();
    while (e2 > start && (out[e2 - 1] == ' ' || out[e2 - 1] == '\t' || out[e2 - 1] == '\r')) e2--;
    out.erase(e2);
  };
  auto ends_blank = [&]() {
    size_t n = 0;
    while (n < out.size() && out[out.size() - 1 - n] == '\n') n++;
    return n >= 2;
  };
  auto blank = [&]() {
    rtrim_line();
    if (out.empty()) return;
    if (out.back() != '\n') out += "\n";
    if (!ends_blank()) out += "\n";
  };
  auto soft = [&]() {
    rtrim_line();
    if (!out.empty() && out.back() != '\n') out += "\n";
  };

  while (i < html.size()) {
    if (html[i] != '<') {
      std::string chunk;
      while (i < html.size() && html[i] != '<') chunk += html[i++];
      if (in_pre) {
        if (pre_skip_nl) {
          pre_skip_nl = false;
          size_t k = 0;
          while (k < chunk.size() && (chunk[k] == '\n' || chunk[k] == '\r' || chunk[k] == ' ' || chunk[k] == '\t')) k++;
          chunk = chunk.substr(k);
        }
        out += decode_entities(chunk);
      }
      else out += collapse_text(chunk);  // HTML collapses whitespace
      continue;
    }
    if (html.compare(i, 4, "<!--") == 0) {  // comment
      size_t e = html.find("-->", i);
      i = (e == std::string::npos) ? html.size() : e + 3;
      continue;
    }
    size_t e = html.find('>', i);
    if (e == std::string::npos) { out += decode_entities(html.substr(i)); break; }
    std::string raw = html.substr(i + 1, e - i - 1);
    i = e + 1;
    std::string low = to_lower(trim(raw));
    bool closing = !low.empty() && low[0] == '/';
    std::string name = closing ? trim(low.substr(1)) : low;
    std::string attrs;
    size_t sp = name.find_first_of(" \t\r\n/");
    if (sp != std::string::npos) { attrs = name.substr(sp); name = name.substr(0, sp); }

    if (name == "script" || name == "style") {  // drop contents outright
      if (!closing) {
        std::string close = "</" + name;
        size_t c = to_lower(html).find(close, i);
        i = (c == std::string::npos) ? html.size() : c;
      }
      continue;
    }
    if (in_pre) {
      if (name == "pre" && closing) { out += "\n```\n"; in_pre = false; }
      else if (name == "br") out += "\n";
      // everything else inside <pre> is literal text
      continue;
    }
    if (name == "pre") {
      if (!closing) { blank(); out += "```\n"; in_pre = true; pre_skip_nl = true; }
      continue;
    }
    if (name.size() == 2 && name[0] == 'h' && name[1] >= '1' && name[1] <= '6') {
      if (closing) blank();
      else { blank(); out += std::string((size_t)(name[1] - '0'), '#') + " "; }
      continue;
    }
    if (name == "p" || name == "div" || name == "section" || name == "article" ||
        name == "header" || name == "footer" || name == "blockquote") {
      if (name == "blockquote") {
        if (!closing) { blank(); out += "> "; }
        else blank();
      } else if (closing) blank();
      else blank();
      continue;
    }
    if (name == "ul" || name == "ol") {
      if (closing) {
        if (list_depth > 0) list_depth--;
        if (!list_count.empty()) list_count.pop_back();
        if (!list_kind.empty()) list_kind.pop_back();
        blank();
      } else {
        blank();
        list_depth++;
        list_count.push_back(0);
        list_kind.push_back(name == "ol" ? 'o' : 'u');
      }
      continue;
    }
    if (name == "li") {
      if (closing) { soft(); continue; }
      soft();
      std::string indent((size_t)std::max(0, list_depth - 1) * 2, ' ');
      bool ordered = !list_kind.empty() && list_kind.back() == 'o';
      if (ordered) {
        int n = ++list_count.back();
        out += indent + std::to_string(n) + ". ";
      } else {
        out += indent + "- ";
      }
      continue;
    }
    if (name == "table") { blank(); continue; }
    if (name == "tr") {
      if (!closing) { soft(); out += "|"; row_cells = 0; }
      else {
        out += "\n";
        if (first_row_cells == 0) {
          first_row_cells = row_cells;
          for (int c = 0; c < first_row_cells; c++) out += "| --- ";
          out += "|\n";
        }
      }
      continue;
    }
    if (name == "td" || name == "th") {
      if (closing) out += " |";
      else { out += " "; row_cells++; }
      continue;
    }
    if (name == "br") { out += "\n"; continue; }
    if (name == "hr") { blank(); out += "---"; blank(); continue; }
    if (name == "strong" || name == "b") { out += "**"; continue; }
    if (name == "em" || name == "i") { out += "*"; continue; }
    if (name == "del" || name == "s" || name == "strike") { out += "~~"; continue; }
    if (name == "code") { out += "`"; continue; }
    if (name == "a") {
      if (closing) out += href.empty() ? "" : ("](" + href + ")");
      else { href = html_attr(attrs, "href"); out += "["; }
      continue;
    }
    if (name == "img") {
      std::string src = html_attr(attrs, "src");
      std::string a = html_attr(attrs, "alt");
      if (!src.empty()) out += "![" + a + "](" + src + ")";
      continue;
    }
    if (name == "sup" || name == "sub") { out += "~"; continue; }
    // unknown tag: drop it, keep the text
  }
  if (in_pre) out += "\n```\n";
  return decode_entities(out);
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
          // inline HTML: turn the tag into the markdown it stands for, so
          // <strong>, <em>, <code>, <a href>, <img> and <br> keep their meaning.
          bool looks_tag = false;
          {
            std::string t2 = trim(inner);
            bool cl = !t2.empty() && t2[0] == '/';
            std::string nm = cl ? trim(t2.substr(1)) : t2;
            size_t sp0 = nm.find_first_of(" \t\r\n/");
            if (sp0 != std::string::npos) nm = nm.substr(0, sp0);
            nm = to_lower(nm);
            if (t2.rfind("!--", 0) == 0) looks_tag = true;
            else if (!nm.empty() && is_known_html_tag(nm) && inner.find('<') == std::string::npos &&
                     html_tag_syntax_ok(inner)) looks_tag = true;
          }
          if (looks_tag) {
            std::string tag_md;
            // Prefer converting the whole <tag>...</tag> pair at once: that is
            // the only way an <a href="..."> keeps its target.
            std::string low = to_lower(inner);
            bool closing = !low.empty() && low[0] == '/';
            std::string name = closing ? trim(low.substr(1)) : low;
            size_t sp2 = name.find_first_of(" \t\r\n/");
            if (sp2 != std::string::npos) name = name.substr(0, sp2);
            static const char* kPair[] = {"a", "strong", "b", "em", "i", "code", "kbd",
                                          "del", "s", "strike", "span", "u", "sup", "sub"};
            bool pairable = false;
            for (const char* pn : kPair) if (name == pn) { pairable = true; break; }
            if (!closing && pairable) {
              std::string close = "</" + name;
              size_t c = to_lower(s).find(close, i);
              if (c != std::string::npos && c - i < 4000) {
                size_t ce = s.find('>', c);
                if (ce != std::string::npos) {
                  tag_md = html_to_markdown(s.substr(i, ce + 1 - i));
                  i = ce + 1;
                }
              }
            }
            if (tag_md.empty()) { tag_md = html_inline_tag(inner); i = e + 1; }
            // an inline fragment must stay on one line
            std::string flat;
            for (char ch2 : tag_md) flat += (ch2 == '\n') ? ' ' : ch2;
            buf += collapse_ws(flat);
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
// True for a character that starts a CJK word: ideographs, kana, Hangul,
// fullwidth forms - the scripts whose writers drop the space after a marker.
bool is_cjk_lead(uint32_t cp) {
  return (cp >= 0x2E80 && cp <= 0x9FFF) || (cp >= 0xAC00 && cp <= 0xD7AF) ||
         (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xFF00 && cp <= 0xFFEF) ||
         (cp >= 0x3000 && cp <= 0x303F);
}

// "##标题", "-项目", ">引用", "1.项目": a block marker with no space behind it,
// used in a document that never uses the CommonMark spelling.
bool looks_unspaced_markers(const std::string& text) {
  int unspaced = 0, spaced = 0;
  for (const std::string& raw : split_lines(text)) {
    std::string t = ltrim(raw);
    if (t.empty()) continue;
    uint32_t cp = 0;
    bool marker = false, space_after = false;
    if (t[0] == '#') {
      size_t h = 0;
      while (h < t.size() && t[h] == '#') h++;
      if (h <= 6 && h < t.size()) {
        marker = true;
        size_t k = h;
        cp = utf8_next(t, k);
        space_after = (t[h] == ' ' || t[h] == '\t');
      }
    } else if (t[0] == '-' || t[0] == '*' || t[0] == '+' || t[0] == '>') {
      if (t.size() > 1) {
        marker = true;
        size_t k = 1;
        cp = utf8_next(t, k);
        space_after = (t[1] == ' ' || t[1] == '\t');
      }
    } else if (isdigit((unsigned char)t[0])) {
      size_t d = 0;
      while (d < t.size() && isdigit((unsigned char)t[d])) d++;
      if (d < t.size() && (t[d] == '.' || t[d] == ')') && d + 1 < t.size()) {
        marker = true;
        size_t k = d + 1;
        cp = utf8_next(t, k);
        space_after = (t[d + 1] == ' ' || t[d + 1] == '\t');
      }
    }
    if (!marker) continue;
    if (space_after) spaced++;
    else if (is_cjk_lead(cp)) unspaced++;
  }
  // Only a document that is consistently written that way, and that would
  // otherwise come out as flat prose, is reinterpreted.
  return unspaced >= 3 && spaced == 0;
}

int heading_level(const std::string& line, std::string& text, bool loose = false) {
  size_t i = 0;
  while (i < line.size() && line[i] == ' ') i++;
  size_t h = 0;
  while (i + h < line.size() && line[i + h] == '#') h++;
  if (h == 0 || h > 6) return 0;
  if (i + h < line.size() && line[i + h] != ' ' && line[i + h] != '\t') {
    if (!loose || i + h >= line.size()) return 0;
    size_t k = i + h;
    if (!is_cjk_lead(utf8_next(line, k))) return 0;  // "#标题" but not "#hashtag"
  }
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
  bool loose = loose_markers_;  // "#标题"/"-项目" style documents (see md.cpp top)
  char c = line[i];
  if (c == '-' || c == '*' || c == '+') {
    if (i + 1 < line.size() && (line[i + 1] == ' ' || line[i + 1] == '\t')) {
      *ordered = false;
      *num = 0;
      *marker_len = 2;
      return true;
    }
    if (loose && i + 1 < line.size()) {
      size_t k = i + 1;
      if (is_cjk_lead(utf8_next(line, k))) {  // "-项目"
        *ordered = false;
        *num = 0;
        *marker_len = 1;
        return true;
      }
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
    if (loose && j < line.size() && (line[j] == '.' || line[j] == ')') && j + 1 < line.size()) {
      size_t k = j + 1;
      if (is_cjk_lead(utf8_next(line, k))) {  // "1.项目"
        *ordered = true;
        *num = atoi(line.substr(i, j - i).c_str());
        *marker_len = j - i + 1;
        return true;
      }
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
      int lv = heading_level(line, text, loose_markers_);
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
      sub.loose_markers_ = loose_markers_;
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
          bool any_item = ind2 <= ind + 1 && is_list_item(l2, ind2, &o2, &n2, &m2);
          // "- a" followed by "1. b" is a new list, not a lazy continuation
          if (any_item && o2 != ordered) break;
          bool is_item = any_item;
          if (is_item) {
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
              // Any item marker at this level ends the current item - including
              // one of the other kind ("- a" then "1. b" starts a new list).
              if (ni <= ind + 1 && is_list_item(l3, ni, &o3, &n3, &m3)) break;
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
          // Reduce the HTML to the Markdown it stands for (<h2> -> ##, <li> ->
          // "- ", <table> -> pipe table, <strong> -> **, <a href> -> link, ...)
          // and parse that, so the document keeps its structure.  Recursion is
          // bounded in case the conversion produces another HTML block.
          if (html_depth_ < 3) {
            std::string md = html_to_markdown(b.code);
            if (getenv("MDT_DEBUG_HTML"))
              fprintf(stderr, "[mdt] html block -> markdown:\n<<<%s>>>\n", md.c_str());
            if (!trim(md).empty()) {
              MarkdownParser sub;
              sub.html_depth_ = html_depth_ + 1;
              sub.loose_markers_ = loose_markers_;
              MdDocument sub_doc = sub.parse(md);
              for (auto& sb : sub_doc.blocks) {
                if (sb.src_line == 0) sb.src_line = b.src_line;
                blocks.push_back(std::move(sb));
              }
              continue;
            }
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
          if (indent_of(l2) <= 3 && !t.empty() && t[0] == '#') {
            std::string dummy;
            if (heading_level(l2, dummy, loose_markers_) > 0) break;
          }
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
  (void)0;
  // Un-escape HTML-escaped sources before anything looks at the characters.
  std::string src = text;
  bool unescaped = false;
  if (opt_.entities != MdOptions::EntOff) {
    if (opt_.entities == MdOptions::EntForce || looks_entity_escaped(src)) {
      // Decode repeatedly: content that was escaped twice (&amp;#35;) needs two
      // passes.  Bounded so a pathological file cannot loop.
      for (int pass = 0; pass < 3; pass++) {
        std::string next = decode_entities(src);
        unescaped = true;
        if (next == src) break;
        src = next;
        if (!looks_entity_escaped(src)) break;
      }
    }
  }
  // Markdown generators (pandoc --to=markdown inside HTML, "copy as markdown"
  // tools, some exporters) escape every syntax character.  Undo that before the
  // block parser runs, otherwise a perfectly structured document reads as prose
  // with literal "_" and "#" in it.
  int invisible_removed = 0;
  {
    std::string next = strip_invisible(src, &invisible_removed);
    if (invisible_removed > 0) src = next;
  }
  int backslashes_removed = 0;
  if (opt_.escapes != MdOptions::EscOff &&
      (opt_.escapes == MdOptions::EscForce || looks_backslash_escaped(src))) {
    int removed = 0;
    std::string next = unescape_backslashes(src, &removed);
    if (removed > 0) { src = next; backslashes_removed = removed; }
  }
  MdDocument doc;
  doc_ = &doc;
  (void)doc_;
  doc.entities_unescaped = unescaped;
  doc.backslashes_removed = backslashes_removed;
  doc.invisible_removed = invisible_removed;
  // normalise line endings and expand tabs
  std::string norm;
  norm.reserve(src.size());
  for (size_t i = 0; i < src.size(); i++) {
    if (src[i] == '\r') continue;
    if (src[i] == '\t') { norm += "    "; continue; }
    norm += src[i];
  }
  lines_ = split_lines(norm);
  lines_.push_back("");  // sentinel
  li_ = 0;
  doc.blocks = parse_blocks(0);
  // A document whose markers are written without the CommonMark space ("#标题",
  // "-项目") parses as flat prose.  When nothing was recognised as a heading
  // and the whole file is written that way, parse it again leniently.
  if (opt_.loose != MdOptions::LooseOff) {
    bool has_heading = false;
    for (auto& b : doc.blocks)
      if (b.type == Block::Heading) { has_heading = true; break; }
    if (!has_heading &&
        (opt_.loose == MdOptions::LooseOn || looks_unspaced_markers(src))) {
      loose_markers_ = true;
      li_ = 0;
      doc.blocks = parse_blocks(0);
      doc.loose_markers = true;
    }
  }

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
