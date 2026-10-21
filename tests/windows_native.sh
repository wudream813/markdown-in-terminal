#!/bin/sh
# Checks for the Windows binary running on a real Windows machine.
#
#   sh tests/windows_native.sh                      # on windows-latest (CI)
#   EXE=build/win/mdt.exe RUN=wine64 sh tests/windows_native.sh   # dry run here
#
# tests/windows.sh is the other half: it runs the same binary under wine on
# Linux, where a POSIX pty makes the interactive reader testable.  This file is
# for the things only a real Windows can answer:
#
#   * the MinGW build runs without a compatibility layer (DLLs, console APIs),
#   * the fonts are the ones Windows ships, not Debian's copied into a prefix,
#   * the command line arrives as UTF-16, so a Chinese file name is a fair test,
#   * the path separators, the CRLF text mode and the ANSI code page are real.
#
# The interactive console tests stay in tests/windows.sh: Python on Windows has
# no pty module, and a pseudo-console would test ConPTY rather than a console.
set -e
cd "$(dirname "$0")/.."
EXE=${EXE:-build/win/mdt.exe}
RUN=${RUN:-}                      # optional runner, for a dry run on Linux
[ -f "$EXE" ] || { echo "error: $EXE not found - run: make windows" >&2; exit 1; }

# Wine (only used for a dry run on Linux) converts the command line with the
# host locale, and under LC_CTYPE=POSIX a Chinese file name arrives mangled.  On
# Windows the command line is UTF-16 from the start and this line does nothing.
case "${LC_ALL:-${LC_CTYPE:-$LANG}}" in
  *UTF-8*|*utf8*) ;;
  *) export LC_ALL=C.UTF-8 ;;
esac

TMP=build/native-test
rm -rf "$TMP"
mkdir -p "$TMP"

mdt() {                          # mdt <args...>
  if [ -n "$RUN" ]; then "$RUN" "$EXE" "$@"; else "$EXE" "$@"; fi
}
fail() { echo "   FAIL $1"; exit 1; }
ok()   { echo "   ok   $1"; }

echo "== binary =="
if command -v file >/dev/null 2>&1; then
  file "$EXE" | sed 's/^/   /'
fi

echo "== version =="
want="mdt $(cat VERSION)"
got=$(mdt --version | tr -d '\r')
[ "$got" = "$want" ] || fail "version is '$got', wanted '$want'"
ok "$got"

echo "== help lists the new option =="
mdt --help | tr -d '\r' > "$TMP/help.txt"
grep -q -- "--watch" "$TMP/help.txt" || fail "--watch is missing from --help"
grep -q -- "--no-watch" "$TMP/help.txt" || fail "--no-watch is missing from --help"
ok "--watch / --no-watch documented"

echo "== text rendering =="
mdt --dump --width=76 demo/demo.md | tr -d '\r' > "$TMP/demo.txt"
[ -s "$TMP/demo.txt" ] || fail "--dump printed nothing"
grep -q "# mdt" "$TMP/demo.txt" || fail "the heading is missing"
grep -q "│" "$TMP/demo.txt" || fail "the quote bar is missing (box drawing)"
grep -q "∫" "$TMP/demo.txt" || fail "the Unicode maths fallback is missing"
ok "headings, box drawing and maths survive ($(wc -l < "$TMP/demo.txt") lines)"

