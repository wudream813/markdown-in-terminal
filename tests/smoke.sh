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
"$BIN" --dump --width=64 tests/fixtures/escaped.md > "$TMP/escaped.txt"
grep -q "HTML-escaped Markdown" "$TMP/escaped.txt" && echo "  heading restored"
grep -q "• first item" "$TMP/escaped.txt" && echo "  list restored"
grep -q "┌" "$TMP/escaped.txt" && echo "  table restored"
grep -q "int x = 1" "$TMP/escaped.txt" && echo "  code block restored"
"$BIN" --entities=off --dump --width=64 tests/fixtures/escaped.md | grep -q '\*\*bold\*\*' && echo "  --entities=off leaves the syntax escaped"
"$BIN" --dump --width=64 tests/fixtures/escaped-twice.md | grep -q "Escaped twice" && echo "  double-escaped source decoded"
"$BIN" --dump --width=64 tests/fixtures/normal-entities.md | grep -q "AT&T and 5 < 7" && echo "  ordinary entities untouched"


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

echo "== frame cost (runs, not per-cell escapes) =="
python3 tools/frame_cost_test.py "$BIN"

echo "== mouse + scrollbar (pty) =="
python3 tools/mouse_test.py "$BIN"

echo "== protocols (pty) =="
python3 tools/pty_test.py

echo "== resize =="
python3 tools/resize_test.py "$BIN"

echo "== interrupted from the outside (SIGTERM) =="
python3 tools/panic_test.py "$BIN"

echo
echo "all smoke tests passed"
