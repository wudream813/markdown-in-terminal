# Contributing

Bug reports and patches are welcome.  The most useful report includes the output
of `mdt --diag file.md` (it works without a tty) plus the terminal you use
(`TERM`, `TERM_PROGRAM`) — mdt's behaviour depends on which graphics protocol
the terminal actually supports, and `--diag` says what was detected.

If the display looks wrong rather than the text, `MDT_FRAME_LOG=/tmp/mdt.log
mdt file.md` writes every frame exactly as it was sent, with the escape
sequences spelled out, which tells "mdt drew it wrong" apart from "the terminal
lost it".  `--compat` turns everything optional off; if that fixes the display,
the report is about one of those things.

## Building

```sh
make -j                    # ./build/mdt
make STATIC=1 -j           # fully static (no glibc version to satisfy)
cmake -B build-cmake . && cmake --build build-cmake   # also works
```

Requirements: a C11 compiler, a C++17 compiler and POSIX.  Everything else is
vendored (stb, QuickJS) or embedded at build time — there is no dependency to
install except `curl` at run time for remote pictures (libcurl is used when it
is available, unless `STATIC=1`).

## Testing

```sh
sh tests/smoke.sh                       # everything: dumps, protocols, pty runs
sh tests/cli.sh                         # the command line and the input file
python3 tools/refs_test.py ./build/mdt  # one area, in a pty
```

`tests/smoke.sh` is the gate: it builds nothing, runs the whole suite against
`$BIN` (default `./build/mdt`) and prints one line per check.  The interactive
checks drive the binary inside a pty and assert on what actually lands on the
screen, which is how the scrolling, the protocols and the key handling are
tested.  Add a check next to the behaviour you change — a fixture under
`tests/fixtures/` and a few `grep` lines in the suite is usually enough; a pty
harness under `tools/` is the right tool when the question is what the screen
shows, or which URL a key press hands to the OS.

Two useful extras:

```sh
make mem-report && ./build/mem-report big.md   # where the memory goes
KITTY=1 ...                                    # a real terminal is the last word
```

## Style

* C++17, no exceptions in the hot paths, no new dependencies without a reason.
* Comments explain *why* something is the way it is (the wrong behaviour it
  replaces is worth one line); the code says what it does.
* A behaviour that changes for the user is worth a line in `CHANGELOG.md` and,
  if it is visible on screen, in both `README.md` and `README.zh-CN.md`.
* Commit messages: a short imperative subject, then a paragraph explaining the
  problem the change solves.  `git log` is the project's design record — the
  messages are written to be read by whoever debugs this next.

## Releasing

```sh
# bump VERSION, update CHANGELOG.md, commit, then:
git tag v0.3.9 && git push origin main --tags
```

Pushing a `v*` tag runs `.github/workflows/release.yml`, which builds the Linux
(static), macOS and Windows artefacts, asserts that the Linux one really is
static, and publishes a GitHub Release with them attached.  Check the release
page afterwards — and run the downloaded binary once (`--version`, and a
`--dump` of `demo/demo.md`) before announcing it.
