#!/usr/bin/env python3
"""Checks the Luogu remark-directive syntax (help: luogu.com.cn/article/70w8j2pj)

    python3 tools/luogu_test.py ./build/mdt
    python3 tools/luogu_test.py build/win/mdt.exe wine64

Structure checks run against --dump (plain text, so they work everywhere);
the colour checks (callout bars, highlighted code lines) run in a pty.
"""
import os
import re
import subprocess
import sys

FIXTURE = os.path.join(os.path.dirname(__file__), "..", "tests", "fixtures", "luogu.md")


def check(fails, ok, label):
    print(("   ok   " if ok else "   FAIL ") + label)
    if not ok:
        fails.append(label)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    exe = sys.argv[1]
    wine = sys.argv[2] if len(sys.argv) > 2 else None
    argv = [wine, exe] if wine else [exe]
    fixture = os.path.normpath(FIXTURE)

    dump = subprocess.run(argv + ["--dump", "--width=80", fixture],
                          capture_output=True).stdout.decode("utf-8", "replace")
    lines = dump.splitlines()
    fails = []

    # ---- callouts ----------------------------------------------------------
    for t in ["这是一个提示", "我是提示内容。", "我默认处于展开状态", "我是展开内容。",
              "我是父容器", "我是一些文字。", "我是子容器 1", "我是内容 1。"]:
        check(fails, t in dump, f"callout text kept: {t}")
    nested = any("│ │" in l for l in lines)
    check(fails, nested, "nested callout gets a second bar")

    # ---- alignment ---------------------------------------------------------
    def leading(needle):
        for l in lines:
            if needle in l:
                return len(l) - len(l.lstrip(" "))
        return -1
    ll, lc, lr = leading("居左内容。"), leading("居中内容。"), leading("居右内容。")
    check(fails, ll >= 0 and lc > ll + 10 and lr > lc + 10,
          f"align left/center/right step right ({ll}, {lc}, {lr})")

    # ---- epigraph ----------------------------------------------------------
    le, la = leading("大家好啊"), leading("——otto")
    check(fails, le > 20 and la > 20, f"epigraph right-aligned ({le}, {la})")

    # ---- cute-table{three} --------------------------------------------------
    heavy = [i for i, l in enumerate(lines) if "━" in l]
    check(fails, len(heavy) >= 2, "three-line table has heavy rules")
    if len(heavy) >= 2:
        seg = lines[heavy[0]:heavy[1] + 1]
        check(fails, not any("│" in l for l in seg), "three-line table has no verticals")

    # ---- cute-table{tuack=2} ------------------------------------------------
    check(fails, any("┃" in l for l in lines), "tuack heavy vertical after column 2")

    # ---- cell merging --------------------------------------------------------
    box = [i for i, l in enumerate(lines) if l.startswith("┌") or l.startswith("└")]
    if len(box) >= 2:
        seg = "\n".join(lines[box[-2]:box[-1] + 1])
        check(fails, seg.count("^") == seg.count("10^5"),
              '"^" cells merged away (only 10^5 keeps a caret)')
        check(fails, "<" not in seg, '"<" cell merged into its left neighbour')
    else:
        check(fails, False, "merged table box found")

    # ---- bare fence defaults to C++ (Luogu editor behaviour) -----------------
    check(fails, dump.count("── cpp ") == 2, "bare fence is highlighted as C++ (2 labels)")

    # ---- colour checks in a pty ---------------------------------------------
    import fcntl
    import pty as ptymod
    import select
    import struct
    import termios
    import time

    pid, fd = ptymod.fork()
    if pid == 0:
        env = dict(os.environ)
        env["TERM"] = "xterm-256color"
        os.execvpe(argv[0], argv + ["--gfx=none", fixture], env)
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", 60, 100, 0, 0))
    out = b""
    t0 = time.time()
    while time.time() - t0 < 6.0:
        r, _, _ = select.select([fd], [], [], 0.2)
        if r:
            try:
                chunk = os.read(fd, 1 << 20)
            except OSError:
                break
            if not chunk:
                break
            out += chunk
        if b"for help" in out and time.time() - t0 > 1.0:
            break
    try:
        os.close(fd)
        os.kill(pid, 15)
        os.waitpid(pid, 0)
    except (OSError, ChildProcessError):
        pass
    frame = out.decode("utf-8", "replace")
    check(fails, "38;2;126;197;255" in frame, "info callout bar is blue")
    check(fails, "38;2;158;206;106" in frame, "success callout bar is green")
    check(fails, "38;2;224;175;104" in frame, "warning callout bar is yellow")
    # lines=2-3,5 get a warm tint over the theme background {24,26,31}
    check(fails, "48;2;50;47;39" in frame, "code lines 2-3,5 are highlighted")

    if fails:
        print("luogu checks failed:", ", ".join(fails))
        return 1
    print("luogu syntax ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
