# Changelog

The format is [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and this
project uses semantic versioning. The version lives in `VERSION`; the Makefile,
CMakeLists and `mdt --version` all read it from there. The releases before 0.3.8
predate this file, so they are summarised from what their commits did.

## [0.3.13] — 2026-10-15

Fixes the Windows console input path (issue #3) and makes the build work in
both MSYS2 shells.

### Fixed

* On Windows an arrow key or a mouse click could end the session silently with
  status 0 (issue #3, WezTerm).  mdt read bytes from the console handle, and
  that path is only as good as the host's VT translation: `ReadFile` on a
  console does not return arrow or function keys at all, discards mouse events,
  and one sequence can reach the application as several separate reads -
  leaving a lone `ESC`, which in mdt means "quit".  Input now comes from the
  console's own records (`ReadConsoleInputW`): keys are translated into the
  same escape sequences a terminal would send, mouse events into SGR reports at
  the same coordinates, and both are handed to the parser whole.  A console can
  also no longer look like end-of-input.  Redirected stdin (`mdt -` behind a
  pipe) still reads bytes, and if a console refuses records mdt falls back to
  that path with the host's VT translation switched back on.

### Added

* `MDT_KEY_LOG=/path/to/file` appends every chunk of input mdt received, hex
  and ASCII side by side, one line per read - the difference between "nothing
  arrived" and "it arrived in pieces".  `tools/keylog_test.py` asserts the
  property automatically, natively and under Wine.
* MSYS2 builds.  `make` now asks the compiler what it targets instead of
  matching its name, so `MSYSTEM=MINGW64` (where the compiler is plain `g++`)
  gets the Windows build.  In the `MSYSTEM=MSYS` shell the target is POSIX, and
  the build switches to `-std=gnu++17`/`-std=gnu11` (the strict ANSI modes hide
  `strdup`, `realpath` and friends) and links iconv as `-lconv`; those
  libraries are probed rather than assumed.  The same is handled in CMakeLists
  and documented in both READMEs.

### Documentation

* `README.zh-CN.md` caught up with the English one: the source layout map, the
  performance numbers and the whole "Display options" section (language,
  settings file, Markdown notes) were missing, as was how to build in MSYS2.
  `## 快捷键` and `## 许可` only exist in the Chinese file and stay there.
* Both READMEs and the man page describe the new Windows input path, and
  `MDT_KEY_LOG` sits next to `MDT_FRAME_LOG` in the environment tables.

## [0.3.12] — 2026-10-14

Fixes the outline panel for documents with more headings than rows (issue #2).

### Fixed

* An outline longer than the panel was only partly reachable (issue #2: thirty
  headings in a twenty-row terminal).  The panel kept showing the top of the
  list while the document scrolled on, and the selection could only be moved
  with a mouse - so the tail of the list could not be selected or jumped to at
  all.  The panel now behaves like a list: `J`/`K` walk it when it is open
  (without it they still jump through the document), the wheel over it walks it
  three entries at a time, and `Enter` jumps to the selection.

### Added

* The outline panel says how long it is: a `12/47` counter in the header and a
  `↑`/`↓` arrow in its right margin when the list continues past the panel.
  Before, a panel too short for the list looked exactly like the whole list.

## [0.3.11] — 2026-10-14

The interface now speaks Chinese as well as English, and the test suite no
longer cares what `LANG` the machine running it has.

### Added

* The interface speaks Chinese as well as English.  `--lang=zh`, `MDT_LANG=zh`,
  `lang = zh` in the settings file, or nothing at all on a machine whose `LANG`
  says `zh_CN.UTF-8`.  Translated: `--help`, the errors from the command line
  and from files, the settings file's complaints, and - in the reader - the
  status bar, the `?` help overlay, the outline panel title and the link dialog.
  `--diag` and `--list-caps` stay English on purpose: they end up in bug
  reports and a report is easier to read in one language.  `zh_TW`/`zh_HK` get
  the same text (one Chinese, the simplified one).  A language mdt does not
  speak is an error (`--lang=fr` exits 2, listing what it does take) rather than
  a silent fallback; an unknown `MDT_LANG` is a warning; an unknown `LANG` is
  not ours to complain about.

### Changed

* The test suite pins `MDT_LANG=en` for the checks that assert English strings
  (and accepts either language where the message follows the machine), so
  `make test` gives the same result on a Chinese machine as on an English one.

* The link dialog's buttons and the outline panel title used to be hard-coded
  Chinese in an otherwise English interface ("c 复制" / "o 浏览器打开" /
  "Esc 关闭"); they follow the language now.
* `--diag` reports the interface language and which of the four settings chose
  it, so a bug report says why the reader speaks what it speaks.

## [0.3.10] — 2026-10-14

A packaging release: nothing about how the reader draws changed, but it is now
installable from somewhere other than the releases page, and the release
workflow itself got the guards it was missing.

### Added

* `packaging/` — the manifests that make mdt installable from somewhere other
  than a release page: Scoop, Homebrew, winget and AUR, plus `gen.py` (downloads
  the artefacts of a tag, hashes them, rewrites every manifest) and `verify.py`
  (downloads every url in the manifests and compares the sha256, so a manifest
  that points at a file the tag does not have fails before it is published).
* `tools/workflow_lint.py`, run as the first step of both workflows: it parses
  the workflow YAML, substitutes `${{ … }}` and `sh -n`s every `run:` block, and
  checks that every `make <target>` a step calls exists in the Makefile.
* Releases now ship `install.sh`, `LICENSE` and a `SHA256SUMS` alongside the
  binaries.

### Changed

* The release workflow runs `tests/smoke.sh` on the commit it is about to tag
  (Linux and macOS legs) instead of publishing a build that was never tested.

## [0.3.9] — 2026-10-14

Everything below this line was in `Unreleased` before the release; 0.3.9 is the
first release that contains the P1/P2 batch from the audit (security fixes went
out earlier, in 0.3.8).

### Fixed

* A 4 MB document needed 500 MB of RAM. `--dump` used to allocate a screen as
  tall as the whole document (`Screen` keeps two cell buffers, 12 bytes a cell,
  so 190 MB of that file went to the framebuffer alone); it now lays the
  document out in 512-row slices and prints them as it goes - 281 MB → 99 MB
  peak, byte-for-byte identical output.
* A GBK ("ANSI") file is read instead of shown as mojibake.
* `--diag` with `--encoding=` no longer reports the raw bytes as invalid UTF-8
  after it has converted them.

### Added

* `--encoding=auto|utf-8|gbk|gb18030|big5|cp1252|latin1|off`, as a flag and as a
  config key. Auto-detection assumes GBK/GB18030; Big5 needs to be named
  explicitly, because GB18030's byte space is a superset of Big5's.
* Footnotes: `[^label]` is numbered where it is first *used*, the notes are
  printed as an ordered list at the end of the document, an undefined label
  stays literal and an unreferenced note is not printed.
* Definition lists: `Term` followed by `: definition` (or `~ definition`), with
  several definitions per term and wrapped text.
* GitHub alerts (`> [!NOTE]` … `> [!CAUTION]`), and `<details>`/`<summary>`
  rendered as a label line plus its body instead of "Summarybody".
* Reference links (`[text][ref]`, `[text][]`, `[ref]`), YAML front matter, and
  `# Title {#id}` heading attributes.
* A settings file (`~/.config/mdt/config`, `%APPDATA%\mdt\config`) whose values
  the command line overrides; a typo is reported with the file and line number.
* `NO_COLOR` (no-color.org) picks the monochrome theme, unless a theme was asked
  for explicitly.
* `--diag` reports the struct sizes, the row/line/run counts and the peak RSS of
  the document it laid out, so a memory report comes with numbers attached.
* `make STATIC=1` builds a fully static binary; the Linux release uses it.
* Man page (`docs/mdt.1`) and shell completions (bash, zsh, fish), installed by
  `make install`.
* `tools/mem_report.cpp` (`make mem-report`) prints the memory breakdown per
  phase, and `tools/memory_test.py` fails the suite if a 3 MB document goes over
  its memory budget.
* `install.sh`: one line downloads the release binary for this OS and
  architecture, checks that it runs, and puts it in your PATH's directory.
* `CHANGELOG.md`, `SECURITY.md`, `CONTRIBUTING.md`, a man page
  (`docs/mdt.1`) and shell completions for bash, zsh and fish.

### Changed

* The command line is checked: a bad value exits 2 and prints the values that
  would have worked, `--width`/`--height`/`--font-px`/`--cell` have ranges, two
  input files are refused instead of silently dropping one, two output modes are
  refused, and a directory says "`X` is a directory, not a file".
* `mdt --dump big.md | head` exits 0 instead of dying of SIGPIPE, and a file of
  more than 512 MB is refused instead of being read until the OOM killer wins.
* CI builds a static Linux binary and asserts with `ldd` that it is static, and
  runs the whole suite under ASan + UBSan (which found a 320-byte leak per
  `--screenshot` in the font loader).

## [0.3.8] — 2026-10-13

### Security

* **A document can no longer run commands.** Picture URLs and links opened with
  `o` were pasted into a shell command line (`popen("curl … \"<URL>\"")`,
  `system("xdg-open '<URL>'")`), so `![](https://host/i.png";cmd;#)` executed
  `cmd` merely by being opened. Both paths now hand the URL to `execvp` /
  `CreateProcessW` / `ShellExecuteW` as a single argument, and a link reaches the
  OS launcher only if its scheme is `http`, `https`, `mailto`, `ftp`, `ftps`,
  `file` or `tel`. Every earlier release is affected and marked as such.
* `tools/injection_test.py` covers both paths, with and without libcurl.

### Added

* `LICENSE` (MIT), `THIRD_PARTY_NOTICES.md`, the vendored licence texts, and
  `README.zh-CN.md`.
* Screenshots and install instructions in both READMEs.

## [0.3.7] — 2026-10-12

### Changed

* The fold arrow moved out of the callout title into the margin, so a long
  LaTeX-style title never wraps onto a row of its own.
* `mdt --version` prints the real version again (it read a copy of the number).

## [0.3.6] — 2026-10-11

### Fixed

* The fold arrow no longer wraps: it starts the title row instead of sitting
  inside it.

## [0.3.5] — 2026-10-11

### Fixed

* Outline entries highlight under the pointer, a callout title that wraps can be
  folded from any of its rows, and `r` (reload) keeps the fold state.

## [0.3.4] — 2026-10-11

### Added

* `e` copies the document to the clipboard as text, keeping quotes, lists,
  tables and callout content in full.

## [0.3.3] — 2026-10-11

### Fixed

* Tab and Enter were dead (the special-key codes never reached the key switch),
  and jumping from the outline or a search hit now opens the folded boxes on the
  way, instead of landing on a hidden row.

## [0.3.2] — 2026-10-11

### Fixed

* Search and the outline see the content of nested `:::directive` boxes.

## [0.3.1] — 2026-10-11

### Added

* A hover wash on folding-box titles; folding and hover are documented in the
  README.

## [0.3.0] — 2026-10-10

### Added

* Callout boxes fold when their title is clicked, `{open}` decides the initial
  state, and dialog buttons highlight on hover.

## [0.2.0] — 2026-10-10

### Added

* The Luogu (洛谷) remark-directive dialect: `:::info`-style callouts with
  coloured bars and title bands, `::cute-table`, `lines=5-6,11` highlighting,
  table merging (`^`, `<`, `>`) and booktabs separators.
* Click a link for a dialog that copies or opens it.
* Sixel: formula and picture bitmaps are composited over the callout title wash.
* A resize test and pty harnesses that wait for the first frame rather than
  sleeping a fixed number of seconds.

## [0.1.0] — 2026-10-07

First tagged release: a terminal Markdown reader with kitty, iTerm2 and sixel
images, a built-in TeX engine (MathJax inside QuickJS), GFM tables, task lists,
strikethrough, four themes, an outline panel, search, a scrollbar, mouse input,
Windows support (Win32 console layer, MinGW-w64 cross build, wine tests) and a
release workflow.

[Unreleased]: https://github.com/wudream813/markdown-in-terminal/compare/v0.3.9...HEAD
[0.3.12]: https://github.com/wudream813/markdown-in-terminal/compare/v0.3.11...v0.3.12
[0.3.11]: https://github.com/wudream813/markdown-in-terminal/compare/v0.3.10...v0.3.11
[0.3.10]: https://github.com/wudream813/markdown-in-terminal/compare/v0.3.9...v0.3.10
[0.3.9]: https://github.com/wudream813/markdown-in-terminal/compare/v0.3.8...v0.3.9
[0.3.8]: https://github.com/wudream813/markdown-in-terminal/compare/v0.3.7...v0.3.8
[0.3.7]: https://github.com/wudream813/markdown-in-terminal/compare/v0.3.6...v0.3.7
[0.3.6]: https://github.com/wudream813/markdown-in-terminal/compare/v0.3.5...v0.3.6
[0.3.5]: https://github.com/wudream813/markdown-in-terminal/compare/v0.3.4...v0.3.5
[0.3.4]: https://github.com/wudream813/markdown-in-terminal/compare/v0.3.3...v0.3.4
[0.3.3]: https://github.com/wudream813/markdown-in-terminal/compare/v0.3.2...v0.3.3
[0.3.2]: https://github.com/wudream813/markdown-in-terminal/compare/v0.3.1...v0.3.2
[0.3.1]: https://github.com/wudream813/markdown-in-terminal/compare/v0.3.0...v0.3.1
[0.3.0]: https://github.com/wudream813/markdown-in-terminal/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/wudream813/markdown-in-terminal/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/wudream813/markdown-in-terminal/releases/tag/v0.1.0
