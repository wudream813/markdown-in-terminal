#!/usr/bin/env python3
"""Checks that a window resize is picked up and the document re-laid out.

mdt learns about resizes from SIGWINCH on POSIX and from polling the console
buffer (GetConsoleScreenBufferInfo) on Windows, so both paths go through the
same script:

    python3 tools/resize_test.py ./build/mdt
    MDT_RESIZE_SKIP=1 python3 tools/resize_test.py build/win/mdt.exe wine64

Wine's console API reports a fixed size and never follows pty resizes, so the
Windows run sets MDT_RESIZE_SKIP=1 and only checks the startup geometry.
"""
import fcntl
import os
import pty
import select
import signal
import struct
import sys
import time

TIOCSWINSZ = 0x5414  # same value on Linux and the BSDs


def set_size(fd, rows, cols):
    fcntl.ioctl(fd, TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))


def pump(fd, seconds, needle=None):
    """Reads for up to `seconds`; returns (new bytes, needle seen)."""
    buf = b""
    seen = False
    t0 = time.time()
    while time.time() - t0 < seconds:
        r, _, _ = select.select([fd], [], [], 0.2)
        if not r:
            if seen:
                return buf, True
            continue
        try:
            chunk = os.read(fd, 1 << 20)
        except OSError:
            break
        if not chunk:
            break
        buf += chunk
        if needle and needle in buf:
            seen = True
            break
    return buf, seen


def status_row(n):
    """The status bar is written at the first column of the last row."""
    return b"\x1b[%d;1H" % n


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    exe = sys.argv[1]
    wine = sys.argv[2] if len(sys.argv) > 2 else None
    skip_resize = os.environ.get("MDT_RESIZE_SKIP") == "1"
    argv = [wine, exe, "--gfx=none", "demo/demo.md"] if wine else [exe, "--gfx=none", "demo/demo.md"]

    pid, fd = pty.fork()
    if pid == 0:
        env = dict(os.environ)
        env["TERM"] = "xterm-256color"
        os.execvpe(argv[0], argv, env)
    set_size(fd, 24, 80)

    ok = True
    _, first = pump(fd, 25.0, status_row(24))
    print(("   ok   " if first else "   FAIL ") + "initial frame fills 80x24")
    ok &= first

    if skip_resize:
        print("   skip resize steps: this host reports a fixed console size (wine)")
    else:
        set_size(fd, 32, 120)
        if not wine:
            os.kill(pid, signal.SIGWINCH)
        _, seen = pump(fd, 6.0, status_row(32))
        print(("   ok   " if seen else "   FAIL ") + "re-rendered at 120x32 after growing")
        ok &= seen

        set_size(fd, 18, 60)
        if not wine:
            os.kill(pid, signal.SIGWINCH)
        _, seen = pump(fd, 6.0, status_row(18))
        print(("   ok   " if seen else "   FAIL ") + "re-rendered at 60x18 after shrinking")
        ok &= seen

    try:
        os.write(fd, b"q")
    except OSError:
        pass
    pump(fd, 1.0)
    try:
        os.close(fd)
    except OSError:
        pass
    try:
        os.kill(pid, signal.SIGTERM)
    except ProcessLookupError:
        pass
    try:
        os.waitpid(pid, 0)
    except ChildProcessError:
        pass
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
