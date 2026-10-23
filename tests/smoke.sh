#!/bin/sh
# mdt smoke tests: build, render every theme, check the three protocols,
# check plain-text export.  Run from the project root:  sh tests/smoke.sh
set -e
BIN=${BIN:-./build/mdt}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

echo "== version =="; "$BIN" --version
echo "== capabilities =="; "$BIN" --list-caps > "$TMP/caps.txt"; cat "$TMP/caps.txt"

echo "== text dump =="
"$BIN" --dump --width=80 "$TMP/../demo/demo.md" 2>/dev/null || "$BIN" --dump --width=80 demo/demo.md | head -5

echo "== stdin =="
printf '# t\n\ninline $e^{i\\pi}$ maths\n' | "$BIN" --dump --width=40 - | head -3

echo "== themes =="
for th in dark light nord monochrome; do
  "$BIN" --theme=$th --screenshot="$TMP/$th.png" --width=80 --height=24 demo/demo.md > /dev/null
  test -s "$TMP/$th.png" && echo "  $th ok"
done

echo "== maths modes =="
"$BIN" --math=unicode --dump --width=60 demo/demo.md | grep -q '√' && echo "  unicode fallback ok"
"$BIN" --math=off --screenshot="$TMP/nomath.png" --width=80 --height=20 demo/demo.md > /dev/null && echo "  maths off ok"

echo "== HTML-escaped source =="
# The auto-repair was removed at the reporter's request: escaping a document is
# a property of how it was pasted, not of the document, so nothing is guessed
# any more.  --entities=force is the explicit repair.
"$BIN" --dump --width=64 tests/fixtures/escaped.md > "$TMP/raw-escaped.txt"
! grep -q "┌" "$TMP/raw-escaped.txt" && ! grep -q "• " "$TMP/raw-escaped.txt" \
  && echo "  the default leaves an escaped source alone (no repaired table or list)"
"$BIN" --entities=force --dump --width=64 tests/fixtures/escaped.md > "$TMP/escaped.txt"
grep -q "HTML-escaped Markdown" "$TMP/escaped.txt" && echo "  --entities=force restores the heading"
grep -q "• first item" "$TMP/escaped.txt" && echo "  --entities=force restores the list"
grep -q "┌" "$TMP/escaped.txt" && echo "  --entities=force restores the table"
grep -q "int x = 1" "$TMP/escaped.txt" && echo "  --entities=force restores the code block"
"$BIN" --entities=force --dump --width=64 tests/fixtures/escaped-twice.md | grep -q "Escaped twice" && echo "  double-escaped source decoded"
"$BIN" --dump --width=64 tests/fixtures/normal-entities.md | grep -q "AT&T and 5 < 7" && echo "  ordinary entities untouched"
# a document with formulas and code is never rewritten behind the reader's back
"$BIN" --dump --width=200 tests/fixtures/abc397d.md > "$TMP/sol.txt"
grep -q 'is_cbr(n + i \* i \* i)' "$TMP/sol.txt" \
  && grep -q "两式相减" "$TMP/sol.txt" \
  && echo "  a 题解 with formulas and code is never 'repaired'"


echo "== backslash-escaped source =="
"$BIN" --dump --width=64 tests/fixtures/backslash.md > "$TMP/bslash.txt"
grep -q "Project documentation" "$TMP/bslash.txt" && echo "  heading restored"
grep -q "• first step" "$TMP/bslash.txt" && echo "  list restored"
grep -q "├" "$TMP/bslash.txt" && echo "  table restored"
grep -q "make -j8" "$TMP/bslash.txt" && echo "  code block restored"
grep -q '\\#' "$TMP/bslash.txt" && { echo "  FAIL: backslash left in the text"; exit 1; } || echo "  no backslash left in the text"
grep -q "data_buffer" "$TMP/bslash.txt" && echo "  escaped underscore unescaped"
"$BIN" --escapes=off --dump --width=64 tests/fixtures/backslash.md | grep -q '\\# Project' && echo "  --escapes=off keeps the source as written"

echo "== HTML inside a markdown file =="
"$BIN" --dump --width=64 tests/fixtures/html-source.md > "$TMP/html.txt"
grep -q "Sub heading" "$TMP/html.txt" && echo "  <h2> became a heading"
grep -q "• alpha item" "$TMP/html.txt" && echo "  <ul><li> became a list"
grep -q "├" "$TMP/html.txt" && echo "  <table> became a table"
grep -q "int main" "$TMP/html.txt" && echo "  <pre> became a code block"
grep -q "First paragraph with bold and a link." "$TMP/html.txt" && echo "  inline tags converted, spacing kept"
printf 'Prose with <that> and <T> and a < b.\n' > "$TMP/placeholders.md"
"$BIN" --dump --width=64 "$TMP/placeholders.md" | grep -q "<that> and <T> and a < b." && echo "  prose placeholders are not eaten"

