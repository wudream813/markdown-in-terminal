#!/usr/bin/env python3
"""Drives mdt inside a pty and checks the interactive behaviour + graphics output."""
import os, pty, select, struct, fcntl, termios, time, sys, re

def run(cmd, env_extra, keys, cols=100, rows=30):
    env = dict(os.environ)
    env.update(env_extra)
    pid, fd = pty.fork()
    if pid == 0:
        os.execvpe(cmd[0], cmd, env)
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
    out = b""
    child_exited = False
    deadline = time.time() + 12
    key_i = 0
    next_key = time.time() + 1.2
    while time.time() < deadline:
        r, _, _ = select.select([fd], [], [], 0.1)
        if r:
            try:
                chunk = os.read(fd, 65536)
            except OSError:
                break
            if not chunk:
                break
            out += chunk
        if time.time() >= next_key and key_i < len(keys):
            os.write(fd, keys[key_i])
            key_i += 1
            next_key = time.time() + 0.45
        if key_i >= len(keys) and time.time() > next_key:
            # keep draining until the child exits (or 2s grace) so we capture the
            # restore-terminal sequence written on shutdown
            grace = time.time() + 4.0
            while time.time() < grace:
                r, _, _ = select.select([fd], [], [], 0.1)
                if r:
                    try:
                        chunk = os.read(fd, 65536)
                    except OSError:
                        chunk = b""
                    if chunk:
                        out += chunk
                        continue
                if os.waitpid(pid, os.WNOHANG)[0]:
                    child_exited = True
                    break
            break
    try:
        os.close(fd)
    except OSError:
        pass
    if not child_exited:
        try:
            os.waitpid(pid, 0)
        except ChildProcessError:
            pass
    return out

KITTY = {"TERM": "xterm-kitty", "KITTY_WINDOW_ID": "1", "COLORTERM": "truecolor"}
XTERM = {"TERM": "xterm-256color", "COLORTERM": "truecolor"}

fails = []
# 1. kitty: images must be transmitted with the kitty protocol
cmd = ["./build/mdt", "--gfx=kitty", "--toc", "demo/demo.md"]
keys = [b"j", b"j", b" ", b"n", b"/math\r", b"t", b"G", b"g", b"J", b"K", b"\t", b"?", b"\x1b", b"\t", b"\x1b", b"q", b"\x03"]
out = run(cmd, KITTY, keys)
text = out.decode("utf-8", "replace")
if b"\x1b_G" not in out: fails.append("kitty: no graphics escape sequences emitted")
if b"a=t,f=100" not in out: fails.append("kitty: images were not transmitted")
if b"a=p,i=" not in out: fails.append("kitty: images were not placed")
if b"\x1b[?1049h" not in out: fails.append("kitty: alt screen not entered")
if b"\x1b[?1049l" not in out: fails.append("kitty: alt screen not left on exit")
print("kitty run: %d bytes, %d image transmits, %d placements" %
      (len(out), out.count(b"a=t,f=100"), out.count(b"a=p,i=")))
print("   image ids:", sorted(set(int(m) for m in re.findall(rb"i=(\d+)", out)))[:12])

# 2. sixel
out2 = run(["./build/mdt", "--gfx=sixel", "demo/demo.md"], XTERM, [b"j", b"q"])
if b"\x1bPq" not in out2: fails.append("sixel: no DCS sixel output")
print("sixel run: %d bytes, %d sixel payloads" % (len(out2), out2.count(b"\x1bPq")))

# 3. no graphics protocol: must still run and show unicode maths fallback
out3 = run(["./build/mdt", "--gfx=none", "demo/demo.md"], XTERM, [b"j", b"q"])
# note: the capability probe itself is a \x1b_G sequence - ignore it
probe = b"\x1b_Gi=31,s=1,v=1,a=q,f=24,t=d"
rest3 = out3.replace(probe, b"")
if b"\x1bPq" in rest3 or b"\x1b_G" in rest3: fails.append("none: graphics emitted anyway")
print("no-gfx run: %d bytes" % len(out3))

# 4. iterm2
out4 = run(["./build/mdt", "--gfx=iterm2", "demo/demo.md"], {"TERM": "xterm-256color", "TERM_PROGRAM": "iTerm.app"}, [b"j", b"q"])
if b"\x1b]1337;File=" not in out4: fails.append("iterm2: no inline image sequence")
print("iterm2 run: %d bytes, %d inline images" % (len(out4), out4.count(b"\x1b]1337;File=")))

if fails:
    print("\nFAIL:")
    for f in fails: print("  -", f)
    sys.exit(1)
print("\nall pty checks passed")
