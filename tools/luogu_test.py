#!/usr/bin/env python3
"""Checks the Luogu remark-directive syntax (help: luogu.com.cn/article/70w8j2pj)

    python3 tools/luogu_test.py ./build/mdt
    python3 tools/luogu_test.py build/win/mdt.exe wine64

Structure checks run against --dump (plain text, so they work everywhere);
the colour checks (callout bars, highlighted code lines) run in a pty.
"""
import base64
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
    dump = dump.replace("\r\n", "\n")   # wine's exe ends lines with CRLF
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
    # round 24: booktabs styles breathe like the Luogu site - a blank
    # separator line between every pair of body rows
    check(fails, re.search(r"\n 1~2[^\n]*\n *\n 3~4", dump) is not None,
          "three-line table keeps a blank line between the rows")
    if len(heavy) >= 2:
        seg = lines[heavy[0]:heavy[1] + 1]
        check(fails, not any("│" in l for l in seg), "three-line table has no verticals")

    # ---- cute-table{tuack=2} ------------------------------------------------
    check(fails, any("┃" in l for l in lines), "tuack heavy vertical after column 2")
    # tuack separator: thin rule, broken where a merge spans it, the heavy
    # vertical crossing it as ┿, and the merged text centred on that line
    check(fails, re.search(r"\n│ +│2 +┃3 +│4 +│\n│ 合并 │ +┃ +│ +│\n│ +│5 +┃6 +│7 +│",
                           dump) is not None,
          "tuack rows get a separator rule; merges break it and centre on it")

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
    out = out.replace(b"\r\r\n", b"")   # wine's console wrapper splits escapes
    frame = out.decode("utf-8", "replace")
    check(fails, "38;2;126;197;255" in frame, "info callout bar is blue")
    check(fails, "38;2;158;206;106" in frame, "success callout bar is green")
    check(fails, "38;2;224;175;104" in frame, "warning callout bar is yellow")
    # info (126,197,255) mixed 16% into the (24,26,31) background = 40,53,66
    check(fails, "48;2;40;53;66" in frame, "callout title sits on a light background band")
    # a nested title row: the parent bar keeps the plain background (no tint
    # bleed), the child's own bar stands inside the wash
    pos = frame.find("我是子容器 1")
    seg = frame[max(0, pos - 300):pos] if pos >= 0 else ""
    check(fails, "38;2;224;175;104;48;2;24;26;31" in seg
          and "38;2;158;206;106;48;2;45;54;43" in seg,
          "nested title: parent bar on plain bg, child bar inside the wash")
    # lines=2-3,5 get a warm tint over the theme background {24,26,31}
    check(fails, "48;2;50;47;39" in frame, "code lines 2-3,5 are highlighted")

    # ---- round-17 features (second fixture) ---------------------------------
    def capture(path, rows=40, need=None, gfx="--gfx=none"):
        pid, fd = ptymod.fork()
        if pid == 0:
            env = dict(os.environ)
            env["TERM"] = "xterm-256color"
            os.execvpe(argv[0], argv + [gfx, path], env)
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
    dump2 = dump2.replace("\r\n", "\n")   # wine's exe ends lines with CRLF
    check(fails, "标题公式" in dump2 and "内层" in dump2 and "引言文字亮一点。" in dump2,
          "round-17 fixture dumps cleanly (titles, epigraph)")
    # merged block: empty first row of the span, content centred in the middle
    check(fails, re.search(r"\n│ +│\n│ +跨列合并 +│", dump2) is not None,
          "merged block centres its content in the whole big cell")
    # round 23: a full grid - a rule between every pair of body rows
    check(fails, re.search(r"\n│1~2[^\n]+\n├[^\n]+┤\n│3~4", dump) is not None,
          "tables get a horizontal rule between every row")
    check(fails, re.search(r"\n│1 +│2 +│\n├─+┼─+┤\n│3 +│4 +│", dump2) is not None,
          "plain tables get a full grid (┼ junctions between the rows)")
    # an even (two-row) merged span centres its content on the middle
    # separator line instead of hugging the top row
    check(fails, re.search(r"\n│ +│\n│ +双行合并 +│\n│ +│\n├─+", dump2) is not None,
          "even merged spans centre on the separator line")
    # hard line breaks: two trailing spaces and a trailing backslash
    check(fails, re.search(r"\n 硬换行甲\n 硬换行乙\n 硬换行丙\n", dump2) is not None,
          "trailing two spaces / backslash make a hard line break")
    # Luogu writes "$n\le $": a trailing space inside inline maths is fine
    # when the body is LaTeX, while prose dollars stay literal
    check(fails, "n≤" in dump2 and "x²" in dump2 and "$5 and $6" in dump2,
          "inline maths with a trailing space render; prose dollars stay text")
    # ...but inside a merged span the rules stay invisible and the exit row
    # reconnects with ┬ junctions
    check(fails, re.search(r"\n│ +跨列合并 +│\n(?:│ +│\n)+├─+┬─+┬─+┤\n│己", dump2)
          is not None,
          "merged span: no rule inside, ┬ junctions where the block ends")
    # "^ | ^ | ^" under a colspan reads exactly like "^ | < | <": one big
    # clean cell, centred content, no rule stubs (╴╵╶╷) anywhere
    check(fails, re.search(r"\n│ +大块 +│\n(?:│ +│\n)+├─+┬─+┬─+┤\n│x", dump2)
          is not None and not re.search(r"[╴╵╶╷]", dump2),
          "^-only continuation rows merge into the same clean big cell")
    frame2 = capture(fixture2, rows=50, need=b"int main")

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
    m = re.search(r"\x1b\[[0-9;]*48;2;(\d+);(\d+);(\d+)m[^]*code", frame2)
    tinted = m and (int(m.group(1)) > 24 or int(m.group(2)) > 26 or int(m.group(3)) > 31) \
        and int(m.group(1)) < 70
    check(fails, bool(tinted), "inline code sits on a very light wash")
    # a title whose formula reserves a second row keeps the wash there and the
    # bar runs through both rows (success tint of (158,206,106) = 45,54,43)
    check(fails, re.search(r"48;2;40;53;66m[^\x1b]*√", frame2) is not None
          and frame2.count("38;2;126;197;255;48;2;40;53;66") >= 2,
          "title formula row keeps the wash and the bar continues")

    # ---- display maths in a callout title (third fixture) ------------------
    fixture3 = os.path.normpath(os.path.join(os.path.dirname(fixture), "luogu3.md"))
    frame3 = capture(fixture3)
    # the formula line is a Line::Image: it still owes the wash and the bar
    check(fails, "38;2;158;206;106;48;2;45;54;43" in frame3
          and "48;2;45;54;43" in frame3,
          "display-math title row carries the wash and the callout bar")
    # sixel has no alpha: the formula bitmap must be composited over the wash
    # (45,54,43 -> 18;21;17 percent) instead of the dark page background
    frame3s = capture(fixture3, gfx="--gfx=sixel")
    check(fails, re.search(r"#\d+;2;18;21;17", frame3s) is not None
          and not re.search(r"#\d+;2;9;10;12", frame3s),
          "sixel formula bitmap is composited over the title wash")

    # ---- round-23: clicking a link shows a dialog; c copies the URL --------
    url = "https://www.luogu.com.cn"
    d100 = subprocess.run(argv + ["--dump", "--width=100", fixture2],
                          capture_output=True).stdout.decode("utf-8", "replace")
    d100 = d100.replace("\r\n", "\n")
    import unicodedata

    def disp_col(s, j):  # display column of character index j (CJK = 2 wide)
        return 1 + sum(2 if unicodedata.east_asian_width(ch) in "WF" else 1
                       for ch in s[:j])

    lr = lc = -1
    for i, s in enumerate(d100.splitlines()):
        j = s.find("洛谷")
        if j >= 0:
            lr, lc = i, disp_col(s, j)
            break
    if lr < 0:
        check(fails, False, "link fixture line found in the dump")
    else:
        pid, fd = ptymod.fork()
        if pid == 0:
            env = dict(os.environ)
            env["TERM"] = "xterm-256color"
            os.execvpe(argv[0], argv + ["--gfx=none", fixture2], env)
        fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", 50, 100, 0, 0))
        acc = b""

        def read_until(needle, timeout=12.0):
            nonlocal acc
            t0 = time.time()
            start = len(acc)
            while time.time() - t0 < timeout:
                r, _, _ = select.select([fd], [], [], 0.2)
                if r:
                    try:
                        chunk = os.read(fd, 1 << 20)
                    except OSError:
                        break
                    if not chunk:
                        break
                    acc += chunk
                if needle in acc[start:].replace(b"\r\r\n", b"") \
                        and time.time() - t0 > 0.4:
                    break
            return acc[start:].replace(b"\r\r\n", b"")

        read_until(b"for help")
        os.write(fd, ("\x1b[<0;%d;%dM\x1b[<0;%d;%dm" % (lc + 1, lr + 1, lc + 1, lr + 1)).encode())
        box = read_until(b"open in browser")
        plainb = re.sub(r"\x1b\[[0-9;?]*[a-zA-Z]|\x1b[_\]][^\x07\x1b]*(\x07|\x1b\\)?",
                        "", box.decode("utf-8", "replace"))
        check(fails, url in plainb and "复制" in plainb and "浏览器打开" in plainb
              and "关闭" in plainb,
              "clicking a link pops a dialog with Chinese copy/open/close buttons")
        os.write(fd, b"c")
        clip = read_until(b"]52;c;")
        b64 = base64.b64encode(url.encode()).decode()
        check(fails, b64 in clip.decode("utf-8", "replace"),
              "c copies the URL to the clipboard (OSC 52)")
        # Esc closes the dialog; wine's console may swallow the bare ESC, in
        # which case the first click closes it - so a second click must bring
        # the dialog back either way.
        click = ("\x1b[<0;%d;%dM\x1b[<0;%d;%dm" % (lc + 1, lr + 1, lc + 1, lr + 1)).encode()
        os.write(fd, b"\x1b")
        read_until(b"for help", 3.0)
        os.write(fd, click)
        hint = "浏览器打开".encode()
        again = read_until(hint, 8.0)   # wine can be slow
        if hint not in again:
            os.write(fd, click)
            again += read_until(hint, 8.0)
        check(fails, hint in again,
              "Esc (or a click) closes the dialog and a click reopens it")
        # the buttons are clickable: copy button -> OSC 52, close button -> gone
        def dw(t):  # display width (CJK = 2)
            import unicodedata
            return sum(2 if unicodedata.east_asian_width(ch) in "WF" else 1 for ch in t)

        labels = ["c 复制", "o 浏览器打开", "Esc 关闭"]
        uw = dw(url)
        pw = min(100 - 4, max(46, uw + 6))
        ulines = max(1, -(-uw // (pw - 4)))
        ph = min(50 - 2, ulines + 6)
        px, py = (100 - pw) // 2, (50 - ph) // 2
        by = py + 3 + ulines
        bws = [dw(t) + 2 for t in labels]
        x = px + max(2, (pw - sum(bws) - 4) // 2)
        bx = []
        for w in bws:
            bx.append(x)
            x += w + 2
        os.write(fd, ("\x1b[<0;%d;%dM\x1b[<0;%d;%dm" % (bx[0] + 2, by + 1, bx[0] + 2, by + 1)).encode())
        clip2 = read_until(b"]52;c;")
        check(fails, b64 in clip2.decode("utf-8", "replace"),
              "clicking the copy button puts the URL on the clipboard")
        os.write(fd, ("\x1b[<0;%d;%dM\x1b[<0;%d;%dm" % (bx[2] + 2, by + 1, bx[2] + 2, by + 1)).encode())
        read_until(b"for help", 2.0)
        os.write(fd, ("\x1b[<0;%d;%dM\x1b[<0;%d;%dm" % (lc + 1, lr + 1, lc + 1, lr + 1)).encode())
        again2 = read_until("浏览器打开".encode(), 6.0)
        check(fails, "浏览器打开" in again2.decode("utf-8", "replace"),
              "the close button dismisses the dialog (a link click reopens it)")
        os.write(fd, b"\x1b")
        read_until(b"for help", 2.0)

        # a child process sharing the tty (xdg-open's fallback browser, a
        # pager, ...) may cook it: mdt must take raw mode back by itself.
        # (wine's console layer sits below the pty discipline, so poking the
        # line discipline here would not reach the Windows console modes)
        if not wine:
            attrs = termios.tcgetattr(fd)
            attrs[3] = attrs[3] | termios.ECHO | termios.ICANON
            termios.tcsetattr(fd, termios.TCSANOW, attrs)
            time.sleep(1.0)
            cur = termios.tcgetattr(fd)
            check(fails, not (cur[3] & termios.ECHO) and not (cur[3] & termios.ICANON),
                  "mdt re-asserts raw mode after another process cooks the tty")
        os.write(fd, b"q")
        try:
            os.close(fd)
            os.kill(pid, 15)
            os.waitpid(pid, 0)
        except (OSError, ChildProcessError):
            pass

    if fails:
        print("luogu checks failed:", ", ".join(fails))
        return 1
    print("luogu syntax ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
