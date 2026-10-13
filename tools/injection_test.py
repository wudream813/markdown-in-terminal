#!/usr/bin/env python3
"""Guards the two places where a document could run commands (fixed in 0.3.8).

Until 0.3.7 both of them built a *shell command line* out of a URL that came
straight from the Markdown file:

  * the `curl` fallback for remote pictures (src/image.cpp) - `popen("curl … \""
    + url + "\"")`.  The published Linux binary has no libcurl, so it took this
    path: opening a document whose picture was
    `![](https://host/i.png";touch${IFS}/tmp/pwned;#)` ran `touch`, with no key
    pressed (also under --screenshot).  Reported as a command injection.
  * the "open link" key `o` (src/platform.cpp) - `system("xdg-open '" + url +
    "'")`, where one `'` in the link ends the quoting.  Every platform.

Both now hand an argument vector to execvp()/CreateProcess() (plat::run_capture,
plat::open_url), where `"` `;` `$` and backticks are ordinary characters of one
argument.

This test replaces `curl` and `xdg-open` with scripts that record their argv,
then checks that
  1. the payload runs nothing (no marker file),
  2. the URL arrives as exactly one argument, byte for byte - which is what
     "the shell never saw it" means,
  3. a link that is not a web/mail/file URL is refused instead of handed to the
     OS launcher (a document may not ask `ShellExecuteW` to run a local file).

    python3 tools/injection_test.py ./build/mdt
"""
import os
import re
import shutil
import struct
import sys
import tempfile
import time
import zlib
import fcntl
import pty
import select
import subprocess
import termios


def png_bytes(w=8, h=8):
    """A minimal valid PNG, so the picture pipeline can finish end to end."""
    raw = b"".join(b"\x00" + bytes([200, 40, 40, 255]) * w for _ in range(h))

    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr)
            + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def write_tools(tmp):
    """Fake curl / xdg-open that log every argument on its own line."""
    bindir = os.path.join(tmp, "bin")
    os.makedirs(bindir, exist_ok=True)
    png = os.path.join(tmp, "pixel.png")
    with open(png, "wb") as f:
        f.write(png_bytes())
    log_line = '#!/bin/sh\nfor a in "$@"; do printf \'%s\\n\' "$a" >> "$MDT_ARGV_LOG"; done\n'
    curl = os.path.join(bindir, "curl")
    with open(curl, "w") as f:
        f.write(log_line + 'cat "' + png + '"\n')
    opener = os.path.join(bindir, "xdg-open")
    with open(opener, "w") as f:
        f.write(log_line + "exit 0\n")
    for p in (curl, opener):
        os.chmod(p, 0o755)
    return bindir


def run_interactive(binary, doc, keys, bindir, env_extra, cols=90, rows=24):
    env = dict(os.environ)
    env["TERM"] = "xterm-256color"
    env["PATH"] = bindir + os.pathsep + env.get("PATH", "")
    env.update(env_extra)
    pid, fd = pty.fork()
    if pid == 0:
        os.execvpe(binary, [binary, doc], env)
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
    out = b""
    t0 = time.time()
    sent = False
    while time.time() - t0 < 12:
        r, _, _ = select.select([fd], [], [], 0.2)
        if r:
            try:
                chunk = os.read(fd, 1 << 20)
            except OSError:
                break
            if not chunk:
                break
            out += chunk
        # Wait for the first frame: before raw mode is on, keys are swallowed.
        if not sent and b"for help" in out:
            for k in keys:
                os.write(fd, k)
                time.sleep(0.4)
            sent = True
            t0 = time.time()  # give the rest a fresh budget
        elif sent and time.time() - t0 > 2.0:
            break
    try:
        os.close(fd)
    except OSError:
        pass
    try:
        os.waitpid(pid, 0)
    except ChildProcessError:
        pass
    return out


PAYLOAD = "https://example.com/i.png\";touch${IFS}%s;#"          # picture URL
LINK_PAYLOAD = "https://example.com/a';touch${IFS}%s;#"          # link target