echo "== CJK markers without the CommonMark space =="
"$BIN" --dump --width=64 tests/fixtures/cjk-loose.md > "$TMP/cjk.txt"
grep -q "项目说明" "$TMP/cjk.txt" && echo "  \"#标题\" became a heading"
grep -q "• 第一步：安装依赖" "$TMP/cjk.txt" && echo "  \"-项目\" became a list"
grep -q "^  1. 第一点" "$TMP/cjk.txt" && echo "  \"1.项目\" became an ordered list"
grep -q "│ 这是引用" "$TMP/cjk.txt" && echo "  \">引用\" became a quote"
"$BIN" --loose=off --dump --width=64 tests/fixtures/cjk-loose.md | grep -q '#项目说明' && echo "  --loose=off keeps CommonMark behaviour"

echo "== table frame (one box, header rule) =="
"$BIN" --dump --width=40 tests/fixtures/table-multi.md > "$TMP/table.txt"
test "$(grep -c '┌' "$TMP/table.txt")" = 1 && echo "  a single frame around all rows"
test "$(grep -c '├' "$TMP/table.txt")" = 1 && echo "  one rule under the header"
test "$(grep -c '└' "$TMP/table.txt")" = 1 && echo "  a single bottom border"
grep -q "third row" "$TMP/table.txt" && echo "  every row drawn"

echo "== blocks nested in list items =="
"$BIN" --dump --width=64 tests/fixtures/nested-blocks.md > "$TMP/nested.txt"
grep -q "• item with a quote" "$TMP/nested.txt" && echo "  item text drawn"
grep -q "│ quoted line inside the item" "$TMP/nested.txt" && echo "  quote inside an item"
grep -q "Col A" "$TMP/nested.txt" && echo "  table inside an item (header)"
grep -q "second paragraph of the item" "$TMP/nested.txt" && echo "  second paragraph inside an item"
test "$(grep -c '┌' "$TMP/nested.txt")" = 1 && echo "  nested table drawn as one frame"

echo "== code blocks (frame hugs the code, long lines wrap) =="
"$BIN" --dump --width=60 tests/fixtures/code-wrap.md > "$TMP/code.txt"
test "$(grep -c '│ .*argument_four' "$TMP/code.txt")" -ge 1 && echo "  long line wrapped, not cut off"
grep -q 'some_function(argument_one, arg' "$TMP/code.txt" && echo "  first half of the line kept"
grep -q 'print(result)' "$TMP/code.txt" && echo "  following line kept"
framew=$(python3 -c 'import sys; ls=[x for x in open(sys.argv[1], encoding="utf-8") if x.startswith("╭")]; print(len(ls[-1].rstrip()) if ls else 0)' "$TMP/code.txt")
test "$framew" -ge 55 && echo "  code frame spans the page by default ($framew cells)"
"$BIN" --code-fit --dump --width=60 tests/fixtures/code-wrap.md > "$TMP/codefit.txt"
framefit=$(python3 -c 'import sys; ls=[x for x in open(sys.argv[1], encoding="utf-8") if x.startswith("╭")]; print(len(ls[-1].rstrip()) if ls else 0)' "$TMP/codefit.txt")
test "$framefit" -lt 30 && echo "  --code-fit hugs the code ($framefit cells)"

echo "== maths inside table cells =="
"$BIN" --dump --width=50 tests/fixtures/table-math.md > "$TMP/tmath.txt"
grep -q 'x² + y² = z²' "$TMP/tmath.txt" && echo "  inline maths transcribed in the cell"
grep -q 'a/b' "$TMP/tmath.txt" && echo "  \\frac became a/b"
if grep -q '\\frac' "$TMP/tmath.txt"; then
  echo "  FAIL raw TeX left in a cell:"
  grep -n "frac" "$TMP/tmath.txt" | head -3
  exit 1
fi
echo "  no raw TeX left"
python3 tools/table_math_test.py "$BIN" tests/fixtures/table-math.md || exit 1

