#!/usr/bin/env python3
"""Footnotes ("[^label]" + "[^label]: text") in the terminal.

Before this the reference and the definition were both printed as ordinary
text: "[^one]" read out loud in the middle of a sentence and the definition
line floating around as prose.  Now the reference is a small [n] that follows
the text (first use gets 1) and the notes are printed as an ordered list at the
end of the document, so a link that only exists inside a note still has to be
reachable - which is what the 'o' check below is for.

    python3 tools/footnotes_test.py ./build/mdt
    python3 tools/footnotes_test.py build/win/mdt.exe wine64
"""
import fcntl
import os
import pty
import re
import select
import struct
import sys
import tempfile
import termios
import time

ANSI = re.compile(r"\x1b\[[0-9;?]*[a-zA-Z]|\x1b_G.*?\x1b\\|\x1b\]0;.*?\x07|\x1b\][^\x07]*\x07")


def run(argv, keys=(), cols=90, rows=40, settle=12.0, after=2.0):
    pid, fd = pty.fork()
    if pid == 0:
        env = dict(os.environ)
        env["TERM"] = "xterm-256color"
        os.execvpe(argv[0], argv, env)
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
    out = b""
    t0 = time.time()
    sent = False
    while time.time() - t0 < settle:
        r, _, _ = select.select([fd], [], [], 0.2)
        if r:
            try:
                chunk = os.read(fd, 1 << 20)
            except OSError:
                break
            if not chunk:
                break
            out += chunk
        if not sent and b"for help" in out:
            for k in keys:
                os.write(fd, k)
                time.sleep(0.4)
            sent = True
            t0 = time.time() - (settle - after)
    try:
        os.close(fd)
    except OSError:
        pass
    try:
        os.waitpid(pid, 0)
    except ChildProcessError:
        pass
    return out


def screen_text(raw):
    return ANSI.sub("", raw.decode("utf-8", "replace"))


def main():
    binary = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "./build/mdt")
    runner = sys.argv[2] if len(sys.argv) > 2 else None
    doc = os.path.abspath("tests/fixtures/footnotes.md")
    tmp = tempfile.mkdtemp(prefix="mdt-notes-")
    bindir = os.path.join(tmp, "bin")
    os.makedirs(bindir, exist_ok=True)
    log = os.path.join(tmp, "argv.log")
    stub = '#!/bin/sh\nfor a in "$@"; do printf \'%s\\n\' "$a" >> "$MDT_ARGV_LOG"; done\nexit 0\n'
    for name in ("xdg-open", "open"):
        path = os.path.join(bindir, name)
        with open(path, "w") as f:
            f.write(stub)
        os.chmod(path, 0o755)

    failures = []
    env_backup = os.environ.copy()
    os.environ["PATH"] = bindir + os.pathsep + os.environ.get("PATH", "")
    os.environ["MDT_ARGV_LOG"] = log
    argv = ([runner, binary] if runner else [binary]) + ["--gfx=none", doc]
    text = screen_text(run(argv))
    flat = " ".join(text.split())

    if "first used[2]" in flat and "footnotes[1] are numbered" in flat:
        print("   ok   the reference is a numbered marker, not the raw label")
    else:
        print("   FAIL the numbered markers are missing from the text")
        failures.append("no [1]/[2] markers on screen")

    if "[^missing] stays as written" in flat:
        print("   ok   an undefined label stays as written")
    else:
        failures.append("an undefined label was rewritten")

    if "The first note, with the project page." in flat and "2. The second one" in flat:
        print("   ok   the notes are listed at the end of the document")
    else:
        failures.append("the footnote text was not printed at the end")

    if "[^one]: The first note" not in flat:
        print("   ok   the definition line itself is not printed as prose")
    else:
        failures.append("the definition line was rendered as prose")

    # The only link in the document lives inside a footnote: `o` must find it.
    run(argv, keys=(b"o",))
    opened = []
    if os.path.exists(log):
        opened = [l for l in open(log).read().splitlines() if l.strip()]
    if "https://example.com/footnotes" in opened:
        print("   ok   a link inside a footnote is still reachable with 'o'")
    else:
        failures.append("the launcher got %r (expected the URL defined in the footnote)" % (opened,))

    for k, v in env_backup.items():
        os.environ[k] = v
    import shutil
    shutil.rmtree(tmp, ignore_errors=True)

    if failures:
        print("\n   FAIL:")
        for f in failures:
            print("     - " + f)
        return 1
    print("   footnote checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
