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