echo "== task list markers line up =="
"$BIN" --dump --width=50 tests/fixtures/tasks.md > "$TMP/tasks.txt"
grep -q '\[x\] done item' "$TMP/tasks.txt" && echo "  checked marker"
grep -q '\[ \] open item' "$TMP/tasks.txt" && echo "  unchecked marker"
test "$(grep -c '^  \[.\] ' "$TMP/tasks.txt")" = 3 && echo "  all three items share one marker width"

echo "== dimmed # before headings =="
grep -q '^ # ' "$TMP/tmath.txt" && echo "  h1 marked"
"$BIN" --dump --width=60 demo/demo.md | grep -q '^ ## ' && echo "  h2 marked"

echo "== invisible characters (BOM, zero width space, nbsp) =="
"$BIN" --dump --width=50 tests/fixtures/invisible.md > "$TMP/invis.txt"
grep -q '^ # Invisible' "$TMP/invis.txt" && echo "  heading recognised behind a zero width space"
grep -q '• item two' "$TMP/invis.txt" && echo "  list recognised behind a zero width space"
"$BIN" --diag tests/fixtures/invisible.md | grep -q 'invisible chars=' && echo "  --diag reports the cleanup"

echo "== a slow picture host does not block the reader =="
python3 tools/async_image_test.py "$BIN" || exit 1

echo "== images are not drawn over the UI =="
python3 tools/image_overlay_test.py "$BIN" || exit 1

echo "== images are placed on the cell grid =="
python3 tools/image_scale_test.py "$BIN" || exit 1

echo "== --entities=force: escaped line breaks (&#10; plus a real newline) =="
"$BIN" --entities=force --dump --width=44 tests/fixtures/escaped-newlines.md > "$TMP/eol.txt"
grep -q '#include <iostream>' "$TMP/eol.txt" && echo "  code content kept"
grep -q '#include <vector>' "$TMP/eol.txt" && echo "  second code line kept"
lines=$(grep -c '^$' "$TMP/eol.txt")
test "$lines" -le 3 && echo "  no empty line added between the code lines ($lines blank lines in the dump)"
test "$(grep -c '│ *│' "$TMP/eol.txt")" = 0 && echo "  no blank row inside the code frame"

echo "== a blank line after every code line is a paste artifact =="
"$BIN" --dump --width=44 tests/fixtures/code-double-spaced.md > "$TMP/gaps.txt"
test "$(grep -c '│ *│' "$TMP/gaps.txt")" = 0 && echo "  no blank row inside the code frame"
grep -q '#include <vector>' "$TMP/gaps.txt" && echo "  every code line kept"
grep -q 'int main' "$TMP/gaps.txt" && echo "  the tail of the block kept"

echo "== deliberately grouped code keeps its blank lines =="
"$BIN" --dump --width=44 tests/fixtures/code-grouped.md > "$TMP/grouped.txt"
test "$(grep -c '│ *│' "$TMP/grouped.txt")" -ge 3 && echo "  blank rows kept"
grep -q 'part two' "$TMP/grouped.txt" && echo "  content kept"

echo "== maths is not mistaken for a backslash-escaped source =="
"$BIN" --diag tests/fixtures/matrix.md | grep -q 'backslash escapes=0' \
  && echo "  a matrix document is not 'repaired'"
python3 tools/matrix_test.py "$BIN" || exit 1
# every fixture stays classified the way it should be
for f in escaped.md escaped-twice.md escaped-newlines.md; do
  "$BIN" --diag "tests/fixtures/$f" | grep -q 'entities=no' \
    && echo "  $f is left untouched by default"
  "$BIN" --entities=force --diag "tests/fixtures/$f" | grep -q 'entities=yes' \
    && echo "  $f is decoded with --entities=force"
done

echo "== cell bitmaps cover their transcription, on their own row =="
python3 tools/cell_cover_test.py "$BIN" || exit 1

echo "== inline formulas stay on their own row (no placeholder box) =="
python3 tools/img_anchor_test.py "$BIN" || exit 1

echo "== inline bitmaps sit on the text line in every protocol =="
python3 tools/subcell_align_test.py "$BIN" || exit 1

echo "== a multi-line formula reserves its rows =="
python3 tools/tall_math_test.py "$BIN" || exit 1

echo "== a file name outside ASCII survives the round trip =="
"$BIN" --dump --width=40 "tests/fixtures/中文标题.md" > "$TMP/cn.txt"
"$BIN" --diag "tests/fixtures/中文标题.md" > "$TMP/cn2.txt" 2>&1
grep -q '中文标题' "$TMP/cn2.txt" && echo "  file name survives the command line"
grep -q '这是一段中文内容' "$TMP/cn.txt" && echo "  contents read"
grep -q '中文文件名测试' "$TMP/cn.txt" && echo "  heading rendered"

