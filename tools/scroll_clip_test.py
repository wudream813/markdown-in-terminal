#!/usr/bin/env python3
"""Pictures cut by the viewport edge, and one paint per frame.

Two reported problems in one harness:

1. "超过顶端就不能渲染" - a picture that hangs over the top of the window used
   to be clamped (drawn from its first row, or not drawn at all when the
   cursor move went negative) instead of being cut.  mdt now sends the visible
   part as its own bitmap: after scrolling a four-row picture eight rows past
   the top, the kitty stream must carry a second transmission (the cut bitmap,
   smaller than the original) and place it at screen row 1.

2. "每次拖动重绘会闪烁" - the frame used to close the DEC 2026 synchronized
   update before the graphics escapes, so the terminal painted text and
   pictures in two steps.  Every frame in MDT_FRAME_LOG must now be a single
   2026h...2026l window with the placements inside it.

usage: scroll_clip_test.py BIN
"""
import os
import pty
import fcntl
import termios
import struct
import select
import sys
import time
import re
import zlib
import binascii

BIN = sys.argv[1] if len(sys.argv) > 1 else "./build/mdt"


def png(w, h):
    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + \
            struct.pack(">I", binascii.crc32(c) & 0xFFFFFFFF)
    raw = b"".join(b"\x00" + bytes((200, 60 + y % 200, 60)) * w for y in range(h))
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw))
            + chunk(b"IEND", b""))


class Pty:
    def __init__(self, argv, cols=80, rows=12, env_extra=None):
        self.out = b""
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            env = dict(os.environ)
            env["TERM"] = "xterm-256color"
            if env_extra:
                env.update(env_extra)
            os.execvpe(argv[0], argv, env)
        fcntl.ioctl(self.fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))

    def drain(self, sec):
        t = time.time()
        while time.time() - t < sec:
            r, _, _ = select.select([self.fd], [], [], 0.05)
            if r:
                try:
                    c = os.read(self.fd, 1 << 20)
                except OSError:
                    return
                if not c:
                    return
                self.out += c

    def send(self, b, wait=0.15):
        try:
            os.write(self.fd, b)
        except OSError:
            pass
        self.drain(wait)

    def close(self):
        self.send(b"q", 0.3)
        try:
            os.close(self.fd)
        except OSError:
            pass
        try:
            os.kill(self.pid, 15)
        except ProcessLookupError:
            pass
        try:
            os.waitpid(self.pid, 0)
        except ChildProcessError:
            pass


def main():
    d = ".scroll_clip.%d" % os.getpid()
    os.makedirs(d, exist_ok=True)
    fails = []
    log = os.path.join(d, "frames.log")
    try:
        pic = os.path.join(d, "tall.png")          # 60x80 px = 4 rows of 20
        with open(pic, "wb") as f:
            f.write(png(60, 80))
        doc = os.path.join(d, "clip.md")
        with open(doc, "w") as f:
            for i in range(6):
                f.write("text line %d\n\n" % i)
            f.write("![t](tall.png)\n\n")   # relative to the document, as usual
            for i in range(8):
                f.write("after %d\n\n" % i)
        p = Pty([BIN, "--gfx=kitty", "--cell=10x20", doc],
                env_extra={"MDT_FRAME_LOG": log})
        # wait for the first frame instead of a fixed sleep: on slow hosts
        # (macOS CI) the pty is still in canonical mode and keys would be lost
        t = time.time()
        while time.time() - t < 8.0:
            p.drain(0.25)
            if b"for help" in p.out:
                break
        first = p.out
        p.out = b""
        for _ in range(14):                 # scroll the picture past the top
            p.send(b"j")
        p.drain(0.8)
        scrolled = p.out

        # ---- 1. the cut bitmap is transmitted and placed at row 1 -----------
        trans = re.findall(rb"\x1b_Ga=t,f=100,i=(\d+),q=2,m=0;((?:.|\n)*?)\x1b\\", scrolled)
        place = re.findall(rb"\x1b\[(\-?\d+);(\d+)H\x1b_Ga=p,i=(\d+),p=1", scrolled)
        sizes = {tid: len(payload) for tid, payload in trans}
        ok = bool(place) and place[-1][0] == b"1" and len(trans) >= 3
        if ok:
            last_id = place[-1][2]
            full_id = max(sizes, key=sizes.get)   # the uncut four-row bitmap
            ok = last_id != full_id and sizes[last_id] < sizes[full_id]
        print(("   ok   " if ok else "   FAIL ") +
              "picture over the top edge: a smaller cut bitmap is transmitted and "
              "placed at row 1 (%d transmissions, last placement row %s, id %s)"
              % (len(trans), place[-1][0].decode() if place else "-",
                 place[-1][2].decode() if place else "-"))
        fails += [] if ok else ["top clip"]

        # ---- 2. a drag paints inside one synchronized window --------------
        p.out = b""
        p.send(b"\x1b[<0;80;3M", 0.3)        # press the scrollbar
        p.send(b"\x1b[<32;80;8M", 0.3)       # drag
        p.send(b"\x1b[<32;80;10M", 0.3)
        p.send(b"\x1b[<0;80;10m", 0.3)       # release
        p.close()
        txt = open(log, "rb").read().decode("utf-8", "replace")
        frames = txt.split("--- frame ")[1:]
        bad = 0
        with_gfx = 0
        for fr in frames:
            body = fr.split("\n", 1)[1] if "\n" in fr else fr
            nh, nl = body.count("\\e[?2026h"), body.count("\\e[?2026l")
            if nh != 1 or nl != 1 or body.find("\\e[?2026h") > body.find("\\e[?2026l"):
                bad += 1
            if "\\e_Ga=p" in body:
                with_gfx += 1
                # every placement inside the window
                if not (body.find("\\e[?2026h") < body.find("\\e_Ga=p") < body.find("\\e[?2026l")):
                    bad += 1
        ok = frames and not bad and with_gfx > 0
        print(("   ok   " if ok else "   FAIL ") +
              "every frame is one synchronized window with the placements inside "
              "(%d frames, %d with graphics)" % (len(frames), with_gfx))
        fails += [] if ok else ["sync window"]
    finally:
        for name in os.listdir(d):
            os.remove(os.path.join(d, name))
        os.rmdir(d)
    if fails:
        print("\nscroll/clip checks failed:", ", ".join(fails))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
