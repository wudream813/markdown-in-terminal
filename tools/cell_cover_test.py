#!/usr/bin/env python3
"""A formula bitmap must sit on its cell row and cover its transcription.

Two failure modes this guards against (both seen in the wild):
  * the bitmap was shifted by a sub-cell offset, so it hung over the row above
    and left the Unicode transcription showing - the placement now carries Y=0;
  * terminals paint cell text above placed images, so a transcription left in
    the text layer shows through/around the bitmap - the cells the bitmap
    covers must be blank in the frame.

    python3 tools/cell_cover_test.py ./build/mdt [fixture]
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
CELL_W, CELL_H = 10, 20  # the cell size that made the offset visible

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

# ---- rebuild the screen grid from the escape stream -------------------------
grid = [[" "] * COLS for _ in range(ROWS)]
row = col = 0
i = 0
while i < len(text):
    ch = text[i]
    if ch == "\x1b":
        m = re.match(r"\x1b\[(\d+);(\d+)H", text[i:])
        if m:
            row, col = int(m.group(1)) - 1, int(m.group(2)) - 1
            i += m.end()
            continue
        m = re.match(r"\x1b\[[0-9;?]*[A-Za-z]", text[i:])
        if m:
            i += m.end()
            continue
        m = re.match(r"\x1b_G.*?\x1b\\", text[i:], re.S)
        if m:
            i += m.end()
            continue
        m = re.match(r"\x1b\].*?(\x07|\x1b\\)", text[i:], re.S)
        if m:
            i += m.end()
            continue
        i += 1
        continue
    if ch == "\n":
        row += 1
        col = 0
        i += 1
        continue
    if ch == "\r":
        col = 0
        i += 1
        continue
    if 0 <= row < ROWS and 0 <= col < COLS:
        grid[row][col] = ch
        col += 1
    i += 1

# ---- checks -----------------------------------------------------------------
places = []
for m in re.finditer(rb"\x1b\[(\d+);(\d+)H\x1b_Ga=p,i=\d+,p=1(?:,c=(\d+),r=(\d+))?"
                     rb"(?:,X=(\d+),Y=(\d+))?", buf):
    prow, pcol, c, r, x_off, y_off = m.groups()
    places.append((int(prow), int(pcol), int(c or 0), int(r or 0),
                   int(x_off or 0), int(y_off or 0)))
cells = [p for p in places if p[3] == 1 and p[2] >= 2]
if not cells:
    print("   FAIL no cell-sized formula bitmap was placed")
    sys.exit(1)
print(f"   {len(cells)} cell-sized placements")

bad_offset = [p for p in cells if p[5] != 0]
if bad_offset:
    rows = sorted(p[0] for p in bad_offset)
    print(f"   FAIL {len(bad_offset)} bitmaps are shifted sub-cell (Y != 0), rows {rows[:6]}")
    sys.exit(1)
print("   every bitmap is anchored on its own cell row (Y=0)")

shown = []
for prow, pcol, c, r, _x, _y in cells:
    for dx in range(c):
        yy, xx = prow - 1, pcol - 1 + dx
        if 0 <= yy < ROWS and 0 <= xx < COLS and grid[yy][xx] != " ":
            shown.append((prow, pcol, grid[yy][xx]))
if shown:
    print(f"   FAIL the transcription is still in the frame under {len(shown)} bitmap cells, "
          f"e.g. {shown[:6]}")
    sys.exit(1)
print("   the transcription is cleared from the text layer under every bitmap")
print("   ok   cell formulas cover their transcription on their own row")
