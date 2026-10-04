// md.h : Markdown (CommonMark subset + GFM tables/strikethrough/task lists)
//        including $...$/$$...$$ maths spans.
#pragma once
#include <string>
#include <vector>

#include "util.h"

namespace mdt {

enum class Align { Left, Center, Right };

struct Span {
  enum Kind { Text, Code, Math, LineBreak, Image } kind = Text;
  std::string text;   // Text / Code
  std::string tex;    // Math
  bool display_math = false;
  // inline styles
  bool bold = false, italic = false, strike = false, underline = false, dim = false;
  bool is_link = false;
  std::string link;
  bool has_color = false;
  RGB color{0, 0, 0};
};

struct TableCell {
  std::vector<Span> spans;
  Align align = Align::Left;
  bool header = false;
};

struct Block;

struct Block {
  enum Type { Paragraph, Heading, CodeBlock, Quote, List, Table, Hr, Html, MathBlock, Image, Definition } type = Paragraph;
  int level = 0;                  // heading level / quote nesting depth
  std::vector<Span> spans;        // paragraph, heading
  std::string code, lang;         // code block
  bool code_is_math = false;
  // list
  bool ordered = false;
  int start_num = 1;
  int marker_width = 2;                // width of "- " / "1. " in source
  std::vector<std::vector<Block>> items;
  std::vector<bool> item_checked;      // task lists (- [ ])
  std::vector<bool> item_is_task;
  // table
  std::vector<std::vector<TableCell>> rows;
  bool has_header = false;
  // image
  std::string img_src, img_title, img_alt;
  int img_w = 0, img_h = 0;
  bool img_sized = false;
  int src_line = 0;
};

struct LinkRef {
  std::string text, url;
  int line = 0;
};

struct MdDocument {
  std::vector<Block> blocks;
  bool entities_unescaped = false;  // the source looked HTML-escaped and was decoded
  int backslashes_removed = 0;      // >0 when the source looked backslash-escaped
  bool loose_markers = false;       // "#标题" / "-项目" style markers were accepted
  std::string title;               // first h1 if present
  std::vector<LinkRef> links;      // all links in document order
  std::vector<std::pair<int, std::string>> outline;  // (block index, heading text)
};

struct MdOptions {
  enum Entities { EntOff = 0, EntAuto = 1, EntForce = 2 };
  enum Escapes { EscOff = 0, EscAuto = 1, EscForce = 2 };
  int entities = EntAuto;    // decode HTML character references (see md.cpp)
  int escapes = EscAuto;     // drop markdown backslash escapes a generator left in
  bool math = true;          // recognise $...$ / $$...$$
  bool tables = true;
  bool strikethrough = true;
  bool autolink = true;
  bool task_lists = true;
  // CJK authors often write "#标题" / "-项目" / ">引用" without the space that
  // CommonMark requires after the marker.  In LooseAuto the parser accepts that
  // only for a document that uses no space-separated marker at all (see
  // looks_unspaced_markers()).
  enum Loose { LooseOff = 0, LooseAuto = 1, LooseOn = 2 };
  int loose = LooseAuto;
};

class MarkdownParser {
 public:
  MdDocument parse(const std::string& text, const MdOptions& opt = MdOptions());
  // Parse inline markup only (used for headings in the TOC, table cells, ...).
  std::vector<Span> parse_inline(const std::string& text, int line_no = 0);

 public:
  int html_depth_ = 0;      // recursion guard for HTML -> Markdown conversion
  bool loose_markers_ = false;  // accept "#标题" / "-项目" / "1.项目" (see MdOptions::loose)

 private:
  MdOptions opt_;
  const std::string* src_ = nullptr;
  MdDocument* doc_ = nullptr;
  std::vector<std::string> lines_;
  size_t li_ = 0;  // current line index

  std::vector<Block> parse_blocks(int depth);
  bool try_fence(Block& out);
  bool try_table(Block& out);
  bool try_hr(Block& out);
  bool try_heading(Block& out);
  bool try_quote(Block& out, int depth);
  bool try_list(Block& out, int depth);
  bool try_html_block(Block& out);
  bool try_math_block(Block& out);
  bool is_list_item(const std::string& line, size_t indent, bool* ordered, int* num, size_t* marker_len);
  std::vector<Span> inline_parse(const std::string& s, int line, bool allow_math);
};

// Walks an inline string and returns only the plain text (no markup).
std::string spans_plain_text(const std::vector<Span>& spans);
void collect_links(const Block& b, std::vector<LinkRef>& out);

}  // namespace mdt
