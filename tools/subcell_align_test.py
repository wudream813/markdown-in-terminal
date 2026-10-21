#!/usr/bin/env python3
"""Inline bitmaps must sit on the text line in every graphics protocol.

A formula or an inline picture is drawn into a canvas whose top is a cell
boundary; the sub-cell offset is baked into the bitmap (see asset_for_math /
asset_for_image in src/render.cpp).  A terminal that cannot place an image at a
pixel offset inside a cell (iTerm2, sixel) therefore has to draw it on exactly
the same rows as kitty does with its sub-cell placement.

MDT_SNAP_SUBCELL=1 makes --screenshot composite images the "no sub-cell offset"
way (bitmap top on its anchor cell).  This test renders a document with inline
maths, a tall fraction, a square root and an inline picture twice - with and
without that variable - and requires the two screenshots to be identical.  If
a layout ever reintroduces a sub-cell offset, the two renders drift apart and
this test fails.

usage: subcell_align_test.py BIN [RUNNER]
"""
import os
import struct
import subprocess
import sys

BIN = sys.argv[1] if len(sys.argv) > 1 else "./build/mdt"
RUNNER = [sys.argv[2]] if len(sys.argv) > 2 else []

DOC = """# 对齐

题意：给定 $N$（$N \\le 10^{18}$），求出一对正整数 $X,Y$ 满足 $X^3-Y^3=N$，或报告无解。

枚举 $Y$，求是否有满足条件的 $X$，如果快要超时就报告无解。

分式行内 $\\frac{1}{2}$ 与根号 $\\sqrt{x}$ 也要对。

$$\\begin{aligned} a &= b + c \\\\ d &= e \\end{aligned}$$

行内小图：![](%s) 之后继续写。
"""

import binascii
import zlib


def _png(w, h, rgb):
    """a tiny real PNG, encoded here so the test needs no fixture on disk"""
    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + \
            struct.pack(">I", binascii.crc32(c) & 0xFFFFFFFF)
    raw = b"".join(b"\x00" + bytes(rgb) * w for _ in range(h))
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw))
            + chunk(b"IEND", b""))


PNG = _png(6, 6, (200, 60, 60))


def shot(doc_path, out_path, snap):
    env = dict(os.environ)
    env.pop("MDT_SNAP_SUBCELL", None)
    if snap:
        env["MDT_SNAP_SUBCELL"] = "1"
    r = subprocess.run(RUNNER + [BIN, "--screenshot=" + out_path, "--width=90",
                        "--height=16", "--cell=10x20", doc_path],
                       capture_output=True, env=env)
    if r.returncode != 0:
        print("screenshot failed:", r.stderr.decode(errors="replace"))
        sys.exit(1)


def main():
    d = ".subcell_align.%d" % os.getpid()
    os.makedirs(d, exist_ok=True)
    try:
        run(d)
    finally:
        for name in os.listdir(d):
            os.remove(os.path.join(d, name))
        os.rmdir(d)


def run(d):
        png = os.path.join(d, "red.png")
        with open(png, "wb") as f:
            f.write(PNG)
        doc = os.path.join(d, "align.md")
        with open(doc, "w", encoding="utf-8") as f:
            f.write(DOC % "red.png")   # relative to the document, as usual
        a = os.path.join(d, "a.png")
        b = os.path.join(d, "b.png")
        shot(doc, a, False)
        shot(doc, b, True)
        with open(a, "rb") as f:
            da = f.read()
        with open(b, "rb") as f:
            db = f.read()
        if da != db:
            print("FAIL: sub-cell offset leaked into the layout "
                  "(kitty render != no-subcell render): %s vs %s" % (a, b))
            sys.exit(1)
        print("ok: inline bitmaps are grid-aligned (%d bytes, both renders identical)" % len(da))


if __name__ == "__main__":
    main()
