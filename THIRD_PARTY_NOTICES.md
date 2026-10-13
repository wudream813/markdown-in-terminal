# Third-party notices

`mdt` itself is MIT licensed (see [LICENSE](LICENSE)).  A single `mdt` binary
also contains the components below — the TeX engine, the SVG rasteriser's
fonts, the image codecs and the vendored runtime.  Their licences are
reproduced in full under [`licenses/`](licenses/).

| component | used for | licence | copyright |
|:--|:--|:--|:--|
| [QuickJS / QuickJS-NG](https://github.com/quickjs-ng/quickjs) (`vendor/quickjs/`) | runs the embedded MathJax TeX→SVG converter | MIT | © 2017–2025 Fabrice Bellard, Charlie Gordon, Ben Noordhuis, Saúl Ibarra Corretgé |
| [MathJax](https://www.mathjax.org/) (`assets/js/mathjax-tex-svg.js`) | TeX/KaTeX input → SVG output | Apache-2.0 | © 2009–2025 The MathJax Consortium |
| [KaTeX](https://katex.org/) (`assets/katex/fontMetricsData.js`) | font metrics for the maths layout | MIT | © 2013–2020 Khan Academy and other contributors |
| [KaTeX fonts](https://github.com/KaTeX/KaTeX/tree/main/fonts) (`assets/fonts/*.ttf`) | drawing formulas in `--screenshot` | SIL OFL 1.1 | © 2013–2020 Khan Academy and other contributors |
| [stb](https://github.com/nothings/stb) (`vendor/stb_*.h`) | PNG/JPEG decoding, PNG writing, image scaling, TrueType rasterising | MIT *or* public domain (Unlicense), dual | © 2017 Sean Barrett and contributors |

Licence texts:

* [`licenses/Apache-2.0.txt`](licenses/Apache-2.0.txt) — MathJax
* [`licenses/OFL-1.1.txt`](licenses/OFL-1.1.txt) — KaTeX fonts
* [`licenses/MIT-QuickJS-NG.txt`](licenses/MIT-QuickJS-NG.txt) — QuickJS-NG
* [`licenses/stb.txt`](licenses/stb.txt) — stb (both options)

Notes:

* The MathJax bundle in `assets/js/` is the production TeX→SVG build of
  MathJax 3, embedded verbatim and executed by QuickJS; mdt does not modify it.
  `tools/build_assets.py` only turns the file into a C++ string so the compiler
  can put it in the binary.
* The KaTeX `fontMetricsData.js` and the KaTeX web fonts are used to typeset
  and to draw formulas.  The fonts are redistributed unmodified.
* libcurl, when linked in, is used under its own MIT-like licence
  (`curl` — © Daniel Stenberg and contributors) and is *not* part of the
  binary this project ships; it is a system library the user's build links
  against.
* On Windows, remote pictures are fetched through the system `WinINet` API
  instead of libcurl.