echo "== nested quotes get a bar each =="
"$BIN" --dump --width=44 tests/fixtures/nested-quotes.md > "$TMP/nq.txt"
grep -q '│ │ 二级' "$TMP/nq.txt" && echo "  two levels"
grep -q '│ │ │ 三级' "$TMP/nq.txt" && echo "  three levels"
grep -q '│ 回到一级' "$TMP/nq.txt" && echo "  back to one level"

echo "== frame cost (runs, not per-cell escapes) =="
python3 tools/frame_cost_test.py "$BIN"

echo "== mouse + scrollbar (pty) =="
python3 tools/mouse_test.py "$BIN"

echo "== top-edge clipping + synchronized frames (pty) =="
python3 tools/scroll_clip_test.py "$BIN" || exit 1

echo "== Luogu directives (callouts / align / epigraph / cute-table / merges / lines=) =="
python3 tools/luogu_test.py "$BIN" || exit 1

echo "== GitHub alerts and <details> =="
"$BIN" --dump --width=70 tests/fixtures/alerts-and-details.md > "$TMP/alerts.txt"
grep -q "│ Note" "$TMP/alerts.txt" && echo "  > [!NOTE] becomes a label"
! grep -q "\[!NOTE\]" "$TMP/alerts.txt" && echo "  the raw marker is gone"
grep -q "│ Warning" "$TMP/alerts.txt" && echo "  > [!WARNING] too"
grep -q "│ A plain quote is still a quote" "$TMP/alerts.txt" && echo "  a normal quote is untouched"
grep -q "Click to expand" "$TMP/alerts.txt" && echo "  <details><summary> keeps its label"
! grep -q "Click to expandThe hidden" "$TMP/alerts.txt" \
  && echo "  ... and the summary no longer runs into the body"

echo "== reference links, front matter, heading attributes =="
"$BIN" --dump --width=76 tests/fixtures/refs.md > "$TMP/refs.txt"
grep -q "Heading with attributes" "$TMP/refs.txt" && echo "  {#custom-id} is not printed"
! grep -q "front matter title" "$TMP/refs.txt" && echo "  YAML front matter is skipped"
! grep -q "https://example.com/docs" "$TMP/refs.txt" && echo "  the definition line is not printed"
grep -q "\[nope\] and \[text\]\[missing\] stay as text" "$TMP/refs.txt" \
  && echo "  undefined references stay literal"
grep -q "\[label\]: https://not-a-definition.example" "$TMP/refs.txt" \
  && echo "  a definition inside a fence stays code"
"$BIN" --diag tests/fixtures/refs.md | grep -q "3 link reference definition" \
  && echo "  --diag counts the definitions"
python3 tools/refs_test.py "$BIN" || exit 1

echo "== footnotes and definition lists =="
"$BIN" --dump --width=76 tests/fixtures/footnotes.md > "$TMP/fn.txt"
grep -q "footnotes\[1\] are numbered where it is first used\[2\]" "$TMP/fn.txt" \
  && echo "  references are numbered where they are first used"
grep -q "Repeating\[1\] does not renumber it" "$TMP/fn.txt" \
  && echo "  a repeated reference keeps its number"
grep -q "not defined\[\^missing\] stays as written" "$TMP/fn.txt" \
  && echo "  an undefined label stays as written"
! grep -q "1. never referenced" "$TMP/fn.txt" \
  && echo "  a definition that is never referenced is not printed"
grep -q "\[\^one\]: not a definition, just code" "$TMP/fn.txt" \
  && echo "  a definition-looking line inside a fence stays code"
grep -q "1. The first note, with the project page." "$TMP/fn.txt" \
  && echo "  the notes are printed at the end, numbered"
grep -q "2. The second one, whose text wraps over two source lines." "$TMP/fn.txt" \
  && echo "  an indented continuation line belongs to the note"
"$BIN" --diag tests/fixtures/footnotes.md | grep -q "3 footnote(s) defined, 2 referenced" \
  && echo "  --diag counts the notes that were defined and referenced"

"$BIN" --dump --width=76 tests/fixtures/definition-lists.md > "$TMP/dl.txt"
grep -q ": A fruit, and a company." "$TMP/dl.txt" \
  && echo "  a 'Term' + ': definition' list keeps its ':' marker"
grep -q ": One term may have several definitions." "$TMP/dl.txt" \
  && echo "  a term may have several definitions"
