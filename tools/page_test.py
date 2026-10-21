#!/usr/bin/env python3
"""The page renderer (--render=image) in a pty, checked pixel by pixel.

    python3 tools/page_test.py [binary]      # default: ./build/mdt

kitty: every strip mdt transmits is decoded and placed where mdt put it, which
rebuilds the screen exactly as the terminal would show it.  That picture has to
equal `--screenshot --render=image` of the same document (same code, same
fonts), and after one line of scrolling it has to be the old picture moved up
by a row - while transmitting at most a couple of new strips, because every
other row is already cached in the terminal.  sixel and iTerm2: one strip per
row, nothing re-encoded when scrolling, status bar left to the text grid.
Without a graphics protocol or a cell size the mode refuses and says why.
"""
import os, pty, re, select, struct, fcntl, termios, time, sys, zlib, base64, tempfile, subprocess

BIN = sys.argv[1] if len(sys.argv) > 1 else "./build/mdt"
SLOWDOWN = float(os.environ.get("MDT_TEST_SLOWDOWN", "1") or 1)
COLS, ROWS, CW, CH = 90, 40, 9, 19
DOC = "demo/demo.md"
fails = []


def ok(msg):
    print("  ok  " + msg)


def run(args, keys, env_extra=None, settle=2.5):
    """Runs mdt, sends each key after the screen has been quiet, returns the
    output split per key: chunks[0] = before the first key, chunks[i] = after key i."""
    env = dict(os.environ)
    env["MDT_LANG"] = "en"
    env.update({"TERM": "xterm-256color", "COLORTERM": "truecolor"})
    env.update(env_extra or {})
    pid, fd = pty.fork()
    if pid == 0:
        os.execvpe(args[0], args, env)
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", ROWS, COLS, 0, 0))
    chunks = [b""]

    def drain(quiet):
        last = time.time()
        end = time.time() + 20 * SLOWDOWN
        while time.time() < end:
            r, _, _ = select.select([fd], [], [], 0.05)
            if r:
                try:
                    d = os.read(fd, 1 << 16)
                except OSError:
                    return False
                if not d:
                    return False
                chunks[-1] += d
                last = time.time()
            elif time.time() - last > quiet:
                return True
        return True

    drain(settle * SLOWDOWN)
    for k in keys:
        chunks.append(b"")
        os.write(fd, k)
        drain(1.0 * SLOWDOWN)
    chunks.append(b"")      # what quitting writes is not part of the last key
    os.write(fd, b"q")
    drain(0.5)
    try:
        os.close(fd)
    except OSError:
        pass
    try:
        os.waitpid(pid, 0)
    except ChildProcessError:
        pass
    return chunks


def png_decode(data):
    i, idat, w, h = 8, b"", 0, 0
    while i < len(data):
        n, = struct.unpack(">I", data[i:i + 4])
        t, c = data[i + 4:i + 8], data[i + 8:i + 8 + n]
        if t == b"IHDR":
            w, h = struct.unpack(">II", c[:8])
        elif t == b"IDAT":
            idat += c
        i += 12 + n
    raw = zlib.decompress(idat)
    stride, bpp, pos = w * 4, 4, 0
    prev, rows = bytearray(stride), []
    for _ in range(h):
        f = raw[pos]; pos += 1
        line = bytearray(raw[pos:pos + stride]); pos += stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if f == 1: line[x] = (line[x] + a) & 255
            elif f == 2: line[x] = (line[x] + b) & 255
            elif f == 3: line[x] = (line[x] + ((a + b) >> 1)) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else (b if pb <= pc else c))) & 255
        rows.append(bytes(line))
        prev = line
    return w, h, rows


