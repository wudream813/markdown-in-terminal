#!/usr/bin/env python3
"""A slow picture host must not freeze the reader.

Opening a document that links to three pictures on a slow CDN used to block the
event loop for the whole download (17 s for the 题解 in the bug report), so
scrolling, '?' and Esc all looked dead.  Remote images are now downloaded on a
worker thread: the first frame appears immediately with a placeholder, the keys
keep working, and the picture is drawn as soon as it arrives.

    python3 tools/async_image_test.py ./build/mdt
    python3 tools/async_image_test.py build/win/mdt.exe /usr/lib/wine/wine64
"""
import fcntl
import http.server
import os
import pty
import re
import select
import struct
import sys
import tempfile
import termios
import threading
import time

BIN = sys.argv[1] if len(sys.argv) > 1 else "./build/mdt"
RUNNER = sys.argv[2].split() if len(sys.argv) > 2 else []
# Wine needs longer to start, and its console driver does not hand a bare Escape
# or an arrow key to the program at all (tools/wine_pty_test.py avoids them for
# the same reason), so those two keys are only checked on a real terminal.
WINE = bool(RUNNER)
ROWS, COLS, CW, CH = 24, 90, 10, 20
DELAY = 1.2          # seconds the fake host needs per request
FIRST_FRAME_MAX = 2.5 if WINE else 1.0
KEY_MAX = 1.0 if WINE else 0.5


def tiny_png(rgb=(90, 140, 200), w=16, h=16):
    """A real (zlib-valid) PNG: a fake one makes the loader fail and hides bugs."""
    import binascii
    import struct
    import zlib

    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", binascii.crc32(c) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + bytes(list(rgb) + [255]) * w for _ in range(h))
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw))
            + chunk(b"IEND", b""))


class Handler(http.server.BaseHTTPRequestHandler):
    hits = []

    def do_GET(self):
        Handler.hits.append((self.path, time.time()))
        time.sleep(DELAY)
        # distinct colours: mdt reuses a transmitted bitmap for identical pixels,
        # and this test wants two real transmissions.
        body = tiny_png((200, 90, 90) if "one" in self.path else (90, 200, 90))
        self.send_response(200)
        self.send_header("Content-Type", "image/png")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *a):
        pass


server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
port = server.server_address[1]
threading.Thread(target=server.serve_forever, daemon=True).start()

tmp = tempfile.mkdtemp()
frame_log = os.path.join(tmp, "frames.log")
doc = os.path.join(tmp, "doc.md")
with open(doc, "w") as f:
    f.write("# slow pictures\n\ntext before\n\n"
            "![](http://127.0.0.1:%d/one.png)\n\n"
            "![](http://127.0.0.1:%d/two.png)\n\n"
            "text after\n" % (port, port))

pid, fd = pty.fork()
if pid == 0:
    os.environ["TERM"] = "xterm-kitty"
    os.environ["KITTY_WINDOW_ID"] = "1"
    # what mdt sent, byte for byte (Wine's console rewrites the pty stream)
    os.environ["MDT_FRAME_LOG"] = frame_log
    if os.environ.get("MDT_TEST_DEBUG"):
        os.environ["MDT_DEBUG_IMAGE"] = "1"
        err = os.open("/tmp/sol/test_err.log", os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o644)
        os.dup2(err, 2)
    argv = RUNNER + [BIN] if RUNNER else [BIN]
    os.execvp(argv[0], argv + ["--gfx=kitty", doc])
fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", ROWS, COLS, 0, 0))

buf = b""
start = time.time()
answered = False
first_frame_at = None
fail = []


def read_first(deadline):
    """Wait for one byte of output; returns the latency in seconds."""
    global buf, answered
    while time.time() < deadline:
        r, _, _ = select.select([fd], [], [], 0.05)
        if not r:
            continue
        try:
            chunk = os.read(fd, 1 << 20)
        except OSError:
            return None
        if chunk:
            buf += chunk
            if not answered and b"\x1b[16t" in buf:
                os.write(fd, b"\x1b[6;%d;%dt" % (CH, CW))
                os.write(fd, b"\x1b[4;%d;%dt" % (ROWS * CH, COLS * CW))
                answered = True
            return time.time() - deadline  # deadline was "now + timeout"
    return None


