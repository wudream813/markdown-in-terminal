#!/usr/bin/env python3
"""A Windows console host rewrites long escape sequences - do not spray them.

Under a ConPTY (any Windows terminal since Windows 10 1809) the host parses the
child's output and re-renders it.  Sequences it does not understand are not
passed through untouched: they are *drawn*, wrapped at the console width.  This
is why WezTerm showed a document's formulas as a screenful of base64 instead of
pictures (issue #3).  Measured under Wine with this program:

    one SGR mouse report   -> 1 CR inserted
    a kitty APC            -> 2 CRs
    a sixel DCS (2674 B)   -> 64 CRs

So mdt only draws pictures on Windows when the terminal answered a probe
itself; a terminal that merely looks capable (TERM_PROGRAM=WezTerm, ...) gets
Unicode formulas instead.  --gfx= still forces a protocol.

    python3 tools/conpty_gfx_test.py [EXE] [WINE]
"""
import os
import pty
import re
import select
import signal
import sys
import time

EXE = sys.argv[1] if len(sys.argv) > 1 else "build/win/mdt.exe"
WINE = sys.argv[2] if len(sys.argv) > 2 else None
DOC = "tests/fixtures/theme-math.md"


def run(extra_args, env_extra):
    env = dict(os.environ, MDT_LANG="en", WINEDEBUG="-all")
    env.update(env_extra)
    argv = ([WINE, EXE] if WINE else [EXE]) + extra_args + ["--width=60", DOC]
    if WINE:
        env.setdefault("WINEPREFIX", os.path.expanduser("~/.wineprefix"))
    pid, fd = pty.fork()
    if pid == 0:
        os.execvpe(argv[0], argv, env)
    out = b""
    end = time.time() + 12
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.3)
        if r:
            try:
                out += os.read(fd, 262144)
            except OSError:
                break
    os.write(fd, b"q")
    time.sleep(0.6)
    try:
        os.kill(pid, signal.SIGKILL)
    except OSError:
        pass
    try:
        os.waitpid(pid, 0)
    except OSError:
        pass
    return out


def main():
    fails = []
    # 1. a terminal that only claims support must not get picture sequences
    out = run([], {"TERM_PROGRAM": "WezTerm", "TERM": "xterm-256color"})
    pictures = len(re.findall(rb"\x1b\]1337", out)) + len(re.findall(rb"a=t,f=100", out)) + \
        len(re.findall(rb"\x1bPq", out))
    print(f"   auto + TERM_PROGRAM=WezTerm: {pictures} picture sequences, {len(out)} bytes")
    if pictures:
        fails.append("auto sent picture sequences into a console host that rewrites them")
    if "∫" not in out.decode("utf-8", "replace") and "∫" not in out.decode("latin-1", "replace"):
        fails.append("auto did not fall back to Unicode formulas either")
    # 2. forcing a protocol still works (for hosts that do pass sequences through)
    forced = run(["--gfx=iterm2"], {})
    if not re.search(rb"\x1b\]1337;File=inline=1", forced):
        fails.append("--gfx=iterm2 no longer emits iTerm2 image sequences")
    else:
        print("   --gfx=iterm2: forced protocol still emitted")
    for f in fails:
        print("   FAIL " + f)
    if fails:
        return 1
    print("   ok   Windows graphics protocol policy holds")
    return 0


if __name__ == "__main__":
    sys.exit(main())
