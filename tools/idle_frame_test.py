#!/usr/bin/env python3
"""An idle reader must not redraw, and must not keep resetting its modes.

Issue #3 reported a Windows build that "flickered a lot".  Both halves of that
are measured here, and both used to fail on Windows only:

  frames
      With no input and nothing to do, mdt should draw its one frame and then
      stop.  On Windows the event loop spun at ~600 Hz (a console input handle
      reports itself as ready even when its buffer is empty, so the wait the
      loop relies on returned immediately), and every pass redrew.

  mode handovers
      Taking the tty back after a child process cooked it re-sends "enter the
      alternate screen" - which clears the screen - plus the mouse modes.  That
      is right on the transition and wrong on every pass of the loop, which is
      what happened when the comparison was made against the *requested*
      console mode rather than the one the console kept.

The second half is exercised for real with MDT_FAKE_STALE_MODE=1, which makes
raw_intact() report "the modes are not ours" the way a console that drops a bit
does.  A build with the fix stays quiet (a repair, then a slow retry); the
broken one draws hundreds of frames per second.

    python3 tools/idle_frame_test.py [BIN] [WINE] [WINEARG...]
"""
import os
import pty
import re
import select
import signal
import sys
import time

BIN = sys.argv[1] if len(sys.argv) > 1 else "./build/mdt"
WINE = sys.argv[2] if len(sys.argv) > 2 else None
TMP = os.environ.get("TMPDIR", "/tmp")
FRAMES = os.path.join(TMP, "mdt-idle-frames.log")
DOC = os.path.join(TMP, "mdt-idle-doc.md")
# A cold wine prefix can take a while before the first frame lands, so the
# warm-up is generous under wine; the byte budgets are inflated there too,
# because wine's console pty inserts CRs (and sometimes LF/CR) into the stream.
WARMUP = 25.0 if WINE else 8.0
IDLE = 3.0       # the window that is measured
SCALE = 3 if WINE else 1


# Escape sequences, as regular expressions.  Test helpers only: the reader has
# a real parser (src/term.cpp).  Notes from the field: Wine's console injects
# CR/LF inside long sequences and can swallow an OSC terminator, so the string
# variants accept either BEL or ST and the result is whitespace-stripped, which
# re-joins a status bar that a cursor move (or Wine) broke in half.
_ESC = re.compile(
    rb"\x1b\[[0-9;?<=>]*[ -/]*[@-~]"          # CSI
    rb"|\x1b\][^\x07\x1b]*(\x07|\x1b\\)?"     # OSC, BEL or ST
    rb"|\x1b[P^_X][^\x1b]*(\x1b\\)?"        # DCS / APC / PM / SOS
    rb"|\x1b[@-Z\\-~]"                    # other two-byte escapes
)


def plain(data: bytes) -> bytes:
    """What the terminal would show: escape sequences removed.

    Order matters: wine's console inserts CR/LF *inside* long sequences, so the
    whitespace goes first - otherwise the sequences no longer match and their
    text survives.  Removing the whitespace also re-joins a status bar that a
    cursor move (or wine) broke in half, which is what the caller searches.
    """
    stripped = data.replace(b"\r", b"").replace(b"\n", b"")
    return _ESC.sub(b"", stripped)


class Result:
    def __init__(self):
        self.frames = 0
        self.idle_bytes = 0
        self.idle_seconds = 0.0
        self.total_seconds = 0.0
        self.fails = []
        self.out = b""
        self.text = ""


def run(fake_stale: bool) -> Result:
    r = Result()
    env = dict(os.environ, MDT_LANG="en", MDT_FRAME_LOG=FRAMES)
    env.pop("MDT_FAKE_STALE_MODE", None)
    if fake_stale:
        env["MDT_FAKE_STALE_MODE"] = "1"
    argv = [WINE, BIN] if WINE else [BIN]
    if WINE:
        env["WINEDEBUG"] = "-all"
        env.setdefault("WINEPREFIX", os.path.expanduser("~/.wineprefix"))
    try:
        os.unlink(FRAMES)
    except FileNotFoundError:
        pass
    # A document with no pictures and no maths: nothing may want to redraw it.
    with open(DOC, "w") as f:
        f.write("# still\n\nA paragraph that never changes.\n")
    pid, fd = pty.fork()
    if pid == 0:
        os.execvpe(argv[0], argv + ["--gfx=none", DOC], env)

    def pump(seconds):
        end = time.time() + seconds
        while time.time() < end:
            sel, _, _ = select.select([fd], [], [], 0.2)
            if sel:
                try:
                    r.out += os.read(fd, 65536)
                except OSError:
                    return

    started = time.time()
    try:
        pump(WARMUP)
        if b"for help" not in plain(r.out):
            r.fails.append("no first frame appeared")
            return r
        seen = len(r.out)
        t0 = time.time()
        pump(IDLE)
        r.idle_seconds = time.time() - t0
        r.idle_bytes = len(r.out) - seen
        r.total_seconds = time.time() - started
    finally:
        # never leave a reader (or a wine process) behind: the next scenario
        # would then share its frame log
        try:
            os.kill(pid, signal.SIGKILL)
        except OSError:
            pass
        try:
            os.waitpid(pid, 0)
        except OSError:
            pass
        try:
            os.close(fd)
        except OSError:
            pass
    text = open(FRAMES, encoding="utf-8", errors="replace").read() if os.path.exists(FRAMES) else ""
    r.frames = len(re.findall(r"^--- frame .*$", text, re.M))
    r.text = text
    return r


def check(name: str, r: Result, frame_budget: int, byte_budget: int) -> bool:
    print(f"== {name} ==")
    print(f"   frames {r.frames}, idle {r.idle_seconds:.1f}s / {r.idle_bytes} bytes "
          f"(total {r.total_seconds:.1f}s)")
    if not r.fails and r.frames > frame_budget:
        r.fails.append(f"{r.frames} frames were drawn (budget {frame_budget}): "
                       "the loop is redrawing, not waiting")
    if not r.fails and r.idle_bytes > byte_budget:
        r.fails.append(f"{r.idle_bytes} bytes were written while idle (budget {byte_budget})")
    # re-entering the alternate screen clears the screen: only a start-up or a
    # real repair may do it, never a loop that keeps finding the modes "wrong"
    handovers = r.text.count("\\e[?1049h") + r.text.count("\\e[?1003h\\e[?1006h")
    if not r.fails and handovers > 1:
        r.fails.append(f"the terminal was re-initialised {handovers} times while idle")
    for f in r.fails:
        print("   FAIL " + f)
    if not r.fails:
        print(f"   ok   quiet: {r.frames} frame(s) total, {r.idle_bytes} bytes while idle")
    return not r.fails


def main() -> int:
    # A host that keeps its modes: one frame, and nothing at all afterwards.
    a = run(fake_stale=False)
    ok = check("a host that keeps the modes", a, frame_budget=2, byte_budget=4096 * SCALE)
    # A host that drops a mode bit: one repair, then a retry at a human pace.
    # The spin this guards against wrote hundreds of frames per second, so a
    # budget of six in three seconds is still decisive.
    b = run(fake_stale=True)
    ok = check("a host that drops a mode bit", b, frame_budget=6, byte_budget=20000 * SCALE) and ok
    if not ok:
        print("idle behaviour is wrong")
        return 1
    print("idle behaviour ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
