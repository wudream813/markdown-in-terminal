#!/usr/bin/env python3
"""--watch: the reader reloads when the file on disk changes.

Three things are checked, and only the first one is the obvious one:

  a plain write
      The file is written in place.  The reader must pick the new text up.

  an atomic save
      Most editors write a temporary file and rename it over the original,
      which is a *different inode*: a watch on the file itself would go deaf
      after the first save.  This is why the watcher sits on the directory.

  an unrelated file next door
      A directory notification fires for every file in it, so `touch other.md`
      must not reload the document.  The stamp comparison is what makes the
      difference, and it is the part that is easy to get wrong.

The reload also keeps the reader's place - a save must not throw away someone's
scroll position - but that is checked by eye in the reader rather than here.

    python3 tools/watch_test.py [BIN]
"""
import os
import pty
import re
import select
import shutil
import signal
import sys
import tempfile
import time

BIN = sys.argv[1] if len(sys.argv) > 1 else "./build/mdt"
WINE = sys.argv[2] if len(sys.argv) > 2 else None  # run the Windows build under wine
WORK = tempfile.mkdtemp(prefix="mdt-watch-")
DOC = os.path.join(WORK, "doc.md")
OTHER = os.path.join(WORK, "other.md")
WARMUP = 8.0

_ESC = re.compile(
    rb"\x1b\[[0-9;?<=>]*[ -/]*[@-~]"
    rb"|\x1b\][^\x07\x1b]*(\x07|\x1b\\)?"
    rb"|\x1b[P^_X][^\x1b]*(\x1b\\)?"
    rb"|\x1b[@-Z\\-~]"
)


def plain(data: bytes) -> str:
    return _ESC.sub(b"", data.replace(b"\r", b"").replace(b"\n", b"")).decode("utf-8", "replace")


class Reader:
    """mdt --watch on a pty, with a helper that waits for text on the screen."""

    def __init__(self, frame_log: str):
        self.out = b""
        self.frame_log = frame_log
        try:
            os.unlink(frame_log)
        except OSError:
            pass
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            env = dict(os.environ, MDT_LANG="en", TERM="xterm-256color",
                       MDT_FRAME_LOG=self.frame_log)
            if WINE:
                env.setdefault("WINEPREFIX", os.path.expanduser("~/.wineprefix"))
                env["WINEDEBUG"] = "-all"
                os.execvpe(WINE, [WINE, BIN, "--gfx=none", "--watch", DOC], env)
            os.execvpe(BIN, [BIN, "--gfx=none", "--watch", DOC], env)

    def pump(self, seconds: float) -> None:
        end = time.time() + seconds
        while time.time() < end:
            sel, _, _ = select.select([self.fd], [], [], 0.1)
            if sel:
                try:
                    self.out += os.read(self.fd, 65536)
                except OSError:
                    return

    def wait_for(self, needle: str, seconds: float) -> bool:
        end = time.time() + seconds
        while time.time() < end:
            if needle in plain(self.out):
                return True
            self.pump(0.1)
        return needle in plain(self.out)

    def seen(self, needle: str) -> bool:
        return needle in plain(self.out)

    def frames(self) -> int:
        """Frames drawn so far: the reader's own log, not the screen."""
        try:
            with open(self.frame_log, encoding="utf-8", errors="replace") as f:
                return sum(1 for line in f if line.startswith("--- frame"))
        except OSError:
            return 0

    def close(self) -> None:
        try:
            os.kill(self.pid, signal.SIGKILL)
        except OSError:
            pass
        try:
            os.waitpid(self.pid, 0)
        except OSError:
            pass
        try:
            os.close(self.fd)
        except OSError:
            pass


def write_atomic(path: str, text: str) -> None:
    """What an editor does on save: temp file, then rename over the original."""
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        f.write(text)
    os.replace(tmp, path)


def run_suite(poll_only: bool) -> list[str]:
    """The whole set, once through the kernel notifier and once through stat()."""
    fails: list[str] = []

    def check(ok: bool, what: str) -> None:
        print(("   ok   " if ok else "   FAIL ") + what)
        if not ok:
            fails.append(what)

    if poll_only:
        os.environ["MDT_WATCH_POLL"] = "1"
    else:
        os.environ.pop("MDT_WATCH_POLL", None)
    try:
        with open(DOC, "w", encoding="utf-8") as f:
            f.write("# first\n\nThe first version of the document.\n")
        with open(OTHER, "w", encoding="utf-8") as f:
            f.write("not the document\n")

        print("== --watch ==")
        r = Reader(os.path.join(WORK, "frames.log"))
        try:
            if not r.wait_for("The first version", WARMUP):
                print("   FAIL no first frame appeared")
                return 1
            check(r.seen("watching for changes"), "the status bar says it is watching")

            # 1. plain write
            with open(DOC, "w", encoding="utf-8") as f:
                f.write("# second\n\nA plain write landed.\n")
            check(r.wait_for("A plain write landed", 5.0), "a plain write reloads")
            check(r.seen("reloaded"), "the status bar says it reloaded")

            # 2. the same write again must reload too (and not only once)
            time.sleep(0.5)
            with open(DOC, "w", encoding="utf-8") as f:
                f.write("# third\n\nA second write landed.\n")
            check(r.wait_for("A second write landed", 5.0), "watching survives a reload")

            # 3. atomic save: a different inode, the usual editor behaviour
            time.sleep(0.5)
            write_atomic(DOC, "# fourth\n\nAn atomic save landed.\n")
            check(r.wait_for("An atomic save landed", 5.0), "a rename-over save reloads")

            # 4. a neighbour changing is not a reason to reload.  The status
            # line is repainted by any frame, so count frames - a reload always
            # redraws and a quiet reader does not.
            r.pump(2.2)                       # let the last status expire
            before = r.frames()
            with open(OTHER, "w", encoding="utf-8") as f:
                f.write("still not the document\n")
            r.pump(1.5)
            check(r.frames() == before, "a neighbour file changing does not reload")
        finally:
            r.close()

        # 5. --watch refuses to pretend in the non-interactive modes
        import subprocess
        argv0 = [WINE, BIN] if WINE else [BIN]
        for args, why in ((["--watch", "--dump", DOC], "--dump"),
                          (["--watch", "-"], "stdin")):
            p = subprocess.run(argv0 + args, capture_output=True, text=True,
                               input="# x\n" if "-" in args else None)
            check(p.returncode == 2 and "--watch" in (p.stderr + p.stdout),
                  f"--watch is refused with {why}")
    finally:
        for name in (DOC, OTHER, DOC + ".tmp"):
            try:
                os.unlink(name)
            except OSError:
                pass
    return fails


def main() -> int:
    print("== --watch (kernel notifier) ==")
    fails = run_suite(poll_only=False)
    # Same expectations without inotify: the stat() fallback is what a file
    # system without notifications gets, so it cannot be the untested branch.
    print("== --watch (stat fallback) ==")
    fails += run_suite(poll_only=True)
    shutil.rmtree(WORK, ignore_errors=True)
    if fails:
        print(f"{len(fails)} watch check(s) failed")
        return 1
    print("watch ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
