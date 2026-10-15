#!/usr/bin/env python3
"""What mdt hands to its key parser, under a real console (issue #3).

The bug report was "pressing an arrow key or using the mouse in WezTerm exits
mdt silently with status 0".  A console-based reader can only tell the two
possible causes apart from the inside: either the terminal never delivered
anything, or what arrived was not a complete sequence.  MDT_KEY_LOG records the
decoded input, so this test asserts the property that matters - whatever the
host did, the parser is given a *whole* ESC [ A (and a whole SGR mouse report),
never a lone ESC that would be read as "quit".

    python3 tools/keylog_test.py [EXE] [WINE] [WINEARG...]
"""
import os
import pty
import select
import signal
import sys
import time

EXE = sys.argv[1] if len(sys.argv) > 1 else "build/win/mdt.exe"
WINE = sys.argv[2] if len(sys.argv) > 2 else None
WINEDEBUG = "-all"
LOG = os.path.join(os.environ.get("TMPDIR", "/tmp"), "mdt-keylog-test.txt")


def run():
    env = dict(os.environ, MDT_LANG="en", MDT_KEY_LOG=LOG)
    argv = [WINE, EXE] if WINE else [EXE]
    if WINE:
        env["WINEDEBUG"] = WINEDEBUG
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
            r, _, _ = select.select([fd], [], [], 0.2)
            if r:
                try:
                    out += os.read(fd, 65536)
                except OSError:
                    return
    # raw mode is only entered after the first frame: wait for the status bar
    # before sending anything, or the keys are typed into the shell.
    pump(8)
    if b"for help" not in out:
        print("   FAIL first frame never appeared")
        return False
    os.write(fd, b"\x1b[A")            # arrow up
    time.sleep(0.5)
    os.write(fd, b"\x1b[B")            # arrow down
    time.sleep(0.5)
    os.write(fd, b"\x1b[<64;20;6M")    # wheel up (SGR)
    time.sleep(0.5)
    os.write(fd, b"\r")                # Enter
    time.sleep(0.5)
    os.write(fd, b"q")                 # and leave on purpose
    pump(2)
    try:
        os.kill(pid, signal.SIGKILL)
    except OSError:
        pass
    try:
        os.waitpid(pid, 0)
    except OSError:
        pass
    try:
        os.close(fd)
    except OSError:
        pass
    return out


def main():
    out = run()
    print(f"   mode: {'wine console' if WINE else 'posix pty'}, {len(out)} bytes of screen output")
    if not os.path.exists(LOG):
        print("   FAIL no key log was written")
        return 1
    text = open(LOG, encoding="utf-8", errors="replace").read()
    checks = []
    # one line per read: "console input: 1b [ A" (or "read: 1b [ A" on POSIX)
    lines = [ln for ln in text.splitlines() if ln.startswith("[")]
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
