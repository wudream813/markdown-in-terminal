#!/usr/bin/env python3
"""A theme change has to reach the formulas, not just the text.

Issue #3: "when I switch theme the background inside the KaTeX formulas did not
change".  A formula bitmap has the theme's ink colour and the page background
composited into it, so a theme change must produce *different pixels* - and the
pictures the layout holds on to, plus the terminal's own image cache, must not
survive it.

Counting transmissions is not enough to prove that: a key that merely says
"this is a different theme" re-sends the same stale bytes.  So this test looks
at the payloads: with the kitty protocol an image travels as
`ESC_G<params>;<base64>`, and after each `t` (theme change) at least one payload
that was never sent before must appear.  With the old one-line `set_theme()`
(nothing dropped, nothing relaid out) the base64 sets are identical phase after
phase - the same bytes, which is exactly what "the background did not change"
looks like on screen.

    python3 tools/theme_formula_test.py [BIN] [WINE] [WINEARG...]
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
DOC = os.path.join(TMP, "mdt-theme-doc.md")
THEMES = 3   # dark -> light -> nord -> monochrome
PAYLOAD = re.compile(rb"\x1b_G[^;\x1b]*;([A-Za-z0-9+/=]{40,})")


def stop(pid, fd):
    for fn, arg in ((os.kill, (pid, signal.SIGKILL)), (os.waitpid, (pid, 0)), (os.close, (fd,))):
        try:
            fn(*arg)
        except OSError:
            pass


def main():
    with open(DOC, "w") as f:
        f.write("# theme\n\nInline $a^2+b^2=c^2$ and a display block:\n\n"
                "$$\\int_0^1 x^2\\,dx = \\frac13$$\n")
    env = dict(os.environ, MDT_LANG="en")
    argv = [WINE, BIN] if WINE else [BIN]
    if WINE:
        env["WINEDEBUG"] = "-all"
        env.setdefault("WINEPREFIX", os.path.expanduser("~/.wineprefix"))
    pid, fd = pty.fork()
    if pid == 0:
        os.execvpe(argv[0], argv + ["--gfx=kitty", DOC], env)
    seen = b""

    def pump(seconds):
        nonlocal seen
        end = time.time() + seconds
        while time.time() < end:
            sel, _, _ = select.select([fd], [], [], 0.2)
            if sel:
                try:
                    seen += os.read(fd, 1 << 16)
                except OSError:
                    return

    def payloads():
        # wine's console inserts CR/LF inside long sequences: take them out
        # before matching, or the base64 never matches as one run.
        clean = seen.replace(b"\r", b"").replace(b"\n", b"")
        return set(PAYLOAD.findall(clean))

    fails = []
    try:
        pump(25 if WINE else 8)
        if b"katex" not in seen.replace(b"\r", b"").replace(b"\n", b""):
            print("   FAIL the reader never drew its first frame")
            return 1
        before = payloads()
        print(f"   image payloads after the first frame: {len(before)}")
        if not before:
            fails.append("no image data was transmitted at all (nothing to compare)")
        for i in range(THEMES):
            os.write(fd, b"t")
            pump(3.0)
            now = payloads()
            fresh = now - before
            print(f"   after theme change {i + 1}: {len(now)} payloads, {len(fresh)} new")
            if not fails and not fresh:
                fails.append(f"theme change {i + 1} produced no new pixels: the pictures "
                             "were reused from the previous theme")
            before = now
    finally:
        stop(pid, fd)

    for f in fails:
        print("   FAIL " + f)
    if fails:
        print("theme changes do not reach the formulas")
        return 1
    print("   ok   every theme change redrew the formulas with new pixels")
    print("theme/formula interaction ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
