#!/usr/bin/env python3
"""Checks the mouse support: wheel scrolling (both encodings), the scrollbar
glyphs and clicking on the scrollbar to jump.

    python3 tools/mouse_test.py ./build/mdt
    python3 tools/mouse_test.py build/win/mdt.exe wine64
"""
import fcntl
import os
import pty
import re
import select
import struct
import sys
import termios
import time

TIOCSWINSZ = 0x5414
ANSI = re.compile(r"\x1b\[[0-9;?]*[a-zA-Z]|\x1b_G.*?\x1b\\|\x1b\]0;.*?\x07")


def run(argv, keys, cols=80, rows=24, settle=2.0, after=1.6, total=None):
    pid, fd = pty.fork()
    if pid == 0:
        env = dict(os.environ)
        env["TERM"] = "xterm-256color"
        os.execvpe(argv[0], argv, env)
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
    out = b""
    t0 = time.time()
    sent = False
    deadline = total if total else settle + after + 3.0
    while time.time() - t0 < deadline:
        r, _, _ = select.select([fd], [], [], 0.2)
        if r:
            try:
                chunk = os.read(fd, 1 << 20)
            except OSError:
                break
            if not chunk:
                break
            out += chunk
        if not sent and time.time() - t0 > settle:
            for k in keys:
                try:
                    os.write(fd, k)
                except OSError:
                    break
                time.sleep(0.2)
            sent = True
        if sent and time.time() - t0 > settle + after:
            break
    try:
        os.close(fd)
    except OSError:
        pass
    try:
        os.kill(pid, 15)
    except ProcessLookupError:
        pass
    try:
        os.waitpid(pid, 0)
    except ChildProcessError:
        pass
    return out


def status(out):
    txt = ANSI.sub("", out.decode("utf-8", "replace"))
    m = re.findall(r"(\d+)%", txt)
    return int(m[-1]) if m else -1


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    exe = sys.argv[1]
    wine = sys.argv[2] if len(sys.argv) > 2 else None
    args = ["--gfx=none", "demo/demo.md"]
    argv = [wine, exe] + args if wine else [exe] + args

    fails = []
    out = run(argv, [])
    bars = out.count("\u2588".encode()) > 0 and out.count("\u2502".encode()) > 0
    print(("   ok   " if bars else "   FAIL ") + "scrollbar drawn (thumb + track)")
    fails += [] if bars else ["scrollbar"]

    # wheel down, SGR encoding (what most terminals send after \x1b[?1006h)
    p1 = status(run(argv, [b"\x1b[<65;40;12M"] * 6))
    ok = p1 > 0
    print(("   ok   " if ok else "   FAIL ") + f"wheel down (SGR) scrolled to {p1}%")
    fails += [] if ok else ["wheel SGR"]

    # wheel down, X10 encoding (terminals without SGR mouse)
    p2 = status(run(argv, [b"\x1b[M\x61\x28\x0c"] * 6))
    ok = p2 > 0
    print(("   ok   " if ok else "   FAIL ") + f"wheel down (X10) scrolled to {p2}%")
    fails += [] if ok else ["wheel X10"]

    # click near the bottom of the scrollbar jumps forward
    p3 = status(run(argv, [b"\x1b[<0;80;23M"]))
    ok = p3 > 60
    print(("   ok   " if ok else "   FAIL ") + f"click at the scrollbar foot jumped to {p3}%")
    fails += [] if ok else ["scrollbar click"]

    # and back up near the top
    p4 = status(run(argv, [b"\x1b[<0;80;2M"]))
    ok = 0 <= p4 <= 20
    print(("   ok   " if ok else "   FAIL ") + f"click near the top jumped to {p4}%")
    fails += [] if ok else ["scrollbar click up"]

    if fails:
        print("\nmouse checks failed:", ", ".join(fails))
        return 1
    print("   mouse + scrollbar ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
