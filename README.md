# mdt — Markdown in the terminal

A **C++17 terminal Markdown reader** that draws maths and images through the
terminal *graphics protocol* (kitty / iTerm2 / sixel), with a **built-in
KaTeX-compatible maths engine**. Everything — the TeX typesetter, the SVG
rasteriser, image decoding, the three graphics protocols — lives inside a single
self-contained binary.

```
  ┌─ demo.md ───────────────────────────────────────────────────────────────┐
  │  ∫_{-∞}^{∞} e^{-x²} dx = √π          # typeset by the embedded engine   │
  └─────────────────────────────────────────────────────────────────────────┘
```

## Highlights

* **KaTeX syntax, no runtime dependencies.** The maths pipeline is
  TeX/KaTeX source → SVG (MathJax's TeX input jax + SVG output jax, ~1.8 MB of JS
  embedded in the binary) → rasterised by a small hand-written SVG renderer →
  PNG. The JS is executed by an embedded **QuickJS** interpreter, so a single
  `mdt` executable renders `\frac`, `\sum`, `\int`, `\begin{pmatrix}` … with no
  node, no browser and no external files.
* **Three graphics protocols**, auto-detected at start-up:
  kitty (`\x1b_G…`, also WezTerm/Ghostty/Konsole), iTerm2 inline images
  (`\x1b]1337;File=`), and sixel (xterm/mlterm/foot/Windows Terminal) with a
  median-cut 256-colour encoder.
* **Inline graphics.** Formulas and images sit on the text baseline
  (fractional cell offsets on kitty, cell-aligned elsewhere) and flow with the
  paragraph.
* **Graceful degradation.** In a terminal without a graphics protocol the
  formulas are transcribed to Unicode (`∑`, `∫`, `√`, sub/superscripts) instead
  of disappearing. `m` toggles between typeset and plain maths live.
* Terminal abilities are probed at run time: cell size in pixels (`CSI 16 t`),
  window size (`CSI 14 t`), sixel support (DA1), kitty graphics support
  (`\x1b_Gi=…,a=q`). The em/cell ratio follows the user's real font.

## Build

```sh
make -j            # produces build/mdt  (needs a C11 + C++17 compiler)
# or
cmake -B build && cmake --build build -j
```

Everything else is vendored: `stb_truetype/stb_image/stb_image_write/stb_image_resize2`
and QuickJS-NG. `python3` is only needed when you change the JS bundle
(`make assets`). If libcurl is available it is linked in for remote images;
otherwise the `curl` binary is used as a fallback.

## Usage

```sh
mdt README.md                 # read a file
mdt --toc notes.md            # start with the outline panel open
mdt --gfx=kitty --theme=light big.md
mdt --dump --width=100 x.md   # text rendering to stdout (no graphics)
mdt --screenshot=shot.png x.md  # render the whole screen to a PNG
cat notes.md | mdt --dump -   # read the document from stdin
```

Options

| flag | meaning |
|:--|:--|
| `--gfx=auto\|kitty\|iterm2\|sixel\|none` | graphics protocol (default `auto`) |
| `--math=katex\|unicode\|off` | maths engine (default `katex`, built in) |
| `--theme=dark\|light\|nord\|monochrome` | colour theme |
| `--toc` | open the outline panel on start |
| `--line-numbers` | line numbers in code blocks |
| `--no-syntax` | disable code highlighting |
| `--font-px=N` | override the derived maths font size |
| `--dump[=file]`, `--screenshot=out.png`, `--width`, `--height` | non-interactive output |
| `--list-caps` | print the detected terminal capabilities |

Keys

| key | action |
|:--|:--|
| `j` `k` `↓` `↑` | scroll a line |
| `space` `b` / `d` `u` | page / half page |
| `g` `G` | top / bottom |
| `J` `K` | next / previous heading |
| `Tab` | outline panel (Enter or click jumps to a heading) |
| `/` `n` `N` | search, next / previous match |
| `t` | cycle theme (`dark`, `light`, `nord`, `monochrome`) |
| `m` | toggle typeset ⇄ plain-Unicode maths |
| `r` | reload the file |
| `o` | open the first link in the browser |
| `e` | export the document text to the clipboard (OSC 52) |
| `?` | help · `q` / `Ctrl-C` quit |

## How the maths pipeline works

```
$…$ / $$…$$ in markdown
        │
        ▼
Mathjax TeX→SVG (JS, embedded)         ← KaTeX-compatible syntax, TeX superset
   run by QuickJS inside the binary
        │  <svg viewBox="0 -1562.5 8064.8 2808.5" width="18.2ex">
        ▼
svg.cpp: XML + path (M/C/S/Q/A/Z, transforms, matrices) parser
   → flattened into polygons, scan-line rasteriser with non-zero winding,
     supersampled anti-aliasing, composited to RGBA
        ▼
render.cpp: sized against the terminal's cell grid, laid out on the text
   baseline (em = 0.85 · cell height, baseline = 0.76 · cell height)
        ▼
term.cpp: PNG for kitty/iTerm2, sixel for sixel terminals,
          cached by content hash so each image is transmitted only once
```

Metrics come out of the SVG (`viewBox` and `vertical-align:-2.819ex`), which is
exactly what a browser would use, so inline formulas align with the surrounding
text instead of floating.

## Layout

```
src/util.*        UTF-8 + wcwidth tables, strings, base64, colours
src/term.*        capabilities, cell framebuffer with diffing, kitty/iTerm2/sixel
src/md.*          CommonMark subset + GFM tables/strike/task lists + $maths$
src/svg.*         SVG subset parser + software rasteriser
src/math.*        QuickJS host + maths metrics/raster cache + Unicode fallback
src/image.*       stb_image decode, scaling, PNG encode, http(s) fetch
src/render*.cpp   document layout and drawing (text grid + graphics overlays)
src/font.*        TrueType rasteriser used by --screenshot
src/app.cpp       event loop, outline, search, status bar
src/generated/    the embedded JS bundle (generated by tools/build_assets.py)
vendor/           stb single-file libraries, QuickJS-NG
```

`tools/pty_test.py` drives the binary in a pty and checks that each protocol
really emits its escape sequences. `sh tests/smoke.sh` builds nothing but runs
the whole battery (themes, maths modes, stdin, text export, all three
protocols) against `./build/mdt`.

## Performance notes

Measured on a 250 KB document (400 sections, ~420 formulas, tables, code, images):

| stage | time |
|:--|--:|
| markdown parse | 6 ms |
| layout of the whole document, formulas deferred | 21 ms |
| first frame on screen (incl. QuickJS start-up ~150 ms) | 0.57 s |
| same document with eager formula typesetting (old behaviour) | 2.5 s |
| re-render of a scrolled frame | < 10 ms, formula bitmaps are cached |

Formulas are typeset lazily: only the ones that come into view are measured and
rasterised. If a deferred formula turns out to be bigger or smaller than the
layout estimate, the document is relaid out once (metrics are cached by then) so
the final geometry is exact.

## Notes / limitations

* Block-level maths is centred and drawn as a bitmap; very wide equations are
  scaled down to the content width.
* sixel images are cell-aligned (a sixel cursor cannot be nudged by a fraction
  of a cell), kitty and iTerm2 images are placed on the baseline.
* Reference-style links `[text][ref]` are parsed but the definitions are not
  resolved yet; `_italic_`/`**bold**` inside words follow the CommonMark rules.
* HTML blocks are reduced to their text content, and tables inside block quotes
  are simplified.

MIT licensed. Vendored components keep their own licences
(stb — public domain/MIT, QuickJS-NG — MIT, MathJax — Apache-2.0).
