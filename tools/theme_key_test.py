#!/usr/bin/env python3
"""The `t` key: the next theme, the same options, and no full relayout.

Three things that went wrong before 0.3.22:

  * the cycle always started at "dark", so `mdt --theme=light` went to
    "light" again on the first `t` (nothing seemed to happen);
  * the new theme was built from its name only, which dropped --panels and
    --no-syntax: after one `t` the code blocks lost their tinted panel;
  * every `t` laid the whole document out again (0.8 s on a 10 MB file); it
    now only redoes the blocks on screen (see DocView::recolour()).

The test drives the reader in a pty with `--panels`, presses `t` and checks
the status bar and the colours on the wire: the light theme's code panel
background (242,242,245) must be there after dark -> light.

    python3 tools/theme_key_test.py [BIN]
"""
import os
import pty
import re
import select
import struct
import sys
import tempfile
import time
import fcntl
import termios

BIN = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "./build/mdt")
SLOWDOWN = float(os.environ.get("MDT_TEST_SLOWDOWN", "1") or 1)
ESC = re.compile(rb"\x1b\[[0-9;?<=>]*[ -/]*[@-~]|\x1b\][^\x07\x1b]*(\x07|\x1b\\)?|\x1b[@-Z\\-~]")


def run(args, doc, keys):
    pid, fd = pty.fork()
    if pid == 0:
        os.environ.update(TERM="xterm-256color", MDT_LANG="en", COLORTERM="truecolor")
        os.execv(BIN, [BIN] + args + [doc])
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", 30, 100, 0, 0))
    chunks = []

    def drain(quiet, need=0, limit=20.0 * SLOWDOWN):
        got = b""
        t0 = time.time()
        while time.time() - t0 < limit:
            r, _, _ = select.select([fd], [], [], quiet)
            if not r:
                if len(got) >= need:
                    break
                continue
            try:
                d = os.read(fd, 1 << 16)
            except OSError:
                break
            if not d:
                break
            got += d
        return got

    chunks.append(drain(1.0 * SLOWDOWN, need=2000))
    for k in keys:
        os.write(fd, k)
        chunks.append(drain(0.6 * SLOWDOWN))
    os.write(fd, b"q")
    drain(0.5)
    os.waitpid(pid, 0)
    return chunks


def text(b):
    return re.sub(rb"\s+", b" ", ESC.sub(b"", b)).decode("utf-8", "replace")


def main():
    ok = True

    def check(cond, msg):
        nonlocal ok
        print(("   ok   " if cond else "   FAIL ") + msg)
        ok = ok and cond

    with tempfile.TemporaryDirectory() as d:
        doc = os.path.join(d, "t.md")
        with open(doc, "w") as f:
            f.write("# Themes\n\nSome text.\n\n```c\nint main(void) { return 0; }\n```\n")
        out = run(["--panels"], doc, [b"t"])
        check("theme: light" in text(out[1]), "dark -> light on the first t")
        check(b"48;2;242;242;245" in out[1],
              "--panels survives the theme change (the light code panel is drawn)")
        out = run(["--theme=light"], doc, [b"t"])
        check("theme: nord" in text(out[1]), "--theme=light: the first t goes on to nord")
    print("theme key ok" if ok else "theme key FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
