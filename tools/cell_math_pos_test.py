#!/usr/bin/env python3
"""Cell math baked for one column must not be reused for another column.

The cache key of baked cell formulas missed the in-cell column offset, so two
formulas rendered in different columns but with the same cell width shared one
bitmap and were both drawn at the first formula's column.

    python3 tools/cell_math_pos_test.py ./build/mdt [fixture]
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

BIN = sys.argv[1] if len(sys.argv) > 1 else "./build/mdt"
DOC = sys.argv[2] if len(sys.argv) > 2 else "tests/fixtures/cell-math-pos.md"
COLS, ROWS = 80, 24
CELL_W, CELL_H = 9, 19

pid, fd = pty.fork()
if pid == 0:
    os.environ["TERM"] = "xterm-kitty"
    os.environ["KITTY_WINDOW_ID"] = "1"
    os.execvp(BIN, [BIN, "--gfx=kitty", DOC])
fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", ROWS, COLS, 0, 0))

buf = b""
answered = False
deadline = time.time() + 6
while time.time() < deadline:
    r, _, _ = select.select([fd], [], [], 0.2)
    if not r:
        continue
    try:
        chunk = os.read(fd, 65536)
    except OSError:
        break
    if not chunk:
        break
    buf += chunk
    if not answered and b"\x1b[16t" in buf:
        # cell size and window size reports the layout needs for baking
        os.write(fd, b"\x1b[6;%d;%dt" % (CELL_H, CELL_W))
        os.write(fd, b"\x1b[4;%d;%dt" % (ROWS * CELL_H, COLS * CELL_W))
        answered = True
    if b"a=p" in buf and b"a=t" in buf:
        break
try:
    os.write(fd, b"q")
except OSError:
    pass
time.sleep(0.2)
try:
    os.kill(pid, 9)
except ProcessLookupError:
    pass
try:
    os.waitpid(pid, 0)
except ChildProcessError:
    pass

places = []
for m in re.finditer(rb"\x1b\[(\d+);(\d+)H\x1b_Ga=p,i=\d+,p=1(?:,c=(\d+),r=(\d+))?",
                     buf):
    row, col, c, r = m.groups()
    places.append((int(row), int(col), int(c or 0), int(r or 0)))

body = [p for p in places if p[3] == 1]
cols_seen = sorted(set(p[1] for p in body))
if len(body) < 2:
    print(f"   FAIL only {len(places)} placement(s) captured, cannot judge")
    sys.exit(1)
if len(cols_seen) < 2:
    print(f"   FAIL one-row formulas were drawn at the same column(s) "
          f"{cols_seen} - the baked cache ignored the in-cell offset")
    sys.exit(1)
print(f"   ok one-row formulas drawn at distinct screen columns {cols_seen}")
