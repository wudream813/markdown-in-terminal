#!/usr/bin/env python3
"""An inline formula must be anchored on the nearest cell row, not the one above.

floor() (plus a cell-based "is it visible" test) used to decide that a formula
whose baseline sits a fraction of a pixel above the text baseline belongs to
the previous row: at the top of a document it was replaced by a placeholder box,
and inside a table it was drawn one row too high.

    python3 tools/img_anchor_test.py ./build/mdt [fixture]
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
DOC = sys.argv[2] if len(sys.argv) > 2 else "tests/fixtures/img-anchor.md"
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

text = buf.decode("utf-8", "replace")
if "\u25a1" in text:
    print("   FAIL a formula was replaced by the placeholder box (\u25a1)")
    sys.exit(1)
print("   no placeholder box")

places = []
for m in re.finditer(rb"\x1b\[(\d+);(\d+)H\x1b_Ga=p,i=\d+,p=1(?:,c=(\d+),r=(\d+))?", buf):
    row, col, c, r = m.groups()
    places.append((int(row), int(col), int(c or 0), int(r or 0)))
if not places:
    print("   FAIL no image was placed at all")
    sys.exit(1)
# the first document line is "$x^2$ 开头就有公式" -> the paragraph formula is on screen row 1
top = [p for p in places if p[0] == 1]
if not top:
    print(f"   FAIL the formula on the first line was placed on screen rows "
          f"{sorted(p[0] for p in places)} instead of 1")
    sys.exit(1)
print(f"   formula on the first line placed on row 1, x={top[0][1]} ({len(places)} placements)")
print("   ok   inline formulas are anchored on the nearest row")
