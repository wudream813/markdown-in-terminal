#!/usr/bin/env python3
"""A multi-line formula must not be drawn over the text that follows it.

Reported with `$\\begin{aligned}...\\end{aligned}$` (inline delimiters, three
aligned rows): the bitmap is several terminal rows tall, but the layout used to
reserve a single row for the line that carries it, so the formula covered the
next paragraph ("testtesttest 被多行公式直接遮挡").  The layout now reserves
Line::rows rows for a line whose inline bitmap spans more than one row, and the
bitmap is drawn inside exactly those rows.

This test renders a document with a three-row aligned block followed by two
paragraphs and checks the screenshot pixel rows: formula ink (the warm maths
colour) and prose ink (the neutral text colour) must never share a row, and
the prose that follows the formula must start below the formula's last row.

usage: tall_math_test.py BIN [RUNNER]
"""
import os
import struct
import subprocess
import sys
import zlib

BIN = sys.argv[1] if len(sys.argv) > 1 else "./build/mdt"
RUNNER = [sys.argv[2]] if len(sys.argv) > 2 else []

DOC = """$\\begin{aligned}
 (x-y)^2+4xy &= x^2-2xy+y^2+4xy \\\\
 &= x^2+2xy+y^2 \\\\
 &= (x+y)^2
\\end{aligned}$

testtesttest

后面还有文字也要露出来。
"""


def load_png(path):
    d = open(path, "rb").read()
    i = 8
    idat = b""
    while i < len(d):
        ln = struct.unpack(">I", d[i:i + 4])[0]
        tag = d[i + 4:i + 8]
        data = d[i + 8:i + 8 + ln]
        if tag == b"IHDR":
            w, h = struct.unpack(">II", data[0:8])
            ct = data[9]
        if tag == b"IDAT":
            idat += data
        i += 12 + ln
    bpp = {0: 1, 2: 3, 4: 2, 6: 4}[ct]
    stride = w * bpp
    raw = zlib.decompress(idat)
    rows = []
    prev = bytearray(stride)
    pos = 0
    for y in range(h):
        f = raw[pos]
        pos += 1
        line = bytearray(raw[pos:pos + stride])
        pos += stride
        if f == 1:
            for x in range(bpp, stride):
                line[x] = (line[x] + line[x - bpp]) & 255
        elif f == 2:
            for x in range(stride):
                line[x] = (line[x] + prev[x]) & 255
        elif f == 3:
            for x in range(stride):
                a = line[x - bpp] if x >= bpp else 0
                line[x] = (line[x] + ((a + prev[x]) >> 1)) & 255
        elif f == 4:
            for x in range(stride):
                a = line[x - bpp] if x >= bpp else 0
                b = prev[x]
                c = prev[x - bpp] if x >= bpp else 0
                pp = a + b - c
                pa, pb, pc = abs(pp - a), abs(pp - b), abs(pp - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 255
        rows.append(bytes(line))
        prev = line
    return w, h, bpp, rows


def main():
    d = ".tall_math.%d" % os.getpid()
    os.makedirs(d, exist_ok=True)
    try:
        doc = os.path.join(d, "cover.md")
        with open(doc, "w", encoding="utf-8") as f:
            f.write(DOC)
        png = os.path.join(d, "cover.png")
        r = subprocess.run(RUNNER + [BIN, "--screenshot=" + png, "--width=80",
                                     "--height=16", "--cell=10x20", doc],
                           capture_output=True)
        if r.returncode != 0:
            print("screenshot failed:", r.stderr.decode(errors="replace"))
            sys.exit(1)
        w, h, bpp, rows = load_png(png)
        cell = 20  # --cell=10x20
        warm_rows, neutral_rows = set(), set()
        for y in range(h - cell):          # skip the status bar row
            line = rows[y]
            warm = neutral = 0
            for x in range(w):
                p = line[x * bpp:x * bpp + 3]
                if sum(p) < 300:
                    continue
                if p[0] - p[2] > 24:       # warm maths colour
                    warm += 1
                elif abs(p[0] - p[1]) < 14 and abs(p[1] - p[2]) < 14:
                    neutral += 1           # neutral prose colour
            if warm > 3:
                warm_rows.add(y // cell)
            if neutral > 3:
                neutral_rows.add(y // cell)
        mixed = warm_rows & neutral_rows
        if mixed:
            print("FAIL: formula ink and prose ink share rows %s" % sorted(mixed))
            sys.exit(1)
        if not warm_rows:
            print("FAIL: no formula ink in the screenshot")
            sys.exit(1)
        last_warm = max(warm_rows)
        # the prose after the formula: the neutral rows below the formula
        after = [r0 for r0 in neutral_rows if r0 > min(warm_rows)]
        below = [r0 for r0 in after if r0 < last_warm]
        if below:
            print("FAIL: prose rows %s sit inside the formula block (ends row %d)"
                  % (below, last_warm))
            sys.exit(1)
        print("ok   a three-row formula reserves its rows (ink rows %s, prose below)"
              % sorted(warm_rows))
    finally:
        for name in os.listdir(d):
            os.remove(os.path.join(d, name))
        os.rmdir(d)


if __name__ == "__main__":
    main()
