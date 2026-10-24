#!/usr/bin/env python3
"""Opens a document in a pty and times what a reader feels.

    python3 tools/big_bench.py ./build/mdt /tmp/big/mixed10.md
    BENCH_KEYS=j,G,t python3 tools/big_bench.py ./build/mdt doc.md

Prints the time to the first screen, then for every key the time until the
output goes quiet again (and until its first byte arrived), then the peak RSS of the reader.  BENCH_KEYS: none,
nav (the default list without t and r) or a comma-separated list.  A dev
tool, not a test: the numbers depend on the machine.
"""
import os, pty, select, struct, sys, termios, fcntl, time
BIN, DOC = sys.argv[1], sys.argv[2]
keys = [b"j", b" ", b" ", b"G", b"g", b"J", b"/Section 9999\r", b"n", b"\t", b"\x1b", b"t", b"r"]
k = os.environ.get("BENCH_KEYS")
if k == "none": keys = []
elif k == "nav": keys = keys[:10]
elif k: keys = [x.encode().decode("unicode_escape").encode() for x in k.split(",")]
pid, fd = pty.fork()
if pid == 0:
    os.environ.update(TERM="xterm-256color", MDT_LANG="en")
    os.execv(BIN, [BIN, DOC])
fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", 40, 120, 0, 0))
def settle(t0, quiet=0.4, limit=60, need=0):
    global first
    last = None; got = 0; first = None
    while time.time() - t0 < limit:
        r, _, _ = select.select([fd], [], [], quiet)
        if not r:
            if last is not None and got >= need: break
            continue
        try: d = os.read(fd, 1 << 16)
        except OSError: break
        if not d: break
        got += len(d); last = time.time()
        if first is None: first = last
    return (last or time.time()) - t0
t0 = time.time()
print(f"  first frame          {settle(t0, quiet=0.5, need=3000):.3f}")
for key in keys:
    t = time.time(); os.write(fd, key)
    quiet = settle(t)
    print(f"  {str(key):20s} {quiet:.3f}  (first byte {(first or t) - t:.3f})")
os.write(fd, b"q")
_, status, ru = os.wait4(pid, 0)
print(f"  peak RSS MB          {ru.ru_maxrss / 1024:.1f}")
