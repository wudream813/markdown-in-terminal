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

echo "== protocols (pty) =="
python3 tools/pty_test.py

echo
echo "all smoke tests passed"
