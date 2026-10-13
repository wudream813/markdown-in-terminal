#!/bin/sh
# mdt command-line checks: the arguments and the input file are validated
# instead of guessed at, and a closed stdout is not a crash.
#
#   sh tests/cli.sh            # uses ./build/mdt
#   sh tests/cli.sh build/win/mdt.exe wine64
#
# Before 0.3.8 `--theme=bogus` silently rendered with the default theme,
# `--width=abc` became 0 ("auto"), `--width=1000000` asked for a million-column
# framebuffer (OOM), a directory was "read" successfully and printed nothing
# (exit 0, indistinguishable from an empty file) and extra files on the command
# line were dropped without a word.
set -e
cd "$(dirname "$0")/.."
BIN=${1:-${BIN:-./build/mdt}}
RUN=${2:-}
case "$BIN" in /*) ;; *) BIN="$(pwd)/$BIN" ;; esac
DOC=demo/demo.md

run() {  # run <args...>; sets $status and $out
  if [ -n "$RUN" ]; then
    out=$($RUN "$BIN" "$@" 2>&1) && status=0 || status=$?
  else
    out=$("$BIN" "$@" 2>&1) && status=0 || status=$?
  fi
}

expect_status() {  # expect_status <wanted> <what> <args...>
  want=$1; what=$2; shift 2
  run "$@"
  if [ "$status" != "$want" ]; then
    echo "   FAIL $what: exit $status, wanted $want"
    echo "        ($out)"
    exit 1
  fi
  echo "   ok   $what (exit $status)"
}

expect_message() {  # expect_message <substring> <what> <args...>
  substr=$1; what=$2; shift 2
  run "$@"
  case "$out" in
    *"$substr"*) echo "   ok   $what" ;;
    *) echo "   FAIL $what: output was: $out"; exit 1 ;;
  esac
}

echo "== a bad value is reported, not ignored =="
for opt in "--theme=bogus" "--gfx=bogus" "--math=bogus" "--entities=bogus" \
           "--escapes=bogus" "--loose=bogus" "--encoding=bogus"; do
  expect_status 2 "$opt is refused" --dump $opt $DOC
done

echo "== sizes and cell geometry are measured, not taken on trust =="
expect_status 2 "--width=abc is refused" --dump --width=abc $DOC
expect_status 2 "--width=-5 is refused" --dump --width=-5 $DOC
expect_status 2 "--width=1000000 is refused (no OOM)" --dump --width=1000000 $DOC
expect_status 2 "--height=5000 is refused" --dump --height=5000 $DOC
expect_status 2 "--font-px=-10 is refused" --dump --font-px=-10 $DOC
expect_status 2 "--font-px=1000 is refused" --dump --font-px=1000 $DOC
expect_status 2 "--cell=abc is refused" --dump --cell=abc $DOC
expect_status 2 "--cell=0x0 is refused" --dump --cell=0x0 $DOC
expect_status 0 "0 means auto and a sane size is accepted" \
  --dump --width=120 --height=30 --font-px=0 --cell=9x19 $DOC

echo "== the input file is checked =="
expect_message "cannot read" "a missing file says so" --dump no-such-file-here.md
expect_status 1 "a missing file is an error" --dump no-such-file-here.md
expect_message "one file at a time" "two files are refused, not dropped" --dump a.md b.md
expect_status 2 "two files are an error" --dump a.md b.md
expect_message "alternatives" "--diag with --dump is refused" --diag --dump $DOC
expect_status 2 "two output modes are an error" --diag --dump $DOC
if [ -z "$RUN" ]; then  # a Windows exe under wine reaches the directory check too,
                        # but "." means something else to it - skip there
  expect_message "is a directory" "a directory says so" --dump .
  expect_status 1 "a directory is an error, not an empty file" --dump .
  expect_status 1 "and it does not abort with std::length_error" --dump .
fi

echo "== stdin still works =="
# $RUN is the wine runner when the Windows binary is under test.  This check
# used to call "$BIN" directly, so it tried to exec a PE file on Linux and
# failed for a reason that had nothing to do with stdin.
if printf '# hello\n\n$a^2+b^2=c^2$\n' | $RUN "$BIN" --dump --width=40 - >/dev/null 2>&1; then
  echo "   ok   # a document on stdin renders"
else
  echo "   FAIL reading from stdin broke"; exit 1
fi

echo "== a settings file, and NO_COLOR =="
if [ -n "$RUN" ]; then
  echo "   (skipped under wine: the config path would have to be a Windows path)"
else
  CFG_DIR=$(mktemp -d)
  trap 'rm -rf "$CFG_DIR"' EXIT
  printf '# mdt settings\ntheme = nord\npanels = 1\n' > "$CFG_DIR/config"
  if MDT_CONFIG="$CFG_DIR/config" "$BIN" --diag "$DOC" 2>/dev/null \
       | grep -q "theme           : nord"; then
    echo "   ok   the config file sets the theme"
  else
    echo "   FAIL the config file was not read"; exit 1
  fi
  if MDT_CONFIG="$CFG_DIR/config" "$BIN" --theme=light --diag "$DOC" 2>/dev/null \
       | grep -q "theme           : light"; then
    echo "   ok   the command line overrides the file"
  else
    echo "   FAIL --theme did not override the config"; exit 1
  fi
  if NO_COLOR=1 "$BIN" --diag "$DOC" 2>/dev/null | grep -q "theme           : monochrome"; then
    echo "   ok   NO_COLOR=1 asks for no colour"
  else
    echo "   FAIL NO_COLOR was ignored"; exit 1
  fi
  if NO_COLOR=1 "$BIN" --theme=nord --diag "$DOC" 2>/dev/null \
       | grep -q "theme           : nord"; then
    echo "   ok   an explicit --theme still wins over NO_COLOR"
  else
    echo "   FAIL NO_COLOR overrode an explicit theme"; exit 1
  fi
  printf 'bogus_setting = 1\n' > "$CFG_DIR/bad"
  if MDT_CONFIG="$CFG_DIR/bad" "$BIN" --dump --width=40 "$DOC" 2>&1 >/dev/null \
       | grep -q "unknown setting"; then
    echo "   ok   a typo in the config file is reported"
  else
    echo "   FAIL a bad config key was swallowed"; exit 1
  fi
  rm -rf "$CFG_DIR"
fi

echo "== a closed stdout is quiet, not a signal death =="
if [ -n "$RUN" ]; then
  echo "   (skipped under wine: this one needs a POSIX pipe)"
else
  python3 - "$BIN" <<'PY'
import os, subprocess, sys
binary = sys.argv[1]
r, w = os.pipe()
p = subprocess.Popen([binary, "--dump", "--width=200", "README.md"],
                     stdout=w, stderr=subprocess.DEVNULL)
os.close(w)
os.read(r, 512)   # the consumer takes a little...
os.close(r)       # ...and goes away: the next write is EPIPE
rc = p.wait()
if rc == 0:
    print("   ok   --dump into a closed pipe exits 0 (was: killed by SIGPIPE, 141)")
else:
    print("   FAIL --dump into a closed pipe exited %d (141 = SIGPIPE)" % rc)
    sys.exit(1)
PY
fi

echo "cli checks passed"