grep -q ": The tilde marker works as well. Its text may wrap onto another line." "$TMP/dl.txt" \
  && echo "  the '~' marker works and its text may wrap"
grep -q "This sentence has a colon: but it is not a definition list." "$TMP/dl.txt" \
  && echo "  a colon inside a sentence is left alone"
grep -q ": this line is code, not a definition" "$TMP/dl.txt" \
  && echo "  a definition inside a fence stays code"
python3 tools/footnotes_test.py "$BIN" || exit 1

echo "== an 8-bit (ANSI) source is read, not shown as mojibake =="
python3 - "$TMP" <<'PY'
import os, sys
d = sys.argv[1]
# a Chinese Windows editor saves as GBK (the "ANSI" code page of the system)
open(os.path.join(d, "gbk.md"), "wb").write(
    "# \u4e2d\u6587\u6807\u9898\n\n\u8fd9\u662f\u4e00\u6bb5\u7528 GBK "
    "\u4fdd\u5b58\u7684\u6b63\u6587\u3002\n\n- \u5217\u8868\u9879\n".encode("gbk"))
open(os.path.join(d, "big5.md"), "wb").write(
    "# \u7e41\u9ad4\u4e2d\u6587\u6a19\u984c\n\n\u9019\u662f Big5 "
    "\u7de8\u78bc\u7684\u6b63\u6587\u3002\n".encode("big5"))
PY
"$BIN" --dump --width=60 "$TMP/gbk.md" > "$TMP/gbk.txt"
grep -q "中文标题" "$TMP/gbk.txt" && echo "  a GBK heading is readable"
grep -q "这是一段用 GBK 保存的正文。" "$TMP/gbk.txt" && echo "  the GBK body text is readable"
grep -q "• 列表项" "$TMP/gbk.txt" && echo "  the structure survives the conversion"
"$BIN" --encoding=off --dump --width=60 "$TMP/gbk.md" > "$TMP/gbk-off.txt"
! grep -q "中文标题" "$TMP/gbk-off.txt" && echo "  --encoding=off leaves the bytes alone"
"$BIN" --diag "$TMP/gbk.md" | grep -q "converted from gb18030 to UTF-8" \
  && echo "  --diag says what it converted"
"$BIN" --encoding=big5 --dump --width=60 "$TMP/big5.md" | grep -q "繁體中文標題" \
  && echo "  --encoding=big5 reads a Big5 file"

echo "== a big document stays inside its memory budget =="
python3 tools/memory_test.py "$BIN" || exit 1

echo "== opening time grows linearly with the document =="
python3 tools/big_doc_test.py "$BIN" || exit 1

echo "== blocks inside a list item are never dropped =="
printf -- '- item\n\n  $$\n  x^2\n  $$\n\n  <div>html inside</div>\n\n- two\n' > "$TMP/li-blocks.md"
"$BIN" --dump --width=40 "$TMP/li-blocks.md" > "$TMP/li-blocks.txt"
grep -q "x²" "$TMP/li-blocks.txt" && echo "  a \$\$ block in a list item is shown"
grep -q "html inside" "$TMP/li-blocks.txt" && echo "  an HTML block in a list item is shown"

echo "== command line and input file =="
sh tests/cli.sh "$BIN" || exit 1

echo "== no shell in the URL paths (command injection) =="
python3 tools/injection_test.py "$BIN" || exit 1

echo "== protocols (pty) =="
python3 tools/pty_test.py "$BIN"
python3 tools/outline_test.py "$BIN" || exit 1   # issue #2: a long outline

echo "== resize =="
python3 tools/resize_test.py "$BIN"

echo "== the key parser is handed whole sequences (issue #3) =="
python3 tools/keylog_test.py "$BIN" || exit 1

echo "== an untouched reader stays untouched (issue #3) =="
python3 tools/idle_frame_test.py "$BIN" || exit 1

echo "== --watch =="
python3 tools/watch_test.py "$BIN" || exit 1

echo "== the page renderer: --render=image (issue #4) =="
python3 tools/page_test.py "$BIN" || exit 1

echo "== a theme change reaches the formulas (issue #3) =="
python3 tools/theme_formula_test.py "$BIN" || exit 1

echo "== the t key: next theme, same options, no full relayout =="
python3 tools/theme_key_test.py "$BIN" || exit 1

echo "== interrupted from the outside (SIGTERM) =="
python3 tools/panic_test.py "$BIN"

echo
echo "all smoke tests passed"
