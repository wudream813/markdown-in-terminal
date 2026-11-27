#!/usr/bin/env python3
"""Remote images: never block the first frame, never show a stale reload.

Mode A (default): a remote .svg used to be downloaded synchronously on the
main thread before the first frame; the whole screen stayed blank while the
download ran. It must now go through the async loader like any raster image.

Mode B: after a reload (R) the cache is cleared, but an image download that
was already in flight used to write its result back afterwards - the screen
showed the stale image again until the fresh download replaced it. The result
of a download started before the reload must be dropped.

    python3 tools/remote_stale_test.py ./build/mdt [A|B]
"""
import http.server
import os
import pty
import re
import select
import socketserver
import struct
import sys
import fcntl
import termios
import threading
import time
import zlib

BIN = sys.argv[1] if len(sys.argv) > 1 else "./build/mdt"
MODE = "A"
if len(sys.argv) > 2 and sys.argv[2].upper() in ("A", "B"):
    MODE = sys.argv[2].upper()
COLS, ROWS = 80, 24
HITS = {"svg": 0, "img": 0}


def png_solid(w, h, rgb):
    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)
    raw = b"".join(b"\x00" + bytes(rgb) * w for _ in range(h))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) +
            chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def do_GET(self):
        if self.path == "/x.svg":
            HITS["svg"] += 1
            time.sleep(3)
            body = (b'<svg xmlns="http://www.w3.org/2000/svg" width="120" '
                    b'height="120"><rect width="36" height="36" fill="#e00"/>'
                    b"</svg>")
            self.send_response(200)
            self.send_header("Content-Type", "image/svg+xml")
        elif self.path == "/img.png":
            HITS["img"] += 1
            if HITS["img"] == 1:
                time.sleep(3)  # stale request in flight across the reload
                body = png_solid(16, 16, (220, 40, 40))
            else:
                time.sleep(2)  # land after the stale one so a frame can show it
                body = png_solid(32, 32, (40, 200, 80))
            self.send_response(200)
            self.send_header("Content-Type", "image/png")
        else:
            self.send_response(404)
            return
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True


srv = Server(("127.0.0.1", 0), Handler)
port = srv.server_address[1]
threading.Thread(target=srv.serve_forever, daemon=True).start()

if MODE == "A":
    doc = f"# slow remote svg\n\n![](http://127.0.0.1:{port}/x.svg)\n"
else:
    doc = f"# reload\n\n![](http://127.0.0.1:{port}/img.png)\n"
doc_path = f"/tmp/mdt_remote_{MODE.lower()}.md"
open(doc_path, "w").write(doc)

pid, fd = pty.fork()
if pid == 0:
    os.environ["TERM"] = "xterm-kitty"
    os.environ["KITTY_WINDOW_ID"] = "1"
    os.execvp(BIN, [BIN, "--gfx=kitty", doc_path])
fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", ROWS, COLS, 0, 0))

t0 = time.time()
first_out = b""
first_t = None
ids = set()
if MODE == "B":
    sent_reload = False
end = t0 + 9
while time.time() < end:
    r, _, _ = select.select([fd], [], [], 0.1)
    if fd in r:
        try:
            chunk = os.read(fd, 65536)
        except OSError:
            break
        if MODE == "A" and not first_out and (b"slow" in chunk or
                                              b"remote" in chunk):
            first_out = chunk
            first_t = time.time() - t0
        for m in re.finditer(rb"\x1b_G[a-zA-Z]=[^;]*,i=(\d+)[^;]*;", chunk):
            ids.add(m.group(1))
    if MODE == "B" and not sent_reload and time.time() - t0 > 0.5:
        os.write(fd, b"R")
        sent_reload = True
os.close(fd)
try:
    os.waitpid(pid, os.WNOHANG)
except OSError:
    pass
srv.shutdown()

if MODE == "A":
    if HITS["svg"] < 1:
        print("   FAIL the server never saw the svg request")
        sys.exit(1)
    if not first_out:
        print("   FAIL no frame at all was produced")
        sys.exit(1)
    if first_t > 2.5:
        print(f"   FAIL the first frame took {first_t:.1f}s - the slow "
              "download blocked the main thread")
        sys.exit(1)
    print(f"   ok first frame after {first_t:.1f}s while the slow svg was "
          "still downloading")
else:
    if HITS["img"] < 2:
        print("   FAIL the reload did not trigger a fresh download")
        sys.exit(1)
    if len(ids) != 1:
        print(f"   FAIL {len(ids)} distinct images were shown; the stale "
              "in-flight download wrote itself back after the reload")
        sys.exit(1)
    print("   ok only the fresh download was ever displayed")