class Kitty:
    """Just enough of a kitty terminal: images, placements, the cursor row."""
    TOKEN = re.compile(rb"\x1b_G([^;\x1b]*)(?:;([^\x1b]*))?\x1b\\|\x1b\[(\d+);(\d+)H")

    def __init__(self):
        self.images = {}      # id -> list of pixel rows (one strip)
        self.pending = {}     # id -> base64 chunks
        self.cur = None
        self.placed = {}      # (id, p) -> screen row
        self.cursor_row = 0
        self.sent = 0
        self.strips = set()   # ids that are page strips (not the text mode's pictures)

    def feed(self, data):
        for m in self.TOKEN.finditer(data):
            if m.group(3):
                self.cursor_row = int(m.group(3)) - 1
                continue
            ctrl = dict(kv.split(b"=", 1) for kv in m.group(1).split(b",") if b"=" in kv)
            payload = m.group(2) or b""
            a = ctrl.get(b"a")
            if a == b"t" or (a is None and self.cur is not None):
                if a == b"t":
                    self.cur = int(ctrl[b"i"])
                    self.pending[self.cur] = b""
                self.pending[self.cur] += payload
                if ctrl.get(b"m", b"0") == b"0":
                    w, h, rows = png_decode(base64.b64decode(self.pending.pop(self.cur)))
                    self.images[self.cur] = rows
                    if (w, h) == (COLS * CW, CH):
                        self.strips.add(self.cur)
                    self.sent += 1
                    self.cur = None
            elif a == b"p":
                self.placed[(int(ctrl[b"i"]), int(ctrl.get(b"p", b"1")))] = self.cursor_row
            elif a == b"d":
                d = ctrl.get(b"d", b"a")
                if d in (b"a", b"A"):
                    self.placed.clear()
                elif d in (b"i", b"I"):
                    iid = int(ctrl[b"i"])
                    pp = int(ctrl[b"p"]) if b"p" in ctrl else None
                    for key in list(self.placed):
                        if key[0] == iid and (pp is None or key[1] == pp):
                            del self.placed[key]
                    if d == b"I":
                        self.images.pop(iid, None)

    def screen(self):
        """Pixel rows of the screen: one list entry per pixel row (None = nothing)."""
        out = [None] * (ROWS * CH)
        for (iid, _), row in sorted(self.placed.items(), key=lambda kv: kv[0]):
            strip = self.images.get(iid)
            if strip is None:
                continue
            for y, line in enumerate(strip):
                if 0 <= row * CH + y < len(out):
                    out[row * CH + y] = line
        return out


def status_text(data):
    """The last row of a text screen after `data` (a tiny VT: cursor moves and
    printable characters; mdt only rewrites the cells that changed, so the
    status bar has to be put together, not grepped)."""
    grid = [[" "] * COLS for _ in range(ROWS)]
    r = c = 0
    tok = re.compile(r"\x1b\[([0-9;?]*)([A-Za-z])|\x1b[_P\]].*?(?:\x1b\\|\x07)|\x1b.|([^\x1b])", re.S)
    for m in tok.finditer(data.decode("utf-8", "replace")):
        if m.group(2) == "H":
            p = (m.group(1) or "1;1").split(";")
            r, c = int(p[0] or 1) - 1, int(p[1] if len(p) > 1 and p[1] else 1) - 1
        elif m.group(3) and m.group(3) >= " ":
            if 0 <= r < ROWS and 0 <= c < COLS:
                grid[r][c] = m.group(3)
            c += 1
    return "".join(grid[ROWS - 1])


def rows_equal(a, b, first, count):
    bad = [r for r in range(first, first + count)
           if a[r * CH:(r + 1) * CH] != b[r * CH:(r + 1) * CH]]
    return bad


# ---------------------------------------------------------------- kitty ----
base = [BIN, "--gfx=kitty", "--render=image", "--cell=%dx%d" % (CW, CH), "--no-scrollbar"]
chunks = run(base + [DOC], [b"j", b"p", b"p"], {"KITTY_WINDOW_ID": "1"})
k = Kitty()
k.feed(chunks[0])
first = k.screen()
if b"a=t,f=100" not in chunks[0]:
    fails.append("kitty: no strips were transmitted")
if any(r is None for r in first):
    fails.append("kitty: %d pixel rows of the screen are not covered by a strip"
                 % sum(r is None for r in first))
else:
    ok("kitty: %d strips cover all %d rows, status bar included" % (k.sent, ROWS))

# the same frame, rendered by --screenshot
with tempfile.TemporaryDirectory() as td:
    shot = os.path.join(td, "s.png")
    subprocess.run([BIN, "--render=image", "--screenshot=" + shot, "--width=%d" % COLS,
                    "--height=%d" % ROWS, "--cell=%dx%d" % (CW, CH), "--no-scrollbar", DOC],
                   check=True, stdout=subprocess.DEVNULL, env=dict(os.environ, MDT_LANG="en"))
    sw, sh, srows = png_decode(open(shot, "rb").read())
if (sw, sh) != (COLS * CW, ROWS * CH):
    fails.append("screenshot is %dx%d, expected %dx%d" % (sw, sh, COLS * CW, ROWS * CH))
