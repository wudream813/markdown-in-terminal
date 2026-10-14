# Changelog

The format is [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and this
project uses semantic versioning. The version lives in `VERSION`; the Makefile,
CMakeLists and `mdt --version` all read it from there. The releases before 0.3.8
predate this file, so they are summarised from what their commits did.

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