def main():
    binary = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "./build/mdt")
    tmp = tempfile.mkdtemp(prefix="mdt-injection-")
    failures = []
    try:
        bindir = write_tools(tmp)
        log = os.path.join(tmp, "argv.log")
        marker = os.path.join(tmp, "pwned")
        link_marker = os.path.join(tmp, "pwned-link")
        env = {"MDT_ARGV_LOG": log, "MDT_FORCE_CURL_BIN": "1"}

        img_doc = os.path.join(tmp, "picture.md")
        with open(img_doc, "w") as f:
            f.write("# picture\n\n![diagram](%s)\n\ntext after\n" % (PAYLOAD % marker))

        link_doc = os.path.join(tmp, "link.md")
        with open(link_doc, "w") as f:
            f.write("# link\n\n[click](%s)\n" % (LINK_PAYLOAD % link_marker))

        local_doc = os.path.join(tmp, "local.md")
        with open(local_doc, "w") as f:
            f.write("# local\n\n[run this](setup.sh)\n")

        # 1. the picture URL path (curl fallback, the published binary's path)
        shot = os.path.join(tmp, "shot.png")
        p = subprocess.run([binary, "--screenshot=" + shot, "--width=60", "--height=16", img_doc],
                           env=dict(os.environ, PATH=bindir + os.pathsep + os.environ["PATH"], **env),
                           capture_output=True, timeout=120)
        if os.path.exists(marker):
            failures.append("a picture URL ran a command (marker %s exists)" % marker)
        else:
            print("   ok   a picture URL cannot run a command (--screenshot)")

        argv_seen = open(log).read().splitlines() if os.path.exists(log) else []
        want = PAYLOAD % marker
        if want in argv_seen:
            print("   ok   the URL reached curl as one argument, unquoted by any shell")
        else:
            failures.append("curl argv did not contain the raw URL (got %r)" % (argv_seen,))
        if any(a in (";", "touch", "-c") for a in argv_seen):
            failures.append("the URL was split into shell words: %r" % (argv_seen,))

        # 2. opening a link with `o`
        before = len(argv_seen)
        run_interactive(binary, link_doc, [b"o"], bindir, env)
        if os.path.exists(link_marker):
            failures.append("opening a link ran a command (marker %s exists)" % link_marker)
        else:
            print("   ok   opening a link cannot run a command (o)")
        seen = open(log).read().splitlines() if os.path.exists(log) else []
        link_url = LINK_PAYLOAD % link_marker
        if link_url in seen:
            print("   ok   the link reached the launcher as one argument")
        else:
            failures.append("the launcher did not get the raw URL (got %r)" % (seen[before:],))

        # 3. a link that is not a web/mail/file URL is refused
        before = len(seen)
        run_interactive(binary, local_doc, [b"o"], bindir, env)
        seen = open(log).read().splitlines() if os.path.exists(log) else []
        if "setup.sh" in seen:
            failures.append("a bare path was handed to the OS launcher")
        else:
            print("   ok   a local-path link is refused instead of executed/opened")

        # 4. the same document with the normal (libcurl / WinINet) downloader
        os.remove(marker) if os.path.exists(marker) else None
        subprocess.run([binary, "--screenshot=" + shot, "--width=60", "--height=16", img_doc],
                       env=dict(os.environ, PATH=bindir + os.pathsep + os.environ["PATH"]),
                       capture_output=True, timeout=180)
        if os.path.exists(marker):
            failures.append("the default downloader ran a command")
        else:
            print("   ok   the default downloader does not run anything either")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    if failures:
        print("\n   FAIL:")
        for f in failures:
            print("     - " + f)
        return 1
    print("   injection checks passed")
    return 0


def main_windows(exe, wine):
    """The Windows build under Wine.

    PATH tricks with fake launchers do not work through Wine (CreateProcess
    cannot run a shell script and a .bat needs cmd.exe again), so this checks
    the payload itself: a cmd.exe payload that would write a file if the URL
    ever reached `cmd /c`.  The image URL points at a closed port so the
    download fails at once instead of waiting for a network timeout.
    """
    tmp = tempfile.mkdtemp(prefix="mdt-injection-win-")
    prefix = os.environ.get("WINEPREFIX", os.path.join(os.path.expanduser("~"), ".wineprefix"))
    marker_win = "C:\\mdt_pwned_flag"
    marker_host = os.path.join(prefix, "drive_c", "mdt_pwned_flag")
    for stale in (marker_host,):
        try:
            os.remove(stale)
        except OSError:
            pass
    try:
        doc = os.path.join(tmp, "picture.md")
        with open(doc, "w") as f:
            f.write("# picture\n\n![diagram](http://127.0.0.1:1/i.png\" & echo pwned> %s & \")\n"
                    % marker_win)
        shot = os.path.join(tmp, "shot.png")
        env = dict(os.environ)
        env.setdefault("WINEDEBUG", "-all")
        subprocess.run([wine, exe, "--screenshot=" + shot, "--width=60", "--height=16", doc],
                       env=env, capture_output=True, timeout=180)
        if os.path.exists(marker_host):
            print("   FAIL a picture URL ran a cmd.exe command")
            return 1
        print("   ok   a picture URL cannot run a command (WinINet/curl.exe path)"
              if os.path.exists(shot) else "   ok   no command ran (screenshot was not produced)")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return 0


if __name__ == "__main__":
    if len(sys.argv) > 2:
        sys.exit(main_windows(os.path.abspath(sys.argv[1]), sys.argv[2]))
    sys.exit(main())