elif None not in first:
    bad = rows_equal(first, srows, 0, ROWS - 1)  # the status bars say different things
    if bad:
        fails.append("kitty: rows %s differ from --screenshot --render=image" % bad[:8])
    else:
        ok("kitty: the rebuilt screen equals --screenshot --render=image pixel for pixel")
    # enlarged headings: the first heading's glyphs reach well into the second row
    ink = sum(1 for y in range(CH, 2 * CH) for x in range(3 * CW, 20 * CW)
              if srows[y][x * 4:x * 4 + 3] != srows[2 * CH + CH // 2][4:7])
    if ink < 200:
        fails.append("headings are not enlarged (%d ink pixels in the heading's second row)" % ink)
    else:
        ok("level 1 heading drawn twice the size (%d ink pixels in its second row)" % ink)

# one line down: everything moves up a row, almost nothing is transmitted
sent_before = k.sent
k.feed(chunks[1])
second = k.screen()
new = k.sent - sent_before
if None not in first and None not in second:
    bad = [r for r in range(ROWS - 2)
           if second[r * CH:(r + 1) * CH] != first[(r + 1) * CH:(r + 2) * CH]]
    if bad:
        fails.append("kitty: after j, rows %s are not the old rows moved up" % bad[:8])
    else:
        ok("kitty: after j the screen is the old one moved up a row")
if new > 3:
    fails.append("kitty: scrolling one line transmitted %d strips (cache unused)" % new)
else:
    ok("kitty: scrolling one line transmitted %d new strip(s), the rest came from the cache" % new)

# p: back to text - the strips are deleted and text is drawn again
k.feed(chunks[2])
left = [key for key in k.placed if key[0] in k.strips]
if left:
    fails.append("kitty: %d strips still placed after p (text mode)" % len(left))
elif b"page drawn as text" not in chunks[2]:
    fails.append("kitty: p did not say it switched to text")
else:
    ok("kitty: p switches to text and deletes every strip")
k.feed(chunks[3])
back = {row for key, row in k.placed.items() if key[0] in k.strips}
if back != set(range(ROWS)):
    fails.append("kitty: p again did not bring the page renderer back (rows %s)" % sorted(back)[:5])
else:
    ok("kitty: p again brings it back (all %d rows placed)" % len(back))

# ------------------------------------------------------- sixel / iTerm2 ----
for proto, start, extra in (("sixel", rb"\x1bP0;1q", {}),
                            ("iterm2", rb"\x1b]1337;File=inline=1;", {"TERM_PROGRAM": "iTerm.app"})):
    ch = run([BIN, "--gfx=" + proto, "--render=image", "--cell=%dx%d" % (CW, CH),
              "--no-scrollbar", DOC], [b"j"], extra)
    pat = re.compile(rb"\x1b\[(\d+);1H(" + start + rb".*?(?:\x1b\\|\x07))", re.S)
    f1 = {int(m.group(1)): m.group(2) for m in pat.finditer(ch[0])}
    f2 = {int(m.group(1)): m.group(2) for m in pat.finditer(ch[1])}
    if sorted(f1) != list(range(1, ROWS)):
        fails.append("%s: strips on rows %s, expected 1..%d (status bar stays text)"
                     % (proto, sorted(f1)[:6], ROWS - 1))
    else:
        ok("%s: one strip per row on rows 1..%d, the status bar is text" % (proto, ROWS - 1))
    fresh = set(f2.values()) - set(f1.values())
    if len(fresh) > 3:
        fails.append("%s: scrolling one line encoded %d new strips" % (proto, len(fresh)))
    else:
        ok("%s: scrolling one line encoded %d new strip(s)" % (proto, len(fresh)))
    if proto == "sixel":
        sizes = {m.group(0) for m in re.finditer(rb'"1;1;(\d+);(\d+)', ch[0])}
        if sizes != {b'"1;1;%d;%d' % (COLS * CW, CH)}:
            fails.append("sixel: strips are not %dx%d: %s" % (COLS * CW, CH, sorted(sizes)[:3]))

# ---------------------------------------------------------- refusals ----
ch = run([BIN, "--gfx=none", "--render=image", DOC], [])
txt = b"".join(ch[:-1])
if "page renderer off: no graphics protocol" not in status_text(txt) or b"a=t," in txt:
    fails.append("no graphics: the page renderer did not refuse with a reason")
else:
    ok("no graphics protocol: refuses, says why, reads as text")
ch = run([BIN, "--gfx=kitty", "--render=image", DOC], [], {"KITTY_WINDOW_ID": "1"})
txt = b"".join(ch[:-1])
if "page renderer off: cell size unknown" not in status_text(txt) or b",C=1," in txt:
    fails.append("unknown cell size: the page renderer did not refuse with a reason")
else:
    ok("unknown cell size: refuses and points at --cell")

if fails:
    print("FAIL:")
    for f in fails:
        print("  - " + f)
    sys.exit(1)
print("page renderer checks passed")
