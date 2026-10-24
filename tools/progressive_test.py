#!/usr/bin/env python3
"""Progressive layout: the reader lays out the first screen of a big document
first and the rest while it waits for keys (0.3.23).  Everything that needs a
row further down has to lay out up to it first, or it lands in the wrong place.

The test writes a document of ~50,000 blocks at run time and drives the
reader in a pty, pressing keys right after the first frame - while most of
the document is not laid out yet:

  * G shows the last line of the document (scroll_end lays out the rest);
  * g goes back to the top;
  * a search for a heading near the end jumps to it (row_of_block);
  * a resize right after the first frame, then G: still the real end;
  * --watch: a reload keeps the reader where they were (the anchor block is
    laid out again before the view scrolls to it).

    python3 tools/progressive_test.py [BIN]
"""
import fcntl
import os
import pty
import re
import select
import signal
import struct
import sys
import tempfile
import termios
import time

BIN = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "./build/mdt")
SLOWDOWN = float(os.environ.get("MDT_TEST_SLOWDOWN", "1") or 1)
ESC = re.compile(rb"\x1b\[[0-9;?<=>]*[ -/]*[@-~]|\x1b\][^\x07\x1b]*(\x07|\x1b\\)?|\x1b[@-Z\\-~]")
SECTIONS = 25000


def text(b):
    return re.sub(rb"\s+", b" ", ESC.sub(b"", b)).decode("utf-8", "replace")


class Reader:
    def __init__(self, args, doc, rows=30, cols=100):
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            os.environ.update(TERM="xterm-256color", MDT_LANG="en", COLORTERM="truecolor")
            os.execv(BIN, [BIN] + args + [doc])
        self.size(rows, cols, signal_it=False)

    def size(self, rows, cols, signal_it=True):
        fcntl.ioctl(self.fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
        if signal_it:
            os.kill(self.pid, signal.SIGWINCH)

    def drain(self, quiet, need=0, limit=None, until=None):
        limit = limit or 30.0 * SLOWDOWN
        got = b""
        t0 = time.time()
        while time.time() - t0 < limit:
            r, _, _ = select.select([self.fd], [], [], quiet)
            if not r:
                # waiting for something specific: a slow (instrumented) build
                # may stay silent for seconds while it lays out the rest
                if len(got) >= need and not until:
                    break
                continue
            try:
                d = os.read(self.fd, 1 << 16)
            except OSError:
                break
            if not d:
                break
            got += d
            if until and until in text(got):
                # the rest of this frame, then stop
                got += self.drain(0.2, limit=2.0)
                break
        return got

    def first_frame(self):
        # just the first frame: short quiet time, so the keys come in while the
        # rest of the document is still being laid out
        return self.drain(0.15, need=2000)

    def key(self, k, until=None, quiet=0.6):
        os.write(self.fd, k)
        return self.drain(quiet * SLOWDOWN, until=until)

    def quit(self):
        os.write(self.fd, b"q")
        self.drain(0.5, limit=5.0)
        os.waitpid(self.pid, 0)


def write_doc(path, extra=""):
    with open(path, "w") as f:
        for i in range(SECTIONS):
            f.write(f"## Part {i}\n\nParagraph number {i}: some words to wrap and to lay out, "
                    f"and a little more so that it takes a second line now and then.\n\n")
        f.write("## The end\n\nZZEND-MARKER is the last line.\n" + extra)


def main():
    ok = True

    def check(cond, msg):
        nonlocal ok
        print(("   ok   " if cond else "   FAIL ") + msg)
        ok = ok and cond

    with tempfile.TemporaryDirectory() as d:
        doc = os.path.join(d, "big.md")
        write_doc(doc)

        r = Reader([], doc)
        first = text(r.first_frame())
        check("Part 0" in first and "ZZEND-MARKER" not in first, "first frame shows the top")
        out = text(r.key(b"G", until="ZZEND-MARKER"))
        check("ZZEND-MARKER" in out, "G right away: the real end of the document")
        out = text(r.key(b"g", until="Part 1 "))
        check("Part 0" in out, "g: back at the top")
        out = text(r.key(b"/Part 24990\r", until="match"))
        check("Part 24990" in out and "1 match" in out, "search near the end jumps there")
        r.quit()

        r = Reader([], doc)
        r.first_frame()
        r.size(24, 80)
        r.drain(0.1, limit=1.0)
        out = text(r.key(b"G", until="ZZEND-MARKER"))
        check("ZZEND-MARKER" in out, "resize right after the first frame, then G: the end")
        r.quit()

        r = Reader(["--watch"], doc)
        r.first_frame()
        out = text(r.key(b"/Part 12000\r", until="match"))
        check("Part 12000" in out, "search jumps to the middle")
        r.drain(0.5, limit=3.0)
        with open(doc, "a") as f:
            f.write("\nOne more line at the end.\n")
        out = text(r.drain(1.5 * SLOWDOWN, until="reloaded", limit=15.0 * SLOWDOWN))
        check("reloaded" in out, "--watch reloaded the file")
        out = text(r.key(b"j"))
        check(re.search(r"Part 1200[01]\b", out) is not None,
              "after the reload the reader is still at Part 12000")
        r.quit()
    print("progressive layout ok" if ok else "progressive layout FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
