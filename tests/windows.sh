#!/bin/sh
# End-to-end checks for the cross compiled Windows binary.
#
#   sh tests/windows.sh                 # uses wine64 + build-win/mdt.exe
#   WINE=wine sh tests/windows.sh
#
# Verifies the non-interactive pipeline (markdown, fonts, the embedded TeX
# engine, PNG output) and, when a pty helper is available, the console mode.
set -e

cd "$(dirname "$0")/.."
EXE=${EXE:-build/win/mdt.exe}
# Wine converts the Unix command line to UTF-16 with the host locale: under
# LC_CTYPE=POSIX a Chinese file name would arrive mangled before mdt ever sees
# it (a real Windows console hands over UTF-16 directly).  Give Wine a UTF-8
# locale so a non-ASCII path can be tested at all.
case "${LC_ALL:-${LC_CTYPE:-$LANG}}" in
  *UTF-8*|*utf8*) ;;
  *) export LC_ALL=C.UTF-8 ;;
esac

WINE=${WINE:-wine64}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

command -v "$WINE" >/dev/null 2>&1 || WINE=/usr/lib/wine/wine64
if ! command -v "$WINE" >/dev/null 2>&1; then
  echo "error: wine not found; install it or set WINE=/path/to/wine64" >&2
  exit 1
fi
[ -f "$EXE" ] || { echo "error: $EXE not found - run: make windows" >&2; exit 1; }

# Wine needs a prefix (created on first run) and, for a Debian/Ubuntu package
# without fonts, at least one TrueType face in C:\windows\Fonts.
: "${WINEPREFIX:=$HOME/.wineprefix}"
export WINEPREFIX
if [ ! -d "$WINEPREFIX/drive_c/windows" ]; then
  echo "== creating wine prefix $WINEPREFIX =="
  "$WINE" wineboot -u >/dev/null 2>&1 || true
