# Security

## Supported versions

Only the newest release is supported.  Older ones get a warning on their release
page and are not patched.

| Version | Supported |
|---|---|
| 0.3.8 and later | yes |
| 0.3.7 and earlier | no — see below |

**Every release up to and including 0.3.7 is vulnerable to command injection**
(CVE-worthy, no CVE requested): a picture URL or a link in a document was pasted
into a shell command line, so opening a malicious `.md` could run commands.  0.3.8
fixed it.  Please update before anything else.

## Reporting a vulnerability

Open a private report through GitHub's
[Security tab](https://github.com/wudream813/markdown-in-terminal/security/advisories/new)
("Report a vulnerability"), or mail the maintainer if you prefer.  Please
include:

* the version (`mdt --version`) and the platform,
* a document or command line that shows the problem,
* what you expected instead.

A reply should arrive within a week.  If the report is valid, the fix is
released as a patch version, with the report credited unless you ask otherwise.

## What mdt does with a document

A Markdown file is untrusted input, and mdt's rule is that it may only ever be
*rendered*:

* **No shell.**  URLs (pictures, links) are handed to `execvp` /
  `CreateProcessW` / `ShellExecuteW` as single arguments, never through `sh -c`
  or `system()`.  `tools/injection_test.py` drives both paths in a pty with
  stub launchers and asserts that a URL like
  `https://host/i.png";touch /tmp/pwned;#` arrives as one argument and that
  nothing was executed.
* **Scheme allow-list.**  A link reaches the OS launcher only when its scheme is
  `http`, `https`, `mailto`, `ftp`, `ftps`, `file` or `tel`; `javascript:`,
  `data:` and anything else are refused, and so is a link to a local path.
* **No network without being asked.**  Remote pictures are downloaded in the
  background (through libcurl, or the `curl` binary when mdt is built without
  it) only for documents that reference them; a text dump never fetches
  anything.
* **Nothing is written** except the file you name with `--screenshot` or
  `--dump=file`.
* **Bounded reading.**  A document larger than 512 MB is refused instead of
  being read until memory runs out, and `--width`/`--height` are range-checked
  so a command line cannot ask for a framebuffer that does not fit.
* **Embedded engine.**  The maths engine is a vendored QuickJS; the JavaScript
  it runs is the MathJax converter embedded in the binary.  Document text is
  passed to it as data, not as code.

## Known non-issues

* A document can reference a remote picture, which leaks the fact that you
  opened it to that host (as any reader with images does).  `--gfx=none` or a
  text dump avoids it.
* `o` and clicking a link hand the URL to your browser or mail client; that is
  the point of the key, and the scheme allow-list is what bounds it.
