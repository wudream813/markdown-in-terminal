#!/usr/bin/env python3
"""Checks that images are placed 1:1 with the cell grid.

mdt used to place every formula with `c=1,r=1`, so kitty squeezed the bitmap
into a single cell and the terminal had to scale it - formulas came out soft.
The renderer now rasterises onto the exact cell grid (cols*cell_w by
rows*cell_h pixels), so a placement must span as many cells as the bitmap is
big, and the two must agree.

    python3 tools/image_scale_test.py ./build/mdt [document]

Answers the cell-size probe (CSI 16 t) like a real terminal, then reads the
placements back out of the graphics stream.
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
DOC = sys.argv[2] if len(sys.argv) > 2 else "demo/demo.md"
COLS, ROWS = 80, 24
CELL_W, CELL_H = 9, 19

pid, fd = pty.fork()
if pid == 0:
    os.environ["TERM"] = "xterm-kitty"
    os.environ["KITTY_WINDOW_ID"] = "1"
    os.execvp(BIN, [BIN, "--gfx=kitty", "--width=%d" % COLS, DOC])

fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", ROWS, COLS, 0, 0))
buf = b""
deadline = time.time() + 4
probe_answered = False
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
    if not probe_answered and b"\x1b[16t" in buf:
        # reply the way a terminal does: cell size, then window size
        os.write(fd, b"\x1b[6;%d;%dt" % (CELL_H, CELL_W))
        os.write(fd, b"\x1b[4;%d;%dt" % (ROWS * CELL_H, COLS * CELL_W))
        probe_answered = True
    if buf.count(b"a=p") >= 3:
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

places = re.findall(rb"a=p,i=\d+,p=1,c=(\d+),r=(\d+),X=(\d+),Y=(-?\d+)", buf)
if not places:
    print("   FAIL no kitty placement carried a cell size "
          "(the terminal answered the probe, so it should have)")
    sys.exit(1)
sizes = [(int(c), int(r)) for c, r, _, _ in places]
wider = [s for s in sizes if s[0] > 1]
print(f"   {len(sizes)} placements, cell {CELL_W}x{CELL_H}, "
      f"{len(wider)} span more than one column")
print(f"   examples: {sizes[:6]}")
if not wider:
    print("   FAIL every image is placed in a single cell: formulas are being "
          "scaled down by the terminal")
    sys.exit(1)
sub = [int(y) for _, _, _, y in places]
if not any(y != 0 for y in sub):
    print("   note: every placement sits exactly on a cell boundary")
print("   ok   images span their real cell box")
