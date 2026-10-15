#!/usr/bin/env python3
"""The outline panel with more headings than rows (issue #2).

    python3 tools/outline_test.py [binary]

Replays mdt's escape sequences into a small screen model and looks at what the
outline panel actually shows, which is the only way to notice that the panel
stopped scrolling: grepping the raw bytes cannot tell a heading drawn in the
panel from the same heading drawn in the document.

The document is the one from the report - thirty headings in a twenty-row
terminal - so the panel is guaranteed to be too short for the list.
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
ROWS, COLS = 20, 90
PANEL_COLS = 24          # // the panel is max(24, min(42, cols / 3)) wide


class Screen:
    """Just enough of a terminal to know what is on the screen.

    mdt repaints the whole frame, so replaying every byte in order lands on the
    final frame.  Handles CUP, ED/EL and the sequences mdt emits; OSC/APC/DCS
    are consumed whole (image payloads included).

    A read can split an escape sequence in half - a colour escape straddling a
    chunk boundary once leaked its parameters onto the screen as text - so
    anything unterminated is carried over to the next feed instead of being
    guessed at.
    """

    def __init__(self, rows, cols):
        self.rows, self.cols = rows, cols
        self.grid = [[" "] * cols for _ in range(rows)]
        self.cy = self.cx = 0
        self.pending = b""

    def feed(self, data: bytes) -> None:
        buf = self.pending + data
        self.pending = b""
        i = 0
        n = len(buf)
        while i < n:
            b = buf[i]
            if b == 0x1B:
                consumed = self._escape(buf, i)
                if consumed < 0:                 # incomplete: wait for more
                    self.pending = buf[i:]
                    return
                i = consumed
                continue
            if b == 0x0A:
                self.cy = min(self.rows - 1, self.cy + 1)
                i += 1
                continue
            if b == 0x0D:
                self.cx = 0
                i += 1
                continue
            if b >= 0x20:
                ch, size = self._decode(buf, i)
                if self.cx < self.cols:
                    self.grid[self.cy][self.cx] = ch
                self.cx += 1
                i += size
                continue
            i += 1

    @staticmethod
    def _decode(buf, i):
        """One code point: (character, bytes used)."""
        b = buf[i]
        size = 1 if b < 0x80 else 2 if b >= 0xC0 and b < 0xE0 else 3 if b < 0xF0 else 4
        if i + size > len(buf):
            return "?", 1
        try:
            return buf[i:i + size].decode("utf-8"), size
        except UnicodeDecodeError:
            return "?", 1

    def _escape(self, buf, i):
        """Consume one escape sequence; -1 when the buffer ends inside it."""
        if i + 1 >= len(buf):
            return -1
        kind = buf[i + 1]
        if kind == 0x5B:                                   # CSI: ESC [ ... final
            j = i + 2
            while j < len(buf) and not (0x40 <= buf[j] <= 0x7E):
                j += 1
            if j >= len(buf):
                return -1                                  # the final byte is not here yet
            args = buf[i + 2:j].decode("ascii", "replace")
            final = chr(buf[j])
            nums = [int(a) if a.isdigit() else 0 for a in re.split(r"[;:]", args.lstrip("?"))]
            if final == "H":
                self.cy = max(0, min(self.rows - 1, (nums[0] if nums and nums[0] else 1) - 1))
                self.cx = max(0, min(self.cols - 1, (nums[1] if len(nums) > 1 and nums[1] else 1) - 1))
            elif final == "J" and (not nums or nums[0] in (0, 2)):
                self.grid = [[" "] * self.cols for _ in range(self.rows)]
            elif final == "K" and self.cy < self.rows:
                if not nums or nums[0] == 0:
                    for c in range(self.cx, self.cols):
                        self.grid[self.cy][c] = " "
            return j + 1
        if kind in (0x5D, 0x50, 0x5F, 0x5E):               # OSC / DCS / APC / PM
            j = i + 2
            while j < len(buf):
                if buf[j] == 0x07:
                    return j + 1
                if buf[j] == 0x1B and j + 1 < len(buf) and buf[j + 1] == 0x5C:
                    return j + 2
                j += 1
            return -1                                      # no terminator yet
        return i + 2

    def row(self, y: int) -> str:
        return "".join(self.grid[y]).rstrip()

    def panel_rows(self, width=None) -> list:
        """The right-hand panel columns of every row (the panel's own width)."""
        w = width if width else panel_width(self.cols)
        return [self.row(y)[self.cols - w:] for y in range(self.rows)]

    def dump(self) -> str:
        return "\n".join("%2d|%s|" % (i, self.row(i)) for i in range(self.rows))


def panel_width(cols: int) -> int:
    """The same formula as App::render: min(42, max(24, cols / 3))."""
    return min(42, max(24, cols // 3))


def run(keys, doc, rows=ROWS, cols=COLS, extra=()):
    pid, fd = pty.fork()
    if pid == 0:
        env = dict(os.environ)
        env["TERM"] = "xterm-256color"
        env["MDT_LANG"] = "en"
        os.execvpe(BIN, [BIN, "--gfx=none", *extra, doc], env)
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
    scr = Screen(rows, cols)
    t0 = time.time()
    # Wait for the first frame before sending anything: before raw mode is on,
    # keys are swallowed.  The status bar is the last thing drawn, so it is the
    # signal that the frame is on the screen.
    nxt = time.time() + 1.5
    ready = False
    ki = 0
    while time.time() - t0 < 25:
        r, _, _ = select.select([fd], [], [], 0.1)
        if r:
            try:
                chunk = os.read(fd, 1 << 20)
            except OSError:
                break
            if not chunk:
                break
            scr.feed(chunk)
            if not ready and "for help" in scr.row(rows - 1):
                ready = True
                nxt = time.time() + 0.3
        if ready and time.time() >= nxt and ki < len(keys):
            os.write(fd, keys[ki])
            ki += 1
            nxt = time.time() + 0.6
        if ready and ki >= len(keys) and time.time() > nxt + 0.8:
            break
    try:
        os.close(fd)
    except OSError:
        pass
    try:
        os.waitpid(pid, 0)
    except ChildProcessError:
        pass
    return scr


def main() -> int:
    doc = "/tmp/outline_test.md"
    with open(doc, "w", encoding="utf-8") as f:
        f.write("# aaa\n\n")
        for i in range(28):
            f.write("## ddd\n\nparagraph %d\n\n" % i)
        f.write("## dddz\n\nthe very last paragraph\n")

    fails = []

    def check(ok, label):
        print(("   ok   " if ok else "   FAIL ") + label)
        if not ok:
            fails.append(label)

    def doc_pane(scr):
        """The columns left of the panel: what the document shows."""
        w = panel_width(scr.cols) if "OUTLINE" in "\n".join(scr.panel_rows()) else 0
        return "\n".join(scr.row(y)[:scr.cols - w] for y in range(scr.rows - 1))

    def show(scr, label):
        print("   (%s)" % label)
        for i, r in enumerate(scr.panel_rows()):
            if r.strip():
                print("     |" + r)

    # SGR mouse uses 1-based coordinates; the panel owns the right-hand columns
    panel_w = panel_width(COLS)
    inside = COLS - panel_w // 2          # 0-based column inside the panel
    inside_col = ("\x1b[<65;%d;8M" % (inside + 1)).encode()

    # --- 1. the report: thirty headings, twenty rows -------------------------
    scr = run([b"G"], doc, extra=("--toc",))
    panel = scr.panel_rows()
    text = "\n".join(panel)
    check("OUTLINE" in text, "the outline panel is drawn")
    check("1/30" in text, "the header says how long the list is and where the selection is (1/30)")
    check("↓" in text, "an arrow says the list continues below the panel")
    check("dddz" not in text, "the last heading is off the panel to begin with")
    check("↑" not in text, "no 'more above' arrow while the list is at the top")

    # --- 2. J walks the list and the panel follows ---------------------------
    scr2 = run([b"J"] * 29, doc, extra=("--toc",))
    p2 = "\n".join(scr2.panel_rows())
    check("30/30" in p2, "J walks the selection to the end of the list")
    check("dddz" in p2, "the last heading is visible once the selection reaches it")
    check("↑" in p2, "the 'more above' arrow appears")
    check("aaa" not in p2, "the first entry has scrolled out of the panel by then")
    check("30/30  dddz" in scr2.row(scr2.rows - 1), "the status bar names the entry under the selection")

    # --- 3. Enter jumps the document to the selection ------------------------
    scr3 = run([b"J"] * 29 + [b"\r"], doc, extra=("--toc",))
    body = doc_pane(scr3)
    check("dddz" in body and "very last paragraph" in body,
          "Enter jumps the document to the selected heading")
    check("aaa" not in body, "the document really moved (the first heading is off the screen)")

    # --- 3b. and the same entry is reachable with the wheel ------------------
    scr3b = run([inside_col] * 10, doc, extra=("--toc",))
    p3b = "\n".join(scr3b.panel_rows())
    check("30/30" in p3b and "dddz" in p3b, "the wheel walks the list to the last heading too")

    # --- 4. the wheel over the panel walks the list, not the document --------
    # SGR mouse: button 65 = wheel down, 64 = wheel up.
    scr4 = run([inside_col], doc, extra=("--toc",))
    p4 = "\n".join(scr4.panel_rows())
    check("4/30" in p4, "the wheel over the panel walks the selection by three")
    check("0%" in scr4.row(scr4.rows - 1) and "line 1" in scr4.row(scr4.rows - 1),
          "... and does not scroll the document (Enter is what moves it)")
    check("↓" in p4, "the arrow is still there (the list is still longer than the panel)")

    # --- 5. the wheel over the document still scrolls the document -----------
    scr5 = run([("\x1b[<65;20;8M").encode()], doc, extra=("--toc",))
    check("aaa" not in doc_pane(scr5), "the wheel over the document scrolls the document")
    check("aaa" in "\n".join(scr5.panel_rows()), "... and leaves the panel where it was")

    # --- 6. without the panel, J still moves the document --------------------
    scr6 = run([b"J", b"J"], doc)
    check("aaa" not in doc_pane(scr6), "J without the outline open still jumps the document")
    check("OUTLINE" not in "\n".join(scr6.row(y) for y in range(scr6.rows)),
          "... and does not open the panel")

    if fails:
        print("\nFAIL:")
        for f in fails:
            print("  -", f)
        print("\nfinal screen:\n" + scr.dump())
        return 1
    print("\noutline checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
