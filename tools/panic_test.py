#!/usr/bin/env python3
"""Checks the emergency restore path: when mdt is interrupted from the outside
(SIGTERM on POSIX, Ctrl-Break / console close on Windows) it must leave the alt
screen, stop mouse reporting and give the terminal back.

    python3 tools/panic_test.py ./build/mdt                 # native
    python3 tools/panic_test.py build/win/mdt.exe wine64    # through wine

MDT_DEBUG_SIGNAL arms the interruption from inside the binary (see
src/platform.h) so the test does not depend on terminal windows or shells.
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

ESC = "\x1b"
# the full shutdown sequence Terminal::shutdown()/panic_restore() emit
REQUIRED = [
    ("alt screen left", ESC + "[?1049l"),
    ("mouse reporting off", ESC + "[?1000l"),
    ("SGR mouse off", ESC + "[?1006l"),
    ("cursor restored", ESC + "[?25h"),
    ("colours reset", ESC + "[0m"),
    ("synchronised updates off", ESC + "[?2026l"),
]


def run(argv, env_extra, timeout=20.0):
    env = dict(os.environ)
    env.update(env_extra)
    pid, fd = pty.fork()
    if pid == 0:
        os.execvpe(argv[0], argv, env)
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 90, 0, 0))
    baseline = termios.tcgetattr(fd)
    out = b""
    t0 = time.time()
    while time.time() - t0 < timeout:
        r, _, _ = select.select([fd], [], [], 0.3)
        if r:
            try:
                chunk = os.read(fd, 1 << 20)
            except OSError:
                break
            if not chunk:
                break
            out += chunk
        if b"\x1b[?1049l" in out:
            time.sleep(0.4)
            break
    # the pty line discipline reflects the slave termios: raw mode must be gone
    try:
        after = termios.tcgetattr(fd)
    except OSError:
        after = baseline
    try:
        os.close(fd)
    except OSError:
        pass
    try:
        _, status = os.waitpid(pid, 0)
    except ChildProcessError:
        status = 0
    return out.decode("utf-8", "replace"), status, baseline, after


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    exe = sys.argv[1]
    wine = sys.argv[2] if len(sys.argv) > 2 else None
    argv = [wine, exe, "--gfx=none", "demo/demo.md"] if wine else [exe, "--gfx=none", "demo/demo.md"]
    env = {"MDT_DEBUG_SIGNAL": "2500"}

    text, status, before, after = run(argv, env)
    # Wine's console re-renders the stream and may break an escape sequence
    # across lines; drop CR/LF before matching.
    flat = text.replace("\r", "").replace("\n", "")

    failed = []
    for name, seq in REQUIRED:
        ok = seq in flat
        print(("   ok   " if ok else "   FAIL ") + name)
        if not ok:
            failed.append(name)

    # lflag bits: canonical mode + echo must be back on
    lflag = after[3]
    restored = bool(lflag & termios.ICANON) and bool(lflag & termios.ECHO)
    print(("   ok   " if restored else "   FAIL ") +
          f"termios restored (ICANON={bool(lflag & termios.ICANON)}, ECHO={bool(lflag & termios.ECHO)})")
    if not restored:
        failed.append("termios not restored")

    code = status >> 8
    print(f"   exit code {code} (POSIX uses 128+signal, Windows 130 for Ctrl-Break)")
    if failed:
        print("\nrestore sequence incomplete:", ", ".join(failed))
        print("tail:", flat[-200:].encode("unicode_escape").decode())
        return 1
    print("   terminal handed back after an external interrupt")
    return 0


if __name__ == "__main__":
    sys.exit(main())
