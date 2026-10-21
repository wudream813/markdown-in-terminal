#!/usr/bin/env python3
"""Guards the cost of a frame: the renderer must send runs of text, not one
cursor jump and one SGR per cell.  A full-screen frame used to cost ~40 bytes
per cell; runs bring it under ~8.  Anything above 15 is a regression.

    python3 tools/frame_cost_test.py ./build/mdt [document]
"""
import fcntl
import os
import pty
import select
import struct
import sys
import termios
import time

BIN = sys.argv[1] if len(sys.argv) > 1 else "./build/mdt"
DOC = sys.argv[2] if len(sys.argv) > 2 else "demo/demo.md"
COLS, ROWS = 80, 24
BUDGET = 15.0  # bytes per cell for the first frame

pid, fd = pty.fork()
if pid == 0:
    os.environ["TERM"] = "xterm-256color"
    os.execvp(BIN, [BIN, "--gfx=none", DOC])

fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", ROWS, COLS, 0, 0))
buf = b""
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
    # the first frame ends with the synchronized-update terminator
    if b"\x1b[?2026l" in buf:
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

start = buf.find(b"\x1b[?2026h")
end = buf.find(b"\x1b[?2026l", start + 1)
if start < 0 or end < 0:
    print("   FAIL no frame in the output")
    sys.exit(1)
frame = buf[start:end]
cells = COLS * ROWS
per_cell = len(frame) / cells
pos = frame.count(b"\x1b[")
print(f"   frame {len(frame)} bytes for {cells} cells = {per_cell:.2f} bytes/cell "
      f"({pos} escape sequences)")
if per_cell > BUDGET:
    print(f"   FAIL above the {BUDGET:.0f} bytes/cell budget: the renderer is "
          f"back to per-cell escapes")
    sys.exit(1)
print("   ok   the frame is sent as runs")
