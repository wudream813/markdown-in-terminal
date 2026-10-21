#!/usr/bin/env python3
"""Opening a document twice as big must take about twice as long, not four times.

0.3.20 found a quadratic step in the parser: every Luogu leaf directive
("::cute-table{tuack}" above a table) was erased from the block list where it
stood, which moved every block behind it.  An 11 MB file with 1,500 of them
took 12 s to parse; 400 copies of the fixtures took 3 s where 200 took 0.5 s.

This test writes a Luogu-flavoured document at two sizes at run time (nothing
big is committed), times --dump on both and checks the ratio.  Absolute times
depend on the machine, the ratio does not: linear work gives ~2, the old code
gave ~4 and more.  It also checks an absolute ceiling that is generous enough
for a slow CI runner but far below what the quadratic version needed.

    python3 tools/big_doc_test.py ./build/mdt

MDT_TEST_SLOWDOWN (an instrumented ASan/UBSan build, or the arm64 artefact
under qemu on a CI runner, where the 6 MB document took 532 s) makes the
documents four times smaller, times each one once and drops the absolute
ceiling - emulation speed says nothing about mdt.  The ratio test still
catches the old parser there: x9 for four times the size.
"""
import os
import subprocess
import sys
import tempfile
import time

BIN = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "./build/mdt")
SLOWDOWN = float(os.environ.get("MDT_TEST_SLOWDOWN", "1") or 1)


def make_document(path, sections):
    part = []
    for n in range(sections):
        part.append(
            f"## Problem {n}\n\n"
            ":::info[Hint {n}]\n"
            "Read the statement twice, then write the code once.\n"
            ":::\n\n"
            "::cute-table{tuack}\n\n"
            "| n | answer | note |\n|:-:|:-:|:--|\n"
            f"| {n} | {n * n} | squares |\n| ^ | {n + 1} | merged |\n\n"
            "Some prose with **bold**, `code` and a link to [the docs](https://example.com) "
            "so that the inline parser has work to do as well.\n\n"
            "::unknown-leaf\n\n"
            "```\nint main() { return 0; }\n```\n\n")
    with open(path, "w", encoding="utf-8") as f:
        f.write("".join(part))
    return os.path.getsize(path)


def best_time(path, runs=3):
    best = None
    for _ in range(runs):
        t = time.monotonic()
        r = subprocess.run([BIN, "--dump", "--width=100", path], stdout=subprocess.DEVNULL,
                           stderr=subprocess.PIPE, env=dict(os.environ, MDT_LANG="en"))
        dt = time.monotonic() - t
        if r.returncode != 0:
            print(f"FAIL: mdt --dump exited {r.returncode}: {r.stderr.decode(errors='replace')}")
            sys.exit(1)
        best = dt if best is None else min(best, dt)
    return best


def main():
    with tempfile.TemporaryDirectory() as d:
        small = os.path.join(d, "small.md")
        big = os.path.join(d, "big.md")
        slow = SLOWDOWN > 1
        s_bytes = make_document(small, 1000 if slow else 4000)
        b_bytes = make_document(big, 4000 if slow else 16000)
        runs = 1 if slow else 3
        t_small = best_time(small, runs)
        t_big = best_time(big, runs)
        ratio = t_big / max(t_small, 1e-3)
        print(f"  {s_bytes / 1e6:.1f} MB: {t_small:.2f} s, {b_bytes / 1e6:.1f} MB: {t_big:.2f} s "
              f"(x{b_bytes / s_bytes:.0f} the size, x{ratio:.1f} the time)")
        # 4x the size: linear is 3-4x (start-up costs make the small run look
        # slower); the quadratic parser was x16.5 at these sizes, x9 at the
        # small ones.
        if ratio > 6.5:
            print("FAIL: the time grows much faster than the document (quadratic step?)")
            return 1
        ceiling = 15.0
        if not slow and t_big > ceiling:
            print(f"FAIL: {b_bytes / 1e6:.1f} MB took {t_big:.1f} s (ceiling {ceiling:.0f} s)")
            return 1
        print("  ok: opening time grows with the size of the document, not its square")
    return 0


if __name__ == "__main__":
    sys.exit(main())