echo "== every fixture renders =="
for f in tests/fixtures/*.md; do
  mdt --dump --width=72 "$f" 2>/dev/null | tr -d '\r' > "$TMP/f.txt" || fail "$f: --dump failed"
  [ -s "$TMP/f.txt" ] || fail "$f: --dump printed nothing"
done
ok "$(ls tests/fixtures/*.md | wc -l) fixtures"

echo "== a UTF-16 command line carries a Chinese file name =="
mdt --dump --width=44 "tests/fixtures/中文标题.md" | tr -d '\r' > "$TMP/cn.txt"
grep -q "这是一段中文内容" "$TMP/cn.txt" || fail "the CJK fixture did not render"
mdt --diag "tests/fixtures/中文标题.md" | tr -d '\r' > "$TMP/cn-diag.txt"
grep -q "中文标题" "$TMP/cn-diag.txt" || fail "--diag lost the file name"
ok "path, contents and --diag all keep the non-ASCII name"

echo "== the fonts are the ones Windows ships =="
for th in dark light nord monochrome; do
  mdt --theme=$th --screenshot="$TMP/$th.png" --width=70 --height=20 demo/demo.md \
    >/dev/null 2>&1 || fail "--screenshot --theme=$th failed"
  [ -s "$TMP/$th.png" ] || fail "--screenshot --theme=$th wrote no file"
done
ok "a screenshot in four themes"
# the page renderer (--render=image) draws with the same fonts; its headings
# are twice the size, so the picture must differ from the text one
mdt --render=image --screenshot="$TMP/page.png" --width=70 --height=20 demo/demo.md \
  >/dev/null 2>&1 || fail "--render=image --screenshot failed"
[ -s "$TMP/page.png" ] || fail "--render=image --screenshot wrote no file"
cmp -s "$TMP/page.png" "$TMP/dark.png" && fail "--render=image changed nothing in the picture"
ok "--render=image: the page renderer's picture, headings enlarged"

if command -v python3 >/dev/null 2>&1; then
  python3 - "$TMP/dark.png" 70 <<'PY' || exit 1
import struct, sys
data = open(sys.argv[1], "rb").read()
if data[:8] != b"\x89PNG\r\n\x1a\n":
    sys.exit("   FAIL not a PNG")
w, h = struct.unpack(">II", data[16:24])
cols = int(sys.argv[2])
if w != cols * 9:                     # default cell is 9x19
    sys.exit(f"   FAIL png is {w}px wide, {cols} columns should be {cols * 9}")
print(f"   ok   png is valid: {w}x{h} for {cols} columns")
PY
else
  echo "   (skipped the PNG header check: no python3 on PATH)"
fi

echo "== stdin, the way a shell hands it over =="
printf '# t\n\ninline $e^{i\\pi}$ maths\n' | mdt --dump --width=50 - 2>/dev/null \
  | tr -d '\r' > "$TMP/stdin.txt" || fail "--dump from stdin failed"
grep -q "maths" "$TMP/stdin.txt" || fail "stdin did not reach the renderer"
ok "a pipe is read as a document"

echo "== bad input is refused, not guessed at =="
check_status() {  # check_status <wanted> <what> <args...>
  want_status=$1; what=$2; shift 2
  set +e
  mdt "$@" >/dev/null 2>&1
  got_status=$?
  set -e
  [ "$got_status" = "$want_status" ] || fail "$what: exit $got_status, wanted $want_status"
  ok "$what (exit $got_status)"
}
check_status 2 "--theme=bogus is refused" --dump --theme=bogus demo/demo.md
check_status 2 "--width=1000000 is refused (no OOM)" --dump --width=1000000 demo/demo.md
check_status 1 "a missing file is an error" --dump no-such-file-here.md
check_status 1 "a directory is an error, not an empty file" --dump .
check_status 2 "two files are an error" --dump a.md b.md
check_status 2 "--watch with --dump is refused" --watch --dump demo/demo.md
check_status 2 "--watch with stdin is refused" --watch -
check_status 0 "--list-caps runs without a terminal" --list-caps

echo "== the pixel-level maths checks, natively =="
if command -v python3 >/dev/null 2>&1; then
  # ${RUN:+"$RUN"}: pass the runner only when there is one, so the same line
  # works on Windows (no runner) and in a dry run under wine.
  python3 tools/matrix_test.py "$EXE" ${RUN:+"$RUN"} >/dev/null || fail "matrix_test"
  ok "matrices keep their rows"
  python3 tools/subcell_align_test.py "$EXE" ${RUN:+"$RUN"} >/dev/null || fail "subcell_align_test"
  ok "inline maths aligns with the text"
  python3 tools/tall_math_test.py "$EXE" ${RUN:+"$RUN"} >/dev/null || fail "tall_math_test"
  ok "tall maths reserves its rows"
else
  echo "   (skipped: no python3 on PATH)"
fi

rm -rf "$TMP"
echo "native windows checks passed"