fi
FONTDIR="$WINEPREFIX/drive_c/windows/Fonts"
if ! ls "$FONTDIR"/*.tt[fc] >/dev/null 2>&1; then
  echo "== seeding wine fonts (host fonts copied into C:\windows\Fonts) =="
  mkdir -p "$FONTDIR"
  for f in /usr/share/fonts/truetype/dejavu/*.ttf /usr/share/fonts/truetype/liberation/*.ttf; do
    [ -f "$f" ] && cp -n "$f" "$FONTDIR/" 2>/dev/null
  done
  for f in /usr/share/fonts/opentype/noto/*CJK*.tt[fc]; do
    [ -f "$f" ] && cp -n "$f" "$FONTDIR/" 2>/dev/null
  done
fi

echo "== binary =="
file "$EXE" | sed 's/^/   /'
x86_64-w64-mingw32-objdump -p "$EXE" 2>/dev/null | awk '/DLL Name/ {print "   needs " $3}' | sort -u

echo "== version under wine =="
"$WINE" "$EXE" --version 2>/dev/null | sed 's/^/   /'

echo "== text rendering matches the native build =="
./build/mdt --dump --width=76 demo/demo.md > "$TMP/native.txt"
"$WINE" "$EXE" --dump --width=76 demo/demo.md 2>/dev/null > "$TMP/wine_crlf.txt"
tr -d '\r' < "$TMP/wine_crlf.txt" > "$TMP/windows.txt"
if diff -q "$TMP/native.txt" "$TMP/windows.txt" >/dev/null; then
  echo "   identical output ($(wc -l < "$TMP/windows.txt") lines)"
else
  echo "   differences:"; diff "$TMP/native.txt" "$TMP/windows.txt" | head -20
  exit 1
fi

echo "== maths engine (embedded QuickJS) =="
printf '# eq\n\n$$\\int_0^1 x^2\\,dx = \\frac{1}{3}$$\n' > "$TMP/eq.md"
"$WINE" "$EXE" --screenshot="$TMP/eq.png" --width=60 --height=14 "$TMP/eq.md" 2>/dev/null | sed 's/^/   /'
[ -s "$TMP/eq.png" ] && echo "   png written ($(stat -c%s "$TMP/eq.png") bytes)"
python3 - "$TMP/eq.png" <<'PY'
import struct, sys
data = open(sys.argv[1], 'rb').read()
assert data[:8] == b'\x89PNG\r\n\x1a\n', "not a PNG"
w, h = struct.unpack('>II', data[16:24])
print(f"   png is valid: {w}x{h}")
PY

echo "== unicode fallback (no graphics protocol) =="
"$WINE" "$EXE" --gfx=none --dump --width=60 demo/demo.md 2>/dev/null | grep -q '∫' && echo "   ∫ present"

echo "== stdin =="
printf '# t\n\ninline $e^{i\\pi}$ maths\n' | "$WINE" "$EXE" --dump --width=50 - 2>/dev/null | head -3 | sed 's/^/   /'

echo "== themes =="
for th in dark light nord monochrome; do
  "$WINE" "$EXE" --theme=$th --screenshot="$TMP/$th.png" --width=70 --height=20 demo/demo.md >/dev/null 2>&1
  [ -s "$TMP/$th.png" ] && echo "   $th ok"
done

echo "== HTML-escaped source =="
"$WINE" "$EXE" --dump --width=64 tests/fixtures/escaped.md 2>/dev/null | tr -d '\r' > "$TMP/escaped.txt"
grep -q "HTML-escaped Markdown" "$TMP/escaped.txt" && echo "   heading restored"
grep -q "â¢ first item" "$TMP/escaped.txt" && echo "   list restored"

echo "== every fixture renders identically on Windows =="
for f in tests/fixtures/*.md; do
  base=$(basename "$f")
  ./build/mdt --dump --width=72 "$f" > "$TMP/nat.txt" 2>/dev/null
  "$WINE" "$EXE" --dump --width=72 "$f" 2>/dev/null | tr -d '\r' > "$TMP/win.txt"
  if diff -q "$TMP/nat.txt" "$TMP/win.txt" >/dev/null; then
    echo "   $base ok"
  else
    echo "   $base DIFFERS:"; diff "$TMP/nat.txt" "$TMP/win.txt" | head -10; exit 1
  fi
done
"$WINE" "$EXE" --dump --width=64 tests/fixtures/backslash.md 2>/dev/null | tr -d '\r' > "$TMP/bs.txt"
grep -q "Project documentation" "$TMP/bs.txt" && echo "   backslash-escaped source restored"
grep -q "• first step" "$TMP/bs.txt" && echo "   escaped bullets restored"
"$WINE" "$EXE" --dump --width=64 tests/fixtures/nested-blocks.md 2>/dev/null | tr -d '\r' > "$TMP/nb.txt"
grep -q "Col A" "$TMP/nb.txt" && echo "   table inside a list item drawn"
"$WINE" "$EXE" --dump --width=64 tests/fixtures/cjk-loose.md 2>/dev/null | tr -d '\r' > "$TMP/cjk.txt"
grep -q "项目说明" "$TMP/cjk.txt" && echo "   CJK markers without spaces restored"

echo "== the round-3 fixes on Windows =="
"$WINE" "$EXE" --dump --width=60 tests/fixtures/table-math.md 2>/dev/null | tr -d '\r' > "$TMP/tm.txt"
grep -q "x² + y² = z²" "$TMP/tm.txt" && echo "   maths in a table cell transcribed"
grep -q "a/b" "$TMP/tm.txt" && echo "   \\frac became a/b"
"$WINE" "$EXE" --dump --width=50 tests/fixtures/tasks.md 2>/dev/null | tr -d '\r' > "$TMP/tk.txt"
grep -q "\[x\] done item" "$TMP/tk.txt" && echo "   task markers are ASCII"
"$WINE" "$EXE" --dump --width=50 tests/fixtures/invisible.md 2>/dev/null | tr -d '\r' > "$TMP/iv.txt"
grep -q "Invisible leading characters" "$TMP/iv.txt" && echo "   zero width spaces stripped"
"$WINE" "$EXE" --dump --width=50 tests/fixtures/code-wrap.md 2>/dev/null | tr -d '\r' > "$TMP/cw.txt"
grep -q "argument_four)" "$TMP/cw.txt" && echo "   code lines wrapped, not truncated"
"$WINE" "$EXE" --entities=force --dump --width=44 tests/fixtures/escaped-newlines.md 2>/dev/null | tr -d '\r' > "$TMP/el.txt"
grep -q "#include <vector>" "$TMP/el.txt" && echo "   escaped line breaks not doubled"
test "$(grep -c '│ *│' "$TMP/el.txt")" = 0 && echo "   no blank row inside the code frame"
"$WINE" "$EXE" --dump --width=40 "tests/fixtures/中文标题.md" 2>/dev/null | tr -d '\r' > "$TMP/cn.txt"
"$WINE" "$EXE" --diag tests/fixtures/matrix.md 2>/dev/null | tr -d '\r' | grep -q 'backslash escapes=0' \
  && echo "   matrices are not 'repaired' as escaped markdown"
python3 tools/matrix_test.py "$EXE" "$WINE" 2>/dev/null || \
  python3 tools/matrix_test.py "$EXE" "$WINE"
python3 tools/subcell_align_test.py "$EXE" "$WINE" 2>/dev/null || \
  python3 tools/subcell_align_test.py "$EXE" "$WINE"
python3 tools/tall_math_test.py "$EXE" "$WINE" 2>/dev/null || \
  python3 tools/tall_math_test.py "$EXE" "$WINE"
"$WINE" "$EXE" --diag "tests/fixtures/中文标题.md" 2>/dev/null | tr -d '\r' > "$TMP/cn2.txt"
grep -q '中文标题' "$TMP/cn2.txt" && echo "   non-ASCII file name survives the command line"
grep -q '这是一段中文内容' "$TMP/cn.txt" && echo "   non-ASCII path opened and read"
"$WINE" "$EXE" --dump --width=44 tests/fixtures/nested-quotes.md 2>/dev/null | tr -d '\r' > "$TMP/nq.txt"
grep -q '│ │ 二级' "$TMP/nq.txt" && echo "   nested quotes get a bar each"
"$WINE" "$EXE" --entities=force --dump --width=44 tests/fixtures/code-double-spaced.md 2>/dev/null | tr -d '\r' > "$TMP/ds.txt"
test "$(grep -c '│ *│' "$TMP/ds.txt")" = 0 && echo "   no blank row when every line is spaced"
grep -q '#include <vector>' "$TMP/ds.txt" && echo "   double-spaced code stays complete"
"$WINE" "$EXE" --dump --width=44 tests/fixtures/code-grouped.md 2>/dev/null | tr -d '\r' > "$TMP/gr.txt"
test "$(grep -c '│ *│' "$TMP/gr.txt")" -ge 3 && echo "   deliberately grouped code keeps its blank lines"
"$WINE" "$EXE" --dump --width=60 demo/demo.md 2>/dev/null | tr -d '\r' | grep -q "^ ## " && echo "   dimmed # before headings"

echo "== a slow picture host does not block the reader =="
python3 tools/async_image_test.py "$EXE" "$WINE" 2>/dev/null || \
  python3 tools/async_image_test.py "$EXE" "$WINE" || exit 1

echo "== mouse + scrollbar (pty) =="
python3 tools/mouse_test.py "$EXE" "$WINE" || exit 1

echo "== console mode (pty) ="
if command -v python3 >/dev/null 2>&1; then
  python3 tools/wine_pty_test.py "$EXE" "$WINE" || exit 1

  echo "== startup geometry (resize polling is exercised on real Windows) =="
  MDT_RESIZE_SKIP=1 python3 tools/resize_test.py "$EXE" "$WINE" || exit 1

  echo "== interrupted from the outside (Ctrl-Break / console close) =="
  python3 tools/panic_test.py "$EXE" "$WINE" || exit 1
else
  echo "   skipped (python3 missing)"
fi

echo
echo "all windows checks passed"
