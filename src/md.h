// md.h : Markdown (CommonMark subset + GFM tables/strikethrough/task lists)
//        including $...$/$$...$$ maths spans.
#pragma once
#include <map>
#include <memory>
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
  bool footnote = false;   // a "[^label]" reference, drawn as a small [n]
  std::string link;
  bool has_color = false;
  RGB color{0, 0, 0};
};

struct TableCell {
  std::vector<Span> spans;
  Align align = Align::Left;
  bool header = false;
  // Luogu table merging (rehype-extend-table): a cell whose whole content is
  // one of these markers continues a neighbour instead of holding text.
  int merge = 0;   // 0 none, 1 = "^" (rowspan, continues the cell above),
                   // 2 = "<" (colspan, continues the cell to the left),
                   // 3 = ">" (colspan, continues the cell to the right)
};

struct Block;

// A pointer with value semantics: copying the owner copies the pointee.  For
// the parts of a struct that most instances do not have.
template <class T>
class ValuePtr {
 public:
  ValuePtr() = default;
  ValuePtr(const ValuePtr& o) : p_(o.p_ ? new T(*o.p_) : nullptr) {}
  ValuePtr(ValuePtr&&) noexcept = default;
  ValuePtr& operator=(const ValuePtr& o) {
    if (this != &o) p_.reset(o.p_ ? new T(*o.p_) : nullptr);
    return *this;
  }
  ValuePtr& operator=(ValuePtr&&) noexcept = default;
  const T* get() const { return p_.get(); }
  T& make() {
    if (!p_) p_.reset(new T());
    return *p_;
  }

 private:
  std::unique_ptr<T> p_;
};

// The fields only a few kinds of block use: Luogu directives, cute-table
// styles, highlighted code lines, image blocks, definition-list bullets.
// Inline they cost every block 250 bytes (a 10 MB document has 200,000
// blocks); read them with Block::x(), write them with Block::ext().
struct BlockExtra {
  std::vector<std::pair<int, int>> hl_ranges;  // Luogu "```cpp lines=5-6,11" (1-based, inclusive)
  // directive (Luogu): :::name[label]{attrs} / ::name[label]{attrs}
  std::string dir_name, dir_label, dir_attrs;
  // table style from a preceding "::cute-table{...}" leaf directive
  std::string table_style;        // "", "three", "tuack"
  std::string marker;             // bullet override (definition list: ":")
  std::string img_src, img_alt;   // image block
};

struct Block {
  enum Type { Paragraph, Heading, CodeBlock, Quote, List, Table, Hr, Html, MathBlock, Image, Definition,
              // Luogu remark-directive block: :::name[label]{attrs} ... :::
              Directive } type = Paragraph;
  int level = 0;                  // heading level / quote nesting depth
  std::vector<Span> spans;        // paragraph, heading
  std::string code, lang;         // code block
  std::vector<std::vector<Block>> items;  // list items / quote and directive bodies
  std::vector<bool> item_checked;      // task lists (- [ ])
  std::vector<bool> item_is_task;
  std::vector<std::vector<TableCell>> rows;  // table
  ValuePtr<BlockExtra> extra;
  int dir_colons = 3;             // colon count of the opening fence
  int uid = -1;                   // unique id among directive blocks (fold state)
  int table_style_arg = 0;        // tuack=N: the heavy vertical rule sits after column N
  int start_num = 1;              // list
  int marker_width = 2;           // width of "- " / "1. " in source
  int src_line = 0;
  bool code_is_math = false;
  bool dir_leaf = false;          // "::name" (exactly two colons, own line)
  bool dir_open = false;          // {open} attribute: start expanded
  bool ordered = false;           // list
  bool has_header = false;        // table

  const BlockExtra& x() const {
    static const BlockExtra none;
    return extra.get() ? *extra.get() : none;
  }
  BlockExtra& ext() { return extra.make(); }
};

struct LinkRef {
  std::string text, url;
  int line = 0;
};

struct MdDocument {
  int uid_ctr = 0;                // hands out Block::uid while parsing
  std::vector<Block> blocks;
  bool entities_unescaped = false;  // the source looked HTML-escaped and was decoded
  int backslashes_removed = 0;      // >0 when the source looked backslash-escaped
  int invisible_removed = 0;        // BOM / ZWSP / NBSP characters dropped
  int code_gaps_collapsed = 0;      // code blocks with every line double-spaced
  int code_refs_decoded = 0;        // code blocks whose HTML references were decoded
  bool loose_markers = false;       // "#标题" / "-项目" style markers were accepted
  int front_matter_lines = 0;       // YAML front matter lines that were skipped
  int link_defs_found = 0;          // reference-style link definitions resolved
  // Footnote definitions ("[^label]: text") and the numbers handed out to the
  // references, in the order the references appear in the document.
  std::map<std::string, std::string> footnote_defs;
  std::map<std::string, int> footnote_nums;
  std::vector<std::string> footnote_order;
  int footnotes_defined = 0;
  int definition_items = 0;         // definition list items ("Term" + ": def")
  // "[label]: url" definitions, keyed by the lowercased label (CommonMark
  // reference links).  They live on the document because quotes and nested
  // lists parse with a sub-parser that shares the document but not the parser.
  std::map<std::string, std::string> link_defs;
  bool luogu = false;               // remark-directive syntax seen: a Luogu document
  std::string title;               // first h1 if present
  std::vector<LinkRef> links;      // all links in document order
  std::vector<std::pair<int, std::string>> outline;  // (block index, heading text)
};

struct MdOptions {
  enum Entities { EntOff = 0, EntAuto = 1, EntForce = 2 };
  enum Escapes { EscOff = 0, EscAuto = 1, EscForce = 2 };
  int entities = EntOff;     // auto-repair of HTML-escaped sources is opt-in (--entities=force)
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
  int code_gaps_collapsed_ = 0;  // fenced blocks that had a blank line after every line
  int code_refs_decoded_ = 0;    // fenced blocks whose HTML references were decoded

 private:
  MdOptions opt_;
  const std::string* src_ = nullptr;
  MdDocument* doc_ = nullptr;
  std::vector<std::string> lines_;
  size_t li_ = 0;  // current line index

  // Removes YAML front matter and collects "[label]: url" link reference
  // definitions out of the line vector before the block parser runs; the
  // definition line is blanked (not deleted) so line numbers stay stable.
  void extract_definitions();
  std::vector<Block> parse_blocks(int depth);
  bool try_fence(Block& out);
  bool try_table(Block& out);
  bool try_hr(Block& out);
  bool try_heading(Block& out);
  bool try_quote(Block& out, int depth);
  bool try_list(Block& out, int depth);
  bool try_html_block(Block& out);
  bool try_math_block(Block& out);
  bool try_definition_list(std::vector<Block>& blocks, int depth);
  bool is_list_item(const std::string& line, size_t indent, bool* ordered, int* num, size_t* marker_len);
  std::vector<Span> inline_parse(const std::string& s, int line, bool allow_math);
};

// Walks an inline string and returns only the plain text (no markup).
std::string spans_plain_text(const std::vector<Span>& spans);
void collect_links(const Block& b, std::vector<LinkRef>& out);

}  // namespace mdt
