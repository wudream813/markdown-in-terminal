#!/usr/bin/env python3
"""A multi-megabyte document must not need hundreds of megabytes of RAM.

The reader used to hold the whole document twice over: the parse tree, and one
Line object per screen row for the entire file, plus a screen the full height of
the document (Screen keeps two cell buffers, so that alone was 2 x 12 bytes x
width x rows - 190 MB for a 4 MB file).  It is now 2 x 12 bytes x width x 512:
--dump lays the document out in slices and prints them as it goes.

This test writes a document of a few megabytes at run time (nothing big is
committed), renders it and checks the peak RSS of the process.  The budget is
generous - it is there to catch a return to "the whole document in a framebuffer",
not to pin the current number down.

    python3 tools/memory_test.py ./build/mdt

MDT_TEST_SLOWDOWN scales the budget for an instrumented (ASan/UBSan) build;
MDT_MEM_BUDGET_MB overrides it outright.
"""
import os
import random
import subprocess
import sys
import tempfile

BIN = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "./build/mdt")
SLOWDOWN = float(os.environ.get("MDT_TEST_SLOWDOWN", "1") or 1)
BUDGET_MB = float(os.environ.get("MDT_MEM_BUDGET_MB", "0") or 0) or (220.0 * SLOWDOWN)
SIZE_MB = 3.0


def make_document(path, megabytes):
    random.seed(11)
    words = ("reader terminal markdown paragraph render width colour heading quote list table "
             "memory layout span block widget buffer cache stream symbol index").split()
    target = int(megabytes * 1e6)
    written = 0
    n = 0
    with open(path, "w", encoding="utf-8") as f:
        while written < target:
            n += 1
            chunk = [
                f"## Section {n}\n\n",
                " ".join(random.choice(words) for _ in range(90)) + "\n\n",
                "- " + " ".join(random.choice(words) for _ in range(15)) + "\n",
                "- " + " ".join(random.choice(words) for _ in range(15)) + "\n\n",
                "> " + " ".join(random.choice(words) for _ in range(25)) + "\n\n",
                f"```cpp\nint f{n}() {{ return {n}; }}\n```\n\n",
            ]
            text = "".join(chunk)
            f.write(text)
            written += len(text)
    return written


def run_measured(args, stdout_path):
    """Runs the binary and returns (exit code, peak RSS in MB)."""
    with open(stdout_path, "wb") as out:
        pid = os.fork()
        if pid == 0:
            os.dup2(out.fileno(), 1)
            os.dup2(os.open(os.devnull, os.O_WRONLY), 2)
            os.execv(BIN, [BIN] + args)
        _, status, ru = os.wait4(pid, 0)
    code = os.waitstatus_to_exitcode(status)
    if sys.platform == "darwin":
        rss_mb = ru.ru_maxrss / (1024.0 * 1024.0)
    else:
        rss_mb = ru.ru_maxrss / 1024.0
    return code, rss_mb


def main():
    if BIN.lower().endswith(".exe") or not hasattr(os, "wait4"):
        print("   (skipped: this check measures the process' own RSS)")
        return 0
    tmp = tempfile.mkdtemp(prefix="mdt-mem-")
    doc = os.path.join(tmp, "big.md")
    size = make_document(doc, SIZE_MB)
    out = os.path.join(tmp, "dump.txt")
    code, rss = run_measured(["--dump", "--width=100", doc], out)
    failures = []
    if code != 0:
        failures.append("--dump exited %d on a %.1f MB document" % (code, size / 1e6))
    if os.path.getsize(out) < size // 2:
        failures.append("the dump is suspiciously short (%d bytes)" % os.path.getsize(out))
    print("   %.1f MB document: %.0f MB peak (budget %.0f MB)" % (size / 1e6, rss, BUDGET_MB))
    if rss > BUDGET_MB:
        failures.append("peak RSS %.0f MB is over the %.0f MB budget" % (rss, BUDGET_MB))
    else:
        print("   ok   a %.0f MB document renders without holding the page in memory" % (size / 1e6))

    # --diag must be able to say what a document costs.
    proc = subprocess.run([BIN, "--diag", doc], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                          cwd=os.path.dirname(BIN) or ".")
    text = proc.stdout.decode("utf-8", "replace")
    if "peak RSS for the whole document" in text and "layout          :" in text:
        for line in text.splitlines():
            if "memory" in line and "peak RSS" in line:
                print("   ok  ", line.strip())
    else:
        failures.append("--diag does not report the memory the document costs")

    import shutil
    shutil.rmtree(tmp, ignore_errors=True)
    if failures:
        print("\n   FAIL:")
        for f in failures:
            print("     - " + f)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
