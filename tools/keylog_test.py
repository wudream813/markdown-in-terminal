#!/usr/bin/env python3
"""What mdt hands to its key parser, under a real console (issue #3).

The bug report was "pressing an arrow key or using the mouse in WezTerm exits
mdt silently with status 0".  A console-based reader can only tell the two
possible causes apart from the inside: either the terminal never delivered
anything, or what arrived was not a complete sequence.  MDT_KEY_LOG records the
decoded input, so this test asserts the properties that matter - whatever the
host did, the parser is given a *whole* ESC [ A (and a whole SGR mouse report),
never a lone ESC that would be read as "quit", and an idle reader waits instead
of reading in a loop (the same issue: a console that reports itself readable
with an empty buffer turned the event loop into a spin).

    python3 tools/keylog_test.py [EXE] [WINE] [WINEARG...]
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
LOG = os.path.join(os.environ.get("TMPDIR", "/tmp"), "mdt-keylog-test.txt")
WARMUP = 25.0 if WINE else 8.0   # a cold wine prefix is slow to draw its first frame
IDLE = 2.0

# Escape sequences, as regular expressions.  Test helper only: the reader has a
# real parser (src/term.cpp).  Wine's console inserts CR/LF inside long
# sequences and can swallow an OSC terminator, so the string variants accept
# either BEL or ST.
_ESC = re.compile(
    rb"\x1b\[[0-9;?<=>]*[ -/]*[@-~]"        # CSI
    rb"|\x1b\][^\x07\x1b]*(\x07|\x1b\\)?"   # OSC, BEL or ST
    rb"|\x1b[P^_X][^\x1b]*(\x1b\\)?"        # DCS / APC / PM / SOS
    rb"|\x1b[@-Z\\-~]"                      # other two-byte escapes
)


def plain(data: bytes) -> bytes:
    """What the terminal would show: escape sequences removed.

    The whitespace goes first: wine inserts CR/LF *inside* long sequences, and
    with those bytes still in place the sequences no longer match and their
    text survives.  Removing them also re-joins a status bar that a cursor move
    (or wine) broke in half, which is what the caller searches.
    """
    return _ESC.sub(b"", data.replace(b"\r", b"").replace(b"\n", b""))


def stop(pid, fd):
    for fn, arg in ((os.kill, (pid, signal.SIGKILL)), (os.waitpid, (pid, 0)), (os.close, (fd,))):
        try:
            fn(*arg)
        except OSError:
            pass


def count_reads():
    if not os.path.exists(LOG):
        return 0
    with open(LOG, encoding="utf-8", errors="replace") as f:
        return sum(1 for ln in f if ln.startswith("["))


def run():
    """Drive one session; returns (screen bytes, idle reads) or None."""
    env = dict(os.environ, MDT_LANG="en", MDT_KEY_LOG=LOG)
    argv = [WINE, EXE] if WINE else [EXE]
    if WINE:
        env["WINEDEBUG"] = "-all"
        env.setdefault("WINEPREFIX", os.path.expanduser("~/.wineprefix"))
    try:
        os.unlink(LOG)
    except FileNotFoundError:
        pass
    pid, fd = pty.fork()
    if pid == 0:
        os.execvpe(argv[0], argv + ["--gfx=none", "demo/demo.md"], env)
    out = b""

    def pump(seconds):
        nonlocal out
        end = time.time() + seconds
        while time.time() < end:
            sel, _, _ = select.select([fd], [], [], 0.2)
            if sel:
                try:
                    out += os.read(fd, 65536)
                except OSError:
                    return
    try:
        # raw mode is only entered after the first frame: wait for the status
        # bar before sending anything, or the keys land in the shell.
        pump(WARMUP)
        if b"for help" not in plain(out):
            print("   FAIL first frame never appeared")
            return None
        # An idle reader must not be reading in a loop.  A reader that waits
        # manages a handful of reads (or none); the spin this guards against
        # managed thousands a second.
        before = count_reads()
        time.sleep(IDLE)
        idle_reads = count_reads() - before
        for keys in (b"\x1b[A", b"\x1b[B", b"\x1b[<64;20;6M", b"\r"):
            os.write(fd, keys)
            time.sleep(0.5)
        os.write(fd, b"q")                 # and leave on purpose
        pump(2)
        return out, idle_reads
    finally:
        stop(pid, fd)


def main():
    result = run()
    if result is None:
        return 1
    out, idle_reads = result
    print(f"   mode: {'wine console' if WINE else 'posix pty'}, "
          f"{len(out)} bytes of screen output, {idle_reads} reads while idle")
    if not os.path.exists(LOG):
        print("   FAIL no key log was written")
        return 1
    text = open(LOG, encoding="utf-8", errors="replace").read()
    lines = [ln for ln in text.splitlines() if ln.startswith("[")]
    checks = []
    for want in ("1b [ A", "1b [ B", "0d"):
        got = [ln for ln in lines if want in ln]
        checks.append((f"arrow/enter reached the parser intact ({want})", bool(got)))
    # The wheel report: some Windows console layers eat the ESC that starts a
    # CSI sequence (the parser re-synthesises it for "[<" / "[M" - see
    # term.cpp), so the report is accepted with or without its leading ESC as
    # long as it arrives whole and in one piece.
    whole = [ln for ln in lines if "[ < 6 4 ; 2 0 ; 6 M" in ln]
    split = [ln for ln in lines if "[ < 6 4 ;" in ln and "6 M" not in ln]
    checks.append(("the wheel report reached the parser whole", bool(whole) and not split))
    # and nothing may have arrived as a bare ESC on its own line
    lone = [ln for ln in lines if ln.rstrip().endswith(": 1b")]
    checks.append(("no key was delivered as a lone ESC (the silent-quit path)", not lone))
    checks.append((f"an idle reader waits instead of spinning ({idle_reads} reads in "
                   f"{IDLE:.0f}s)", idle_reads <= 40))
    for name, ok in checks:
        print(("   ok   " if ok else "   FAIL ") + name)
    if lone:
        for ln in lone:
            print("         lone ESC: " + ln)
    if not all(ok for _, ok in checks):
        print("   key log:")
        for ln in lines:
            print("      " + ln)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
