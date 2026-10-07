#!/usr/bin/env python3
"""Images must not be drawn over the UI.

Two rules are checked here:
  * with the help panel open no image is transmitted or placed at all (the
    non-kitty protocols cannot be erased cell by cell, so a formula left
    underneath the panel stayed on screen and covered it);
  * no image extends into the status bar.

    python3 tools/image_overlay_test.py ./build/mdt
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
COLS, ROWS = 80, 24
DOC = "demo/demo.md"

pid, fd = pty.fork()
if pid == 0:
    os.environ["TERM"] = "xterm-kitty"
    os.environ["KITTY_WINDOW_ID"] = "1"
    os.execvp(BIN, [BIN, "--gfx=kitty", DOC])
fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", ROWS, COLS, 0, 0))


def drain(seconds):
    out = b""
    end = time.time() + seconds
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.15)
        if not r:
            continue
        try:
            chunk = os.read(fd, 65536)
        except OSError:
            break
        if not chunk:
            break
        out += chunk
    return out


def placements(blob):
    """(row, cols, rows) for every image the terminal is asked to draw: the
    cursor jump that precedes the placement gives the cell, c/r give the size
    (0 when the terminal never reported its cell size - then the image is
    placed at its own pixel size)."""
    found = []
    for m in re.finditer(rb"\x1b\[(\d+);(\d+)H\x1b_Ga=p,i=\d+,p=1(?:,c=(\d+),r=(\d+))?", blob):
        row, _col, c, r = m.groups()
        found.append((int(row), int(c or 0), int(r or 0)))
    return found


doc_frames = drain(2.5)
os.write(fd, b"?")                      # open help
help_frames = drain(1.5)
os.write(fd, b"?")                      # close help
drain(0.5)
os.write(fd, b"G")                      # jump to the end of the document
end_frames = drain(1.5)
os.write(fd, b"q")
time.sleep(0.2)
try:
    os.kill(pid, 9)
except ProcessLookupError:
    pass
try:
    os.waitpid(pid, 0)
except ChildProcessError:
    pass

ok = True
placed_doc = placements(doc_frames)
print(f"   document: {len(placed_doc)} images placed")
if placements(help_frames):
    print(f"   FAIL {len(placements(help_frames))} images are drawn while the help panel is open")
    ok = False
else:
    print("   ok   no image while the help panel is open")
if re.search(rb"\x1b_Ga=d,d=a", help_frames):
    print("   ok   placements are deleted when the panel opens")

placed_end = placements(end_frames)
def bottom(p):
    # p = (row, cols, rows); rows == 0 means "placed at pixel size"
    return p[0] - 1 + max(1, p[2])


bad = [p for p in placed_end if bottom(p) > ROWS - 1]
if bad:
    print(f"   FAIL {len(bad)} image(s) reach into the status bar: {bad[:4]}")
    ok = False
elif placed_end:
    print(f"   ok   all {len(placed_end)} images stay inside the page")
else:
    print("   note: nothing to place at the end of the document")

sys.exit(0 if ok else 1)