def read_until(deadline, note_first_frame=True):
    global buf, answered, first_frame_at
    while time.time() < deadline:
        r, _, _ = select.select([fd], [], [], 0.05)
        if not r:
            continue
        try:
            chunk = os.read(fd, 1 << 20)
        except OSError:
            return
        if not chunk:
            return
        buf += chunk
        if not answered and b"\x1b[16t" in buf:
            os.write(fd, b"\x1b[6;%d;%dt" % (CH, CW))
            os.write(fd, b"\x1b[4;%d;%dt" % (ROWS * CH, COLS * CW))
            answered = True
        if note_first_frame and first_frame_at is None and b"slow pictures" in buf:
            first_frame_at = time.time()


read_until(start + FIRST_FRAME_MAX + 0.5)
first = (first_frame_at or 0) - start
if first_frame_at is None:
    fail.append("no first frame within %.1fs" % (FIRST_FRAME_MAX + 0.5))
else:
    print("   first frame after %.2fs" % first)
    if first > FIRST_FRAME_MAX:
        fail.append("the first frame took %.2fs (a slow download blocked it)" % first)

# keys must work while the downloads are still running
# Scrolling during the download is the probe: it redraws the page, which is
# exactly what used to take three seconds per key.  On Wine the help panel is
# left for after the pictures arrive, because a bare Escape does not come back
# from Wine's console driver and an open panel would hide the very pictures
# this test is waiting for.
keys_to_try = [("j", b"j")] if WINE else [("?", b"?"), ("Esc", b"\x1b"), ("down", b"\x1b[B")]
for label, keys in keys_to_try:
    n0 = len(buf)
    t = time.time()
    os.write(fd, keys)
    lat = None
    deadline = t + KEY_MAX
    while lat is None and time.time() < deadline:
        got = read_first(deadline)
        if got is None:
            break
        lat = time.time() - t
    grew = len(buf) - n0
    if lat is None:
        print("   %-4s no answer within %.2fs" % (label, KEY_MAX))
        fail.append("'%s' did not answer within %.1fs while images were loading" % (label, KEY_MAX))
    else:
        print("   %-4s answered after %.2fs (%d bytes)" % (label, lat, grew))
        if lat > KEY_MAX:
            fail.append("'%s' took %.2fs to answer while images were loading" % (label, lat))
    read_until(time.time() + 0.1, note_first_frame=False)

# wait for the downloads to finish and land on screen
read_until(time.time() + 3 * DELAY + 3.0, note_first_frame=False)
placements = len(re.findall(rb"a=p", buf))


def frame_log_bytes():
    if not os.path.exists(frame_log):
        return b""
    with open(frame_log, "rb") as f:
        return f.read()


# the log is written through stdio, so give the last frame a moment to land
sent = frame_log_bytes()
transmits = sent.count(b"a=t")
deadline = time.time() + 4.0
while transmits < 2 and time.time() < deadline:
    read_until(time.time() + 0.25, note_first_frame=False)
    sent = frame_log_bytes()
    transmits = sent.count(b"a=t")
print("   %d image transmissions in the frame log, %d placements seen on the wire"
      % (transmits, placements))
if transmits < 2:
    fail.append("only %d image transmissions: the pictures never arrived on screen" % transmits)

# the help panel still opens with the pictures on screen
if WINE:
    n0 = len(buf)
    t = time.time()
    os.write(fd, b"?")
    lat = None
    deadline = t + KEY_MAX
    while lat is None and time.time() < deadline:
        if read_first(deadline) is None:
            break
        lat = time.time() - t
    print("   ?    answered after %.2fs (help panel)" % (lat if lat else -1))
    if lat is None:
        fail.append("'?' did not answer after the pictures arrived")

hits = [h[0] for h in Handler.hits]
print("   the host was asked for %s" % sorted(set(hits)))
if len(hits) != len(set(hits)):
    fail.append("a picture was downloaded more than once: %s" % hits)

try:
    os.write(fd, b"q")
except OSError:
    pass
for _ in range(30):  # the log is buffered: wait for a clean exit
    try:
        done, _ = os.waitpid(pid, os.WNOHANG)
    except ChildProcessError:
        break
    if done:
        break
    time.sleep(0.1)
try:
    os.kill(pid, 9)
except ProcessLookupError:
    pass
try:
    os.waitpid(pid, 0)
except ChildProcessError:
    pass
server.shutdown()

if fail:
    for f in fail:
        print("   FAIL " + f)
    sys.exit(1)
print("   ok   a slow picture host does not block the reader")
