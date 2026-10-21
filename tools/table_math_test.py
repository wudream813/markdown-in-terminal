#!/usr/bin/env python3
"""Formulas inside table cells must be drawn as bitmaps, not left as TeX.

A cell is a text grid, so the cell keeps a Unicode transcription (which is what
terminals without graphics show).  Where a graphics protocol is available the
transcription is covered by a typeset bitmap that is exactly as wide as the
transcription and one cell tall.

    python3 tools/table_math_test.py ./build/mdt [fixture]
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
DOC = sys.argv[2] if len(sys.argv) > 2 else "tests/fixtures/table-math.md"
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
deadline = time.time() + 4
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
for m in re.finditer(rb"\x1b\[(\d+);(\d+)H\x1b_Ga=p,i=\d+,p=1(?:,c=(\d+),r=(\d+))?(?:,X=(\d+),Y=(-?\d+))?",
                     buf):
    row, col, c, r, _x, _y = m.groups()
    places.append((int(row), int(col), int(c or 0), int(r or 0)))

if not places:
    print("   FAIL no image was placed: the formula in the cell is only text")
    sys.exit(1)
single_row = [p for p in places if p[3] == 1 and p[2] >= 2]
print(f"   {len(places)} placements, {len(single_row)} of them one row high "
      f"inside a cell: {[(p[1], p[2]) for p in single_row][:6]}")
if not single_row:
    print("   FAIL the formulas in the table were not placed as cell-sized bitmaps")
    sys.exit(1)
print("   ok   cell formulas are drawn as bitmaps")
