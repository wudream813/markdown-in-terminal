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

    # ---- round-17 features (second fixture) ---------------------------------
    def capture(path, rows=40, need=None):
        pid, fd = ptymod.fork()
        if pid == 0:
            env = dict(os.environ)
            env["TERM"] = "xterm-256color"
            os.execvpe(argv[0], argv + ["--gfx=none", path], env)
        fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, 100, 0, 0))
        buf = b""
        t0 = time.time()
        while time.time() - t0 < 12.0:
            r, _, _ = select.select([fd], [], [], 0.2)
            if r:
                try:
                    chunk = os.read(fd, 1 << 20)
                except OSError:
                    break
                if not chunk:
                    break
                buf += chunk
            # wine's first frame can take seconds: wait for what we need
            if b"for help" in buf and time.time() - t0 > 1.0 \
                    and (need is None or need in buf):
                break
        try:
            os.close(fd)
            os.kill(pid, 15)
            os.waitpid(pid, 0)
        except (OSError, ChildProcessError):
            pass
        # wine's console wrapper splits escapes with CR CR LF; rejoin them
        return buf.replace(b"\r\r\n", b"").decode("utf-8", "replace")

    fixture2 = os.path.normpath(os.path.join(os.path.dirname(fixture), "luogu2.md"))
    dump2 = subprocess.run(argv + ["--dump", "--width=80", fixture2],
                           capture_output=True).stdout.decode("utf-8", "replace")
    check(fails, "标题公式" in dump2 and "内层" in dump2 and "引言文字亮一点。" in dump2,
          "round-17 fixture dumps cleanly (titles, epigraph)")
    frame2 = capture(fixture2, need=b"int main")

    plain = re.sub(r"\x1b\[[0-9;?]*[a-zA-Z]|\x1b[_\]][^\x07\x1b]*(\x07|\x1b\\)?",
                   "", frame2)
    # wine's console can split an SGR in two; drop orphan "12;34;56m" leftovers
    plain = re.sub(r"(?<![0-9])(?:\d+;)+\d+m", "", plain)
    check(fails, " 1 int main() {" in plain, "luogu code block shows line numbers")
    pos = frame2.find("内内容")
    seg = frame2[max(0, pos - 300):pos] if pos >= 0 else ""
    iw, isucc = seg.rfind("38;2;224;175;104"), seg.rfind("38;2;158;206;106")
    check(fails, pos >= 0 and iw != -1 and isucc != -1 and iw < isucc,
          "nested bars keep per-level colours (warning outer, success inner)")
    me = re.search(r"\x1b\[([0-9;]*)m引言文字", frame2)
    ma = re.search(r"\x1b\[([0-9;]*)m——某人", frame2)
    ok = bool(me) and bool(ma) and me.group(1) != ma.group(1) \
        and not me.group(1).startswith("0;2;") and not ma.group(1).startswith("0;2;")
    check(fails, ok, "epigraph is bright: no DIM, content colour differs from attribution")

    if fails:
        print("luogu checks failed:", ", ".join(fails))
        return 1
    print("luogu syntax ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
