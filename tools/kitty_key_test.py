#!/usr/bin/env python3
"""Esc must work where the terminal honours the kitty keyboard protocol.

mdt pushes `ESC[>1u` at startup ("best effort").  Terminals that implement the
protocol (kitty, ghostty, Windows Terminal 1.25+, ...) then encode Esc as
`CSI 27 u`, and mdt's key table only mapped `CSI 13 u` (Enter) - every other
CSI u code was dropped, so Esc died on those terminals while arrows and Enter
kept working.  mdt must decode the protocol codes AND pop the flags on exit
(`ESC[<1u`), or the shell after mdt loses Esc too.

    python3 tools/kitty_key_test.py ./build/mdt
"""
import os
import pty
import select
import struct
import sys
import fcntl
import termios
import time

BIN = sys.argv[1] if len(sys.argv) > 1 else "./build/mdt"
COLS, ROWS = 80, 24


def run_case(key_bytes, label):
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-kitty"
        os.environ["KITTY_WINDOW_ID"] = "1"
        os.execvp(BIN, [BIN, "tests/fixtures/multi-image.md"])
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", ROWS, COLS, 0, 0))
    out = b""
    end = time.time() + 4
    sent = False
    exited = False
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.1)
        if fd in r:
            try:
                out += os.read(fd, 65536)
            except OSError:
                exited = True
                break
        if not sent and (b"multi" in out or b"images" in out or b"two" in out):
            os.write(fd, key_bytes)
            sent = True
        if sent:
            p, _ = os.waitpid(pid, os.WNOHANG)
            if p == pid:
                exited = True
                break
    try:
        os.close(fd)
    except OSError:
        pass
    if not exited:
        try:
            os.kill(pid, 9)
            os.waitpid(pid, 0)
        except OSError:
            pass
    return exited, out


# 1. CSI 27 u (kitty-protocol Esc) must act as Esc: quit at the top level
exited, out = run_case(b"\x1b[27u", "csi-27u")
if not exited:
    print("   FAIL CSI 27 u did not quit - Esc is dead where the terminal "
          "encodes it (kitty / Windows Terminal 1.25+)")
    sys.exit(1)
if b"\x1b[<1u" not in out:
    print("   FAIL the kitty keyboard flags were not popped on exit "
          "(ESC[<1u missing) - Esc stays CSI 27 u in the shell afterwards")
    sys.exit(1)
print("   ok CSI 27 u quits and the keyboard flags are popped on exit")

# 2. a bare ESC must still quit as well (legacy terminals)
exited, _ = run_case(b"\x1b", "bare-esc")
if not exited:
    print("   FAIL a bare ESC no longer quits")
    sys.exit(1)
print("   ok bare ESC still quits")
