#!/usr/bin/env python3
"""A matrix must keep its rows.

`\\begin{pmatrix} 1 & 2 \\\\ 3 & 4 \\\\ 5 & 6 \\end{pmatrix}` used to collapse into a
single row: the backslash-escape repair counted the "\\\\" row separators as
escaped punctuation, decided the file was "backslash escaped" and then removed
one backslash of each pair.  The repair now leaves maths (and code) alone, so a
three-row matrix is drawn three rows tall.

    python3 tools/matrix_test.py ./build/mdt [fixture]

Runs --screenshot and measures the ink of the first formula, so it needs no
terminal at all.
"""
import struct
import subprocess
import sys
import tempfile
import zlib

BIN = sys.argv[1] if len(sys.argv) > 1 else "./build/mdt"
# optional runner ("wine") so the Windows binary can be measured too
RUNNER = [sys.argv[2]] if len(sys.argv) > 2 else []
DOC = sys.argv[3] if len(sys.argv) > 3 else "tests/fixtures/matrix.md"
CELL_W, CELL_H = 10, 20
COLS = 60


def read_png(path):
    d = open(path, "rb").read()
    pos, idat = 8, b""
    w = h = 0
    while pos < len(d):
        ln, typ = struct.unpack(">I4s", d[pos:pos + 8])
        pos += 8
        if typ == b"IHDR":
            w, h = struct.unpack(">II", d[pos:pos + 8])
        elif typ == b"IDAT":
            idat += d[pos:pos + ln]
        pos += ln + 4
    raw = zlib.decompress(idat)
    bpp, stride = 4, w * 4
    out, prev, i = bytearray(), bytearray(stride), 0
    for _ in range(h):
        f = raw[i]
        i += 1
        line = bytearray(raw[i:i + stride])
        i += stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if f == 1:
                line[x] = (line[x] + a) & 255
            elif f == 2:
                line[x] = (line[x] + b) & 255
            elif f == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                pp = a + b - c
                pa, pb, pc = abs(pp - a), abs(pp - b), abs(pp - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 255
        out += line
        prev = line
    return w, h, out


with tempfile.TemporaryDirectory() as tmp:
    png = tmp + "/matrix.png"
    r = subprocess.run(RUNNER + [BIN, "--screenshot=" + png, "--width=%d" % COLS, "--height=24",
                                 "--cell=%dx%d" % (CELL_W, CELL_H), DOC],
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if r.returncode != 0:
        print("   FAIL could not render:", r.stderr.decode("utf-8", "replace")[:200])
        sys.exit(1)
    w, h, px = read_png(png)

# rows that carry ink, ignoring the status bar at the bottom
rows = []
for y in range(CELL_H, h - CELL_H):
    for x in range(w):
        i = (y * w + x) * 4
        if px[i] > 120 and px[i + 1] > 120 and px[i + 2] > 120:
            rows.append(y)
            break
if not rows:
    print("   FAIL nothing was drawn")
    sys.exit(1)
# the first block of consecutive inked rows is the first formula
first = [rows[0]]
for y in rows[1:]:
    if y - first[-1] <= 2:
        first.append(y)
    else:
        break
height_cells = (first[-1] - first[0] + 1) / CELL_H
print(f"   the first formula is {first[-1] - first[0] + 1} px tall "
      f"({height_cells:.1f} cells of {CELL_H} px)")
if height_cells < 2.5:  # a collapsed matrix is ~1 cell, a two-row one ~2.2
    print("   FAIL a multi-row formula was drawn less than three cells tall - "
          "it collapsed into one row")
    sys.exit(1)
print("   ok   matrices keep their rows")
