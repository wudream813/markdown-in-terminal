#!/usr/bin/env python3
"""Runs the cross compiled mdt.exe inside a pty under Wine to check that the
Win32 console path (raw mode, VT sequences, mouse, capability probing) works."""
import os
import pty
import re
import select
import struct
import sys
import fcntl
import termios
import time

ANSI = re.compile(r"\x1b\[[0-9;?]*[a-zA-Z]|\x1b_G.*?\x1b\\|\x1b\]0;.*?\x07")
# Wine's console re-renders our stream and happily breaks escape sequences across
# line wraps, so also strip "ESC up to the next letter" before looking at text.
LOOSE = re.compile(r"\x1b[^a-zA-Z]*[a-zA-Z]", re.S)
CTRL = re.compile(r"[\x00-\x08\x0b-\x1f\x7f]")


def run(exe, wine, keys, cols=100, rows=24, timeout=25.0):
    env = dict(os.environ)
    env.setdefault("WINEDEBUG", "-all")
    env["TERM"] = "xterm-256color"
    pid, fd = pty.fork()
    if pid == 0:
        os.execvpe(wine, [wine, exe, "--gfx=none", "demo/demo.md"], env)
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
    out = b""
    deadline = time.time() + timeout
    key_i = 0
    next_key = time.time() + 4.0
    while time.time() < deadline:
        r, _, _ = select.select([fd], [], [], 0.2)
        if r:
            try:
                chunk = os.read(fd, 1 << 20)
            except OSError:
                break
            if not chunk:
                break
            out += chunk
        if time.time() >= next_key and key_i < len(keys):
            os.write(fd, keys[key_i])
            key_i += 1
            next_key = time.time() + 0.6
        elif key_i >= len(keys) and time.time() > next_key:
            break
    try:
        os.close(fd)
    except OSError:
        pass
    try:
        os.waitpid(pid, 0)
    except ChildProcessError:
        pass
    return out


def main():
    exe = sys.argv[1] if len(sys.argv) > 1 else "build-win/mdt.exe"
    wine = sys.argv[2] if len(sys.argv) > 2 else "wine64"
    fails = []

    # scroll, open the outline, search, help, then quit
    keys = [b"j", b" ", b"\t", b"/math\r", b"n", b"?", b"\x1b", b"q"]
    out = run(exe, wine, keys)
    text = out.decode("utf-8", "replace")
    plain = ANSI.sub("", text)

    checks = [
        ("alt screen entered", "\x1b[?1049h" in text),
        ("mouse reporting enabled", "\x1b[?1006h" in text),
        ("capability probe sent", "\x1b[16t" in text or "Gi=31" in text),
        ("status bar rendered", "katex" in plain or "unicode" in plain),
        ("document title shown", "demo.md" in plain),
    ]
    # Wine's console rewrites the stream, so match on loose words rather than
    # on exact typography.
    loose = CTRL.sub("", LOOSE.sub("", text))
    words = ["Markdown", "graphics", "Inline", "formatting", "engine"]
    hits = sum(1 for w in words if w in loose)
    checks.append((f"document body rendered ({hits}/{len(words)} keywords)", hits >= 3))

    for name, ok in checks:
        print(("   ok   " if ok else "   FAIL ") + name)
        if not ok:
            fails.append(name)

    if fails:
        print("\nwine console checks failed:", ", ".join(fails))
        print("first 600 bytes:")
        print(text[:600].encode("unicode_escape").decode())
        sys.exit(1)
    print("   console mode ok on Windows")


if __name__ == "__main__":
    main()
