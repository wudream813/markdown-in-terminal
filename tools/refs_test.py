#!/usr/bin/env python3
"""Reference-style links, YAML front matter and heading attributes.

Until 0.3.9 the parser knew the `[text][ref]` *shape* but never resolved it:
the definition line was printed as ordinary prose and the link pointed at the
label itself, so `[d]: https://example.com` was both visible in the document and
useless.  Front matter was rendered as text and `# Title {#id}` kept its braces.

    python3 tools/refs_test.py ./build/mdt
    python3 tools/refs_test.py build/win/mdt.exe wine64
"""
import fcntl
import os
import pty
import re
import select
import struct
import sys
import tempfile
import termios
import time

ANSI = re.compile(r"\x1b\[[0-9;?]*[a-zA-Z]|\x1b_G.*?\x1b\\|\x1b\]0;.*?\x07|\x1b\][^\x07]*\x07")


def run(argv, keys=(), cols=90, rows=26, settle=12.0, after=2.0):
    pid, fd = pty.fork()
    if pid == 0:
        env = dict(os.environ)
        env["TERM"] = "xterm-256color"
        os.execvpe(argv[0], argv, env)
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
    out = b""
    t0 = time.time()
    sent = False
    while time.time() - t0 < settle:
        r, _, _ = select.select([fd], [], [], 0.2)
        if r:
            try:
                chunk = os.read(fd, 1 << 20)
            except OSError:
                break
            if not chunk:
                break
            out += chunk
        if not sent and b"for help" in out:
            for k in keys:
                os.write(fd, k)
                time.sleep(0.4)
            sent = True
            t0 = time.time() - (settle - after)
    try:
        os.close(fd)
    except OSError:
        pass
    try:
        os.waitpid(pid, 0)
    except ChildProcessError:
        pass
    return out


def screen_text(raw):
    return ANSI.sub("", raw.decode("utf-8", "replace"))


def main():
    binary = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "./build/mdt")
    runner = sys.argv[2] if len(sys.argv) > 2 else None
    doc = os.path.abspath("tests/fixtures/refs.md")
    tmp = tempfile.mkdtemp(prefix="mdt-refs-")
    bindir = os.path.join(tmp, "bin")
    os.makedirs(bindir, exist_ok=True)
    log = os.path.join(tmp, "argv.log")
    stub = '#!/bin/sh\nfor a in "$@"; do printf \'%s\\n\' "$a" >> "$MDT_ARGV_LOG"; done\nexit 0\n'
    for name in ("xdg-open", "open"):
        path = os.path.join(bindir, name)
        with open(path, "w") as f:
            f.write(stub)
        os.chmod(path, 0o755)

    failures = []
    env_backup = os.environ.copy()
    os.environ["PATH"] = bindir + os.pathsep + os.environ.get("PATH", "")
    os.environ["MDT_ARGV_LOG"] = log
    # Keep the screen checks in English-independent terms: the document text.
    argv = ([runner, binary] if runner else [binary]) + ["--gfx=none", doc]
    raw = run(argv, keys=())
    text = screen_text(raw)

    if "Heading with attributes" in text and "custom-id" not in text:
        print("   ok   '# Heading {#custom-id}' renders as a heading")
    else:
        failures.append("the heading attribute list was not stripped")

    if "front matter title" not in text:
        print("   ok   YAML front matter is not rendered")
    else:
        failures.append("front matter leaked into the text")

    if "https://example.com/docs" not in text:
        print("   ok   a '[d]: url' definition line is not rendered")
    else:
        failures.append("a link reference definition was printed as prose")

    if "[nope]" in text and "[text][missing]" in text:
        print("   ok   an undefined reference stays as written")
    else:
        failures.append("an undefined reference did not stay literal")

    # The [d] definition is inside a code fence: it must stay code.
    if "[label]: https://not-a-definition.example" in text:
        print("   ok   a definition-looking line inside a fence stays code")
    else:
        failures.append("a code block line was eaten as a definition")

    # ... and the resolved URL is what `o` opens.
    raw = run(argv, keys=(b"o",))
    opened = []
    if os.path.exists(log):
        opened = [l for l in open(log).read().splitlines() if l.strip()]
    if "https://example.com/docs" in opened:
        print("   ok   'o' opens the *defined* URL, not the label")
    else:
        failures.append("the launcher got %r (expected [+--] the defined https URL)" % (opened,))

    for k, v in env_backup.items():
        os.environ[k] = v
    import shutil
    shutil.rmtree(tmp, ignore_errors=True)

    if failures:
        print("\n   FAIL:")
        for f in failures:
            print("     - " + f)
        return 1
    print("   reference link checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
