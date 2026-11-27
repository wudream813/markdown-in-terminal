#!/usr/bin/env python3
"""A DA1 answer must set the sixel capability even when it arrives in pieces.

The probe used to stop reading at the first zero-byte read (the Windows record
reader returns 0 whenever its queued records produce no bytes) and dropped an
escape sequence that was still incomplete at that moment - and ConPTY is known
to split DA answers across reads (microsoft/terminal#19282).  Either way the
`;4` (sixel) token was lost and `--gfx=auto` reported "none" on terminals that
do support sixel.  The whole reply and the split reply must both work.

    python3 tools/da1_sixel_test.py ./build/mdt
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


def run_case(fragments, label):
    """fragments: list of (delay_after_da_query_seconds, bytes)."""
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm"  # no kitty hints: fall through to sixel
        os.environ.pop("KITTY_WINDOW_ID", None)
        os.environ.pop("TERM_PROGRAM", None)
        os.execvp(BIN, [BIN, "tests/fixtures/cell-math-pos.md"])
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", ROWS, COLS, 0, 0))
    out = b""
    t_query = None
    idx = 0
    end = time.time() + 6
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.05)
        if fd in r:
            try:
                out += os.read(fd, 65536)
            except OSError:
                break
        if t_query is None and b"\x1b[16t" in out:
            t_query = time.time()  # the probe burst starts here
        if t_query is not None and idx < len(fragments):
            delay, data = fragments[idx]
            if time.time() - t_query >= delay:
                os.write(fd, data)
                idx += 1
        if idx == len(fragments) and time.time() - t_query > 1.2:
            break
    try:
        os.close(fd)
    except OSError:
        pass
    try:
        os.kill(pid, 9)
        os.waitpid(pid, 0)
    except OSError:
        pass
    return out


# 1. the whole DA1 answer in one piece
out = run_case([(0.05, b"\x1b[?62;4;22c")], "whole")
if b"sixel" not in out:
    print("   FAIL a complete DA1 answer with ;4 did not select sixel")
    sys.exit(1)
print("   ok complete DA1 answer selects sixel")

# 2. the same answer split the way ConPTY splits it: the tail lands after the
#    original probe deadline and must still be waited for
out = run_case([(0.30, b"\x1b[?62;4;2"), (0.42, b"2c")], "split")
if b"sixel" not in out:
    print("   FAIL a fragmented DA1 answer was dropped - sixel went undetected")
    sys.exit(1)
print("   ok fragmented DA1 answer selects sixel")
