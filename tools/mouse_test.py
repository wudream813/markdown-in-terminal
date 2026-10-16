#!/usr/bin/env python3
"""Checks the mouse support: wheel scrolling (both encodings), the scrollbar
glyphs, clicking on the scrollbar to jump and dragging the scrollbar: every
motion event between press and release has to move the view live, not only
the release (reported as "拖动滚动条没用实时更新").

    python3 tools/mouse_test.py ./build/mdt
    python3 tools/mouse_test.py build/win/mdt.exe wine64
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

TIOCSWINSZ = getattr(termios, "TIOCSWINSZ", 0x5414)  # macOS/BSD differ from Linux
ANSI = re.compile(r"\x1b\[[0-9;?]*[a-zA-Z]|\x1b_G.*?\x1b\\|\x1b\]0;.*?\x07")


KEY_LOG = os.path.join(os.environ.get("TMPDIR", "/tmp"), "mdt-mouse-keylog.txt")


def run(argv, keys, cols=80, rows=24, settle=2.0, after=1.6, total=None):
    try:
        os.unlink(KEY_LOG)
    except FileNotFoundError:
        pass
    pid, fd = pty.fork()
    if pid == 0:
        env = dict(os.environ)
        env["TERM"] = "xterm-256color"
        env["MDT_LANG"] = "en"   # the assertions below are English; do not
                                 # inherit a Chinese LANG from the machine
        # MDT_KEY_LOG: a failure has to say whether the host handed the mouse
        # reports to mdt at all, or whether mdt received them and dropped them.
        # (Under Ubuntu's Wine 9 the console host does not deliver them.)
        env["MDT_KEY_LOG"] = KEY_LOG
        os.execvpe(argv[0], argv, env)
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
    out = b""
    t0 = time.time()
    sent = False
    tsent = 0.0
    # A fixed settle time loses keys on slow hosts (macOS CI scans the fresh
    # binary on first exec): while mdt has not entered raw mode the pty is in
    # canonical mode and an escape sequence without a newline is never
    # delivered.  Send only once the first frame (status bar) has arrived.
    deadline = total if total else settle + after + 8.0
    while time.time() - t0 < deadline:
        r, _, _ = select.select([fd], [], [], 0.2)
        if r:
            try:
                chunk = os.read(fd, 1 << 20)
            except OSError:
                break
            if not chunk:
                break
            out += chunk
        ready = b"for help" in out
        if not sent and ((ready and time.time() - t0 > 0.5) or time.time() - t0 > settle + 4.0):
            for k in keys:
                try:
                    os.write(fd, k)
                except OSError:
                    break
                time.sleep(0.2)
            sent = True
            tsent = time.time()
        if sent and time.time() - tsent > after:
            break
    try:
        os.close(fd)
    except OSError:
        pass
    try:
        os.kill(pid, 15)
    except ProcessLookupError:
        pass
    try:
        os.waitpid(pid, 0)
    except ChildProcessError:
        pass
    return out


def run_steps(argv, steps, cols=80, rows=24, settle=2.0):
    """Like run(), but captures the output at marked points (payload None)."""
    pid, fd = pty.fork()
    if pid == 0:
        env = dict(os.environ)
        env["TERM"] = "xterm-256color"
        env["MDT_LANG"] = "en"   # the assertions below are English; do not
                                 # inherit a Chinese LANG from the machine
        os.execvpe(argv[0], argv, env)
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
    out = b""
    caps = []

    def drain(sec):
        nonlocal out
        t = time.time()
        while time.time() - t < sec:
            r, _, _ = select.select([fd], [], [], 0.05)
            if r:
                try:
                    chunk = os.read(fd, 1 << 20)
                except OSError:
                    return
                if not chunk:
                    return
                out += chunk

    t = time.time()
    while time.time() - t < settle + 6.0:
        drain(0.2)
        if b"for help" in out and time.time() - t > 0.5:
            break
    for payload, wait in steps:
        if payload:
            try:
                os.write(fd, payload)
            except OSError:
                break
        drain(wait if wait else 0.4)
        if payload is None:
            caps.append(out)
    try:
        os.close(fd)
    except OSError:
        pass
    try:
        os.kill(pid, 15)
    except ProcessLookupError:
        pass
    try:
        os.waitpid(pid, 0)
    except ChildProcessError:
        pass
    return caps


def status(out):
    txt = ANSI.sub("", out.decode("utf-8", "replace"))
    m = re.findall(r"(\d+)%", txt)
    return int(m[-1]) if m else -1


def log_lines():
    try:
        return open(KEY_LOG, encoding="utf-8", errors="replace").read().splitlines()
    except OSError:
        return []


def received_any_input():
    # Event lines start with "[time] "; key_log_note() lines are indented and
    # describe the console modes, so they do not count as input.
    return any(ln.startswith("[") and "nothing" not in ln for ln in log_lines())


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    exe = sys.argv[1]
    wine = sys.argv[2] if len(sys.argv) > 2 else None
    args = ["--gfx=none", "demo/demo.md"]
    argv = [wine, exe] + args if wine else [exe] + args

    # Under wine the console host may accept ENABLE_MOUSE_INPUT and then never
    # deliver a single mouse record to the program (Ubuntu's Wine 9 in CI does
    # exactly that: verified with MDT_KEY_LOG, the mode line is all the reader
    # ever sees).  That is the host, not the program, so when we are running
    # through wine and not one byte of the mouse reports arrives, say so and
    # skip.  Wine 10 (tested locally) and a real Windows console do deliver
    # them - and a real console is driven without this wine argument, so a
    # genuine regression there still fails the build.
    if wine:
        run(argv, [b"\x1b[<65;40;12M"] * 6)
        if not received_any_input():
            print(f"   skip  {wine} delivers no console mouse input at all to the")
            print("         program: the console accepts the mouse mode and then")
            print("         nothing arrives.  Known Wine 9 limitation, not a")
            print("         reader bug (wine 10 and real Windows deliver them)")
            return 0

    fails = []
    out = run(argv, [])
    bars = out.count("\u2588".encode()) > 0 and out.count("\u2502".encode()) > 0
    print(("   ok   " if bars else "   FAIL ") + "scrollbar drawn (thumb + track)")
    fails += [] if bars else ["scrollbar"]

    # wheel down, SGR encoding (what most terminals send after \x1b[?1006h)
    p1 = status(run(argv, [b"\x1b[<65;40;12M"] * 6))
    ok = p1 > 0
    print(("   ok   " if ok else "   FAIL ") + f"wheel down (SGR) scrolled to {p1}%")
    fails += [] if ok else ["wheel SGR"]

    # wheel down, X10 encoding (terminals without SGR mouse)
    p2 = status(run(argv, [b"\x1b[M\x61\x28\x0c"] * 6))
    ok = p2 > 0
    print(("   ok   " if ok else "   FAIL ") + f"wheel down (X10) scrolled to {p2}%")
    fails += [] if ok else ["wheel X10"]

    # click near the bottom of the scrollbar jumps forward
    p3 = status(run(argv, [b"\x1b[<0;80;23M"]))
    ok = p3 > 60
    print(("   ok   " if ok else "   FAIL ") + f"click at the scrollbar foot jumped to {p3}%")
    fails += [] if ok else ["scrollbar click"]

    # and back up near the top
    p4 = status(run(argv, [b"\x1b[<0;80;2M"]))
    ok = 0 <= p4 <= 20
    print(("   ok   " if ok else "   FAIL ") + f"click near the top jumped to {p4}%")
    fails += [] if ok else ["scrollbar click up"]

    # a drag has to move the view while the button is still held:
    # press near the top, motion to the middle, motion further down, release
    caps = run_steps(argv, [
        (b"\x1b[<0;80;3M", 0.5),    # press on the scrollbar
        (b"\x1b[<32;80;12M", 0.5),  # motion: middle of the track
        (None, 0),                   # snapshot A: before the release
        (b"\x1b[<32;80;20M", 0.5),  # motion: near the foot
        (None, 0),                   # snapshot B: before the release
        (b"\x1b[<0;80;20m", 0.5),   # release
        (None, 0),                   # snapshot C
    ])
    if len(caps) == 3:
        a, b, c = (status(x) for x in caps)
        ok = 25 < a < 80 and b > a + 5 and c == b
        print(("   ok   " if ok else "   FAIL ") +
              f"drag tracks live: {a}% mid-drag, {b}% lower, {c}% after release")
        fails += [] if ok else ["scrollbar drag"]
    else:
        print("   FAIL  drag: no captures")
        fails += ["scrollbar drag"]

    if fails:
        print("\nmouse checks failed:", ", ".join(fails))
        log = log_lines()
        inputs = [ln for ln in log if ln.startswith("[") and "nothing" not in ln]
        if inputs:
            print("input the reader actually received:")
            for ln in inputs[-12:]:
                print("   " + ln)
        else:
            for ln in [l for l in log if not l.startswith("[")][-3:]:
                print("   " + ln.strip())
            print("the reader received no input at all from this host: the mouse")
            print("reports never left the test's pty, so nothing here is mdt's")
            print("doing (Ubuntu's Wine 9 console host does not deliver them).")
        return 1
    print("   mouse + scrollbar ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
