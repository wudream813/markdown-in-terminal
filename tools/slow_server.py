#!/usr/bin/env python3
"""A deliberately slow picture host, for testing the download path by hand.

    python3 tools/slow_server.py [port] [seconds]

Point a document at it and open the file in mdt:

    # slow-pictures.md
    ![](http://127.0.0.1:8731/picture.png)

With `--async` downloads (the interactive default) the page appears at once
with a `[loading…]` placeholder and every key keeps working; the picture is
drawn when the reply finally arrives.  `tools/async_image_test.py` checks the
same thing automatically.
"""
import binascii
import http.server
import struct
import sys
import time
import zlib

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8731
DELAY = float(sys.argv[2]) if len(sys.argv) > 2 else 1.5


def png(w=320, h=200, rgb=(90, 140, 200)):
    """A real PNG: a fake one would only test the error path."""
    def chunk(tag, data):
        body = tag + data
        return (struct.pack(">I", len(data)) + body
                + struct.pack(">I", binascii.crc32(body) & 0xFFFFFFFF))

    rows = []
    for y in range(h):
        row = bytes([min(255, rgb[0] + y // 3), min(255, rgb[1] + y // 3), min(255, rgb[2] + y // 3), 255])
        rows.append(b"\x00" + row * w)
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(b"".join(rows)))
            + chunk(b"IEND", b""))


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        sys.stderr.write("REQ %s at %.2f\n" % (self.path, time.time()))
        sys.stderr.flush()
        time.sleep(DELAY)
        body = png()
        self.send_response(200)
        self.send_header("Content-Type", "image/png")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *a):
        pass


if __name__ == "__main__":
    print("serving one picture at http://127.0.0.1:%d/picture.png (%.1fs per request)"
          % (PORT, DELAY), file=sys.stderr)
    http.server.ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
