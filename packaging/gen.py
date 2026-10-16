#!/usr/bin/env python3
"""Regenerate the packaging manifests for a release.

The manifests in this directory (Scoop, Homebrew, winget, AUR) are real files
that get published, not templates, so every new release means fresh version
strings and fresh sha256 sums.  This script downloads the release assets and
the raw files at that tag, hashes them, and rewrites all of them:

    python3 packaging/gen.py v0.3.9                  # use the released assets
    python3 packaging/gen.py v0.3.9 --from ~/dl      # use files you already have
    python3 packaging/gen.py v0.3.9 --check          # only report drift

then, to publish the two channels that live in their own repositories:

    GITHUB_TOKEN=... python3 packaging/gen.py v0.3.9 --publish

winget and AUR are not pushable from here: winget needs a pull request into
microsoft/winget-pkgs (with a CLA signature) and AUR needs an SSH key that is
registered to an AUR account.  The files this script writes are exactly what
those two want, see packaging/README.md.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import sys
import urllib.error
import urllib.request

REPO = "wudream813/markdown-in-terminal"
HOMEPAGE = f"https://github.com/{REPO}"
LICENCE = "MIT"
DESC = "Terminal Markdown reader: typeset maths, images, syntax highlighting"  # < 80 chars
LONG = (
    "mdt reads a Markdown file in the terminal and draws what a terminal pager "
    "normally cannot: formulas typeset by an embedded KaTeX-compatible engine, "
    "images through the kitty/iTerm2/sixel graphics protocols, tables, alerts, "
    "footnotes.  One self-contained C++17 binary, no runtime dependencies."
)
MONIKER = "mdt"
TAGS = ["markdown", "terminal", "cli", "viewer", "tui", "math", "latex"]

HERE = pathlib.Path(__file__).resolve().parent

# name -> (url template, what it is used for)
ASSETS = {
    "linux": ("mdt-linux-amd64", "Homebrew (Linux), AUR"),
    "linux_arm64": ("mdt-linux-arm64", "Homebrew (Linux arm64), AUR"),
    "macos": ("mdt-macos-arm64", "Homebrew (macOS)"),
    "zip": ("mdt-{version}-win64.zip", "Scoop, winget"),
}
RAW = {
    "man": "docs/mdt.1",
    "bash": "completions/mdt.bash",
    "zsh": "completions/_mdt",
    "fish": "completions/mdt.fish",
    "licence": "LICENSE",
}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def fetch(url: str) -> bytes:
    req = urllib.request.Request(url, headers={"User-Agent": "mdt-packaging/1"})
    with urllib.request.urlopen(req, timeout=120) as r:
        return r.read()


def collect(tag: str, version: str, from_dir: pathlib.Path | None) -> dict[str, bytes]:
    """Every file a manifest points at, keyed by the name used in the templates."""
    wanted: dict[str, str] = {}
    for key, (name, _) in ASSETS.items():
        wanted[key] = f"https://github.com/{REPO}/releases/download/{tag}/{name.format(version=version)}"
    for key, path in RAW.items():
        wanted[key] = f"https://raw.githubusercontent.com/{REPO}/{tag}/{path}"

    blobs: dict[str, bytes] = {}
    for key, url in wanted.items():
        local = from_dir / pathlib.PurePosixPath(url).name if from_dir else None
        if local and local.is_file():
            blobs[key] = local.read_bytes()
            print(f"  {key:<8} sha256:{sha256(blobs[key])[:16]}…  (local {local})")
        else:
            blobs[key] = fetch(url)
            print(f"  {key:<8} sha256:{sha256(blobs[key])[:16]}…  ({url})")
    return blobs


def scoop(version: str, h: dict[str, str]) -> str:
    return json.dumps(
        {
            "version": version,
            "description": DESC,
            "homepage": HOMEPAGE,
            "license": LICENCE,
            "notes": [
                "Run 'mdt --list-caps' to see what this terminal supports.",
                "mdt --help lists every option; the zip also ships a README.",
            ],
            "architecture": {
                "64bit": {
                    "url": f"{HOMEPAGE}/releases/download/v{version}/mdt-{version}-win64.zip",
                    "hash": h["zip"],
                    "extract_dir": f"mdt-{version}-win64",
                }
            },
            "bin": "mdt.exe",
            "checkver": "github",
            "autoupdate": {
                "architecture": {
                    "64bit": {
                        "url": f"{HOMEPAGE}/releases/download/v$version/mdt-$version-win64.zip",
                        "extract_dir": "mdt-$version-win64",
                    }
                }
            },
        },
        indent=4,
    ) + "\n"


def homebrew(version: str, h: dict[str, str]) -> str:
    return f'''class Mdt < Formula
  desc "{DESC}"
  homepage "{HOMEPAGE}"
  version "{version}"
  license "{LICENCE}"

  # Prebuilt, self-contained binaries: Apple silicon on macOS, and fully
  # static builds on Linux for both x86-64 and arm64 (no glibc version to
  # satisfy either way).
  on_macos do
    on_arm do
      url "{HOMEPAGE}/releases/download/v{version}/mdt-macos-arm64"
      sha256 "{h['macos']}"
    end
  end

  on_linux do
    on_intel do
      url "{HOMEPAGE}/releases/download/v{version}/mdt-linux-amd64"
      sha256 "{h['linux']}"
    end
    on_arm do
      url "{HOMEPAGE}/releases/download/v{version}/mdt-linux-arm64"
      sha256 "{h['linux_arm64']}"
    end
  end

  resource "man" do
    url "https://raw.githubusercontent.com/{REPO}/v{version}/docs/mdt.1"
    sha256 "{h['man']}"
  end

  resource "completion-bash" do
    url "https://raw.githubusercontent.com/{REPO}/v{version}/completions/mdt.bash"
    sha256 "{h['bash']}"
  end

  resource "completion-zsh" do
    url "https://raw.githubusercontent.com/{REPO}/v{version}/completions/_mdt"
    sha256 "{h['zsh']}"
  end

  resource "completion-fish" do
    url "https://raw.githubusercontent.com/{REPO}/v{version}/completions/mdt.fish"
    sha256 "{h['fish']}"
  end

  def install
    binary = if OS.mac?
               "mdt-macos-arm64"
             elsif Hardware::CPU.arm?
               "mdt-linux-arm64"
             else
               "mdt-linux-amd64"
             end
    bin.install binary => "mdt"

    man1.install resource("man")
    bash_completion.install resource("completion-bash") => "mdt"
    zsh_completion.install resource("completion-zsh") => "_mdt"
    fish_completion.install resource("completion-fish") => "mdt.fish"
  end

  def caveats
    <<~EOS
      mdt is not notarised by Apple, so a copy that has been quarantined (that
      is, downloaded with a browser) will be refused by Gatekeeper.  Homebrew's
      own download is not quarantined; if you moved a copy in yourself, clear
      it with:

        xattr -d com.apple.quarantine #{{bin}}/mdt

      Remote images are fetched with the curl(1) command, and links are opened
      with xdg-open(1) (Linux), so having curl and xdg-utils installed helps.
    EOS
  end

  test do
    assert_match version.to_s, shell_output("#{{bin}}/mdt --version")
    (testpath/"t.md").write "# heading\\n\\nsome *text*\\n"
    assert_match "heading", shell_output("#{{bin}}/mdt --dump #{{testpath}}/t.md")
  end
end
'''


def q(text: str) -> str:
    """A YAML-safe double-quoted scalar (JSON quoting is valid YAML)."""
    return json.dumps(text, ensure_ascii=False)


def winget(version: str, h: dict[str, str]) -> dict[str, str]:
    ident = "wudream813.MarkdownInTerminal"
    version_yaml = f"""# yaml-language-server: $schema=https://aka.ms/winget-manifest.version.1.6.0.schema.json
PackageIdentifier: {ident}
PackageVersion: {version}
DefaultLocale: en-US
ManifestType: version
ManifestVersion: 1.6.0
"""
    installer_yaml = f"""# yaml-language-server: $schema=https://aka.ms/winget-manifest.installer.1.6.0.schema.json
PackageIdentifier: {ident}
PackageVersion: {version}
InstallerType: zip
NestedInstallerType: portable
NestedInstallerFiles:
- RelativeFilePath: mdt-{version}-win64\\mdt.exe
  PortableCommandAlias: mdt
Installers:
- Architecture: x64
  InstallerUrl: {HOMEPAGE}/releases/download/v{version}/mdt-{version}-win64.zip
  InstallerSha256: {h['zip'].upper()}
ManifestType: installer
ManifestVersion: 1.6.0
"""
    locale_yaml = f"""# yaml-language-server: $schema=https://aka.ms/winget-manifest.defaultLocale.1.6.0.schema.json
PackageIdentifier: {ident}
PackageVersion: {version}
PackageLocale: en-US
Publisher: wudream813
PublisherUrl: {HOMEPAGE.rsplit("/", 1)[0]}/wudream813
PackageName: mdt
PackageUrl: {HOMEPAGE}
License: {LICENCE}
LicenseUrl: {HOMEPAGE}/blob/main/LICENSE
ShortDescription: {q(LONG)}
Moniker: {MONIKER}
Tags:
""" + "".join(f"- {t}\n" for t in TAGS) + f"""ReleaseNotesUrl: {HOMEPAGE}/releases/tag/v{version}
ManifestType: defaultLocale
ManifestVersion: 1.6.0
"""
    return {
        f"{ident}.yaml": version_yaml,
        f"{ident}.installer.yaml": installer_yaml,
        f"{ident}.locale.en-US.yaml": locale_yaml,
    }


def aur(version: str, h: dict[str, str]) -> tuple[str, str]:
    pkgbuild = f'''# Maintainer: wudream813 <wudream813@users.noreply.github.com>
# Prebuilt -bin package: the release binary is statically linked, so there is
# nothing to compile and nothing to link against at run time.
pkgname=mdt-bin
pkgver={version}
pkgrel=1
pkgdesc='{DESC}'
arch=('x86_64' 'aarch64')
url='{HOMEPAGE}'
license=('{LICENCE}')
depends=()
optdepends=(
    'curl: fetch images that are linked from a document'
    'xdg-utils: open links in your browser'
)
provides=('mdt')
conflicts=('mdt' 'mdt-git')
options=('!strip' '!debug')
_gh="{HOMEPAGE}"
_raw="https://raw.githubusercontent.com/{REPO}"

# One statically linked binary per architecture, straight from the release.
source_x86_64=("mdt-linux-amd64::$_gh/releases/download/v$pkgver/mdt-linux-amd64")
source_aarch64=("mdt-linux-arm64::$_gh/releases/download/v$pkgver/mdt-linux-arm64")
source=("mdt.1::$_raw/v$pkgver/docs/mdt.1"
        "mdt.bash::$_raw/v$pkgver/completions/mdt.bash"
        "_mdt::$_raw/v$pkgver/completions/_mdt"
        "mdt.fish::$_raw/v$pkgver/completions/mdt.fish"
        "LICENSE::$_raw/v$pkgver/LICENSE")
sha256sums_x86_64=('{h['linux']}')
sha256sums_aarch64=('{h['linux_arm64']}')
sha256sums=('{h['man']}'
            '{h['bash']}'
            '{h['zsh']}'
            '{h['fish']}'
            '{h['licence']}')

package() {{
  case "$CARCH" in
    x86_64)  _bin=mdt-linux-amd64 ;;
    aarch64) _bin=mdt-linux-arm64 ;;
  esac
  install -Dm755 "$srcdir/$_bin" "$pkgdir/usr/bin/mdt"
  install -Dm644 "$srcdir/mdt.1"   "$pkgdir/usr/share/man/man1/mdt.1"
  install -Dm644 "$srcdir/mdt.bash" "$pkgdir/usr/share/bash-completion/completions/mdt"
  install -Dm644 "$srcdir/_mdt"     "$pkgdir/usr/share/zsh/site-functions/_mdt"
  install -Dm644 "$srcdir/mdt.fish" "$pkgdir/usr/share/fish/vendor_completions.d/mdt.fish"
  install -Dm644 "$srcdir/LICENSE"  "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}}
'''
    srcinfo = f"""pkgbase = mdt-bin
\tpkgdesc = {DESC}
\tpkgver = {version}
\tpkgrel = 1
\turl = {HOMEPAGE}
\tarch = x86_64
\tarch = aarch64
\tlicense = {LICENCE}
\tprovides = mdt
\tconflicts = mdt
\tconflicts = mdt-git
\toptdepends = curl: fetch images that are linked from a document
\toptdepends = xdg-utils: open links in your browser
\tsource_x86_64 = mdt-linux-amd64::https://github.com/{REPO}/releases/download/v{version}/mdt-linux-amd64
\tsource_aarch64 = mdt-linux-arm64::https://github.com/{REPO}/releases/download/v{version}/mdt-linux-arm64
\tsource = mdt.1::https://raw.githubusercontent.com/{REPO}/v{version}/docs/mdt.1
\tsource = mdt.bash::https://raw.githubusercontent.com/{REPO}/v{version}/completions/mdt.bash
\tsource = _mdt::https://raw.githubusercontent.com/{REPO}/v{version}/completions/_mdt
\tsource = mdt.fish::https://raw.githubusercontent.com/{REPO}/v{version}/completions/mdt.fish
\tsource = LICENSE::https://raw.githubusercontent.com/{REPO}/v{version}/LICENSE
\tsha256sums_x86_64 = {h['linux']}
\tsha256sums_aarch64 = {h['linux_arm64']}
\tsha256sums = {h['man']}
\tsha256sums = {h['bash']}
\tsha256sums = {h['zsh']}
\tsha256sums = {h['fish']}
\tsha256sums = {h['licence']}
\toptions = !strip
\toptions = !debug

pkgname = mdt-bin
"""
    return pkgbuild, srcinfo


def write(path: pathlib.Path, text: str, check: bool) -> bool:
    """True when the file on disk already had this content."""
    old = path.read_text() if path.exists() else None
    same = old == text
    if not check and not same:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
    verb = "unchanged" if same else ("would update" if check else "wrote")
    print(f"  {verb:<12} {path.relative_to(HERE.parent)}")
    return same


def publish(version: str, check: bool) -> None:
    """Push the two manifests that live in their own repositories."""
    import os
    import shutil
    import subprocess
    import tempfile

    token = os.environ.get("GITHUB_TOKEN")
    if not token and not check:
        # --check only reads, and the two repositories are public, so it works
        # without a token at all.
        sys.exit("publish: set GITHUB_TOKEN (a token that can push to the two repos)")

    def api(method: str, path: str, payload: dict | None = None) -> tuple[int, dict]:
        url = f"https://api.github.com{path}"
        data = json.dumps(payload).encode() if payload is not None else None
        req = urllib.request.Request(url, data=data, method=method)
        if token:
            req.add_header("Authorization", f"Bearer {token}")
        req.add_header("Accept", "application/vnd.github+json")
        if data:
            req.add_header("Content-Type", "application/json")
        try:
            with urllib.request.urlopen(req, timeout=120) as r:
                return r.status, json.loads(r.read() or b"{}")
        except urllib.error.HTTPError as e:
            return e.code, {"message": json.loads(e.read() or b"{}").get("message", "")}

    scoop_readme = f"""# A Scoop bucket for [mdt]({HOMEPAGE})

```powershell
scoop bucket add wudream813 https://github.com/wudream813/scoop-bucket
scoop install wudream813/mdt
```

`mdt` is a terminal Markdown reader that typesets maths and draws images with
the terminal's graphics protocol; the manifest installs the Windows release zip
and puts `mdt.exe` on your PATH.  `scoop update mdt` follows new releases.

（中文：这是 mdt 的 Scoop 安装源，装完直接 `mdt 文件.md`。）
"""

    brew_readme = f"""# A Homebrew tap for [mdt]({HOMEPAGE})

```sh
brew tap wudream813/mdt
brew install mdt
```

Formulas install the published release binaries - Apple silicon on macOS,
static x86-64 or arm64 on Linux - plus the man page and the bash/zsh/fish
completions from the same tag.

（中文：这是 mdt 的 Homebrew 安装源，macOS 为 Apple 芯片，Linux 支持 x86-64 与 arm64。）
"""

    targets = [
        ("wudream813/scoop-bucket", {
            "bucket/mdt.json": (HERE / "scoop/mdt.json").read_text(),
            "README.md": scoop_readme,
        }),
        ("wudream813/homebrew-mdt", {
            "Formula/mdt.rb": (HERE / "homebrew/mdt.rb").read_text(),
            "README.md": brew_readme,
        }),
    ]
    if check:
        for repo, files in targets:
            status, _ = api("GET", f"/repos/{repo}")
            print(f"  {'exists' if status == 200 else 'missing':<12} {repo}")
        return

    for repo, files in targets:
        status, body = api("GET", f"/repos/{repo}")
        if status == 404:
            status, body = api("POST", "/user/repos", {
                "name": repo.split("/")[1],
                "description": f"Manifests for {REPO} ({repo.split('/')[1].split('-')[-1]})",
                "homepage": HOMEPAGE,
                "has_issues": False,
                "has_wiki": False,
                "auto_init": True,
            })
            print(f"  created      {repo} (HTTP {status})")
            if status not in (200, 201):
                print(f"  could not create {repo}: {body.get('message')}")
                continue
        elif status != 200:
            print(f"  could not read {repo}: HTTP {status} {body.get('message')}")
            continue

        try:
            with tempfile.TemporaryDirectory() as tmp:
                url = f"https://x-access-token:{token}@github.com/{repo}.git"
                subprocess.run(["git", "clone", "-q", url, tmp], check=True)
                for name, text in files.items():
                    dest = pathlib.Path(tmp) / name
                    dest.parent.mkdir(parents=True, exist_ok=True)
                    old = dest.read_text() if dest.exists() else None
                    if old == text:
                        print(f"  unchanged    {repo}/{name}")
                        continue
                    dest.write_text(text)
                    subprocess.run(["git", "-C", tmp, "add", name], check=True)
                if subprocess.run(["git", "-C", tmp, "diff", "--cached", "--quiet"]).returncode == 0:
                    continue
                subprocess.run(["git", "-C", tmp, "-c", "user.name=wudream813",
                                "-c", "user.email=wudream813@users.noreply.github.com",
                                "commit", "-q", "-m", f"mdt {version}"], check=True)
                subprocess.run(["git", "-C", tmp, "push", "-q", "origin", "HEAD"], check=True)
                print(f"  pushed       {repo}: mdt {version}")
        except subprocess.CalledProcessError as exc:
            # A fine-grained token only reaches the repositories it was scoped
            # to, and "create a repository" is not the same permission as
            # "write into it".
            print(f"  FAILED       {repo}: {exc}")
            print(f"               the token needs Contents: Read and write on {repo}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("tag", help="the release tag to point the manifests at, e.g. v0.3.9")
    ap.add_argument("--from", dest="from_dir", type=pathlib.Path,
                    help="use already-downloaded files from this directory instead of downloading")
    ap.add_argument("--check", action="store_true", help="report drift, write nothing, push nothing")
    ap.add_argument("--publish", action="store_true", help="also push the Scoop and Homebrew repos")
    args = ap.parse_args()

    tag = args.tag if args.tag.startswith("v") else "v" + args.tag
    version = tag[1:]
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        sys.exit(f"unexpected version '{version}', want vX.Y.Z")

    print(f"collecting {tag}:")
    blobs = collect(tag, version, args.from_dir)
    h = {k: sha256(v) for k, v in blobs.items()}

    print(f"writing manifests for {version}:")
    pkgbuild, srcinfo = aur(version, h)
    written = [
        write(HERE / "scoop/mdt.json", scoop(version, h), args.check),
        write(HERE / "homebrew/mdt.rb", homebrew(version, h), args.check),
        write(HERE / "aur/PKGBUILD", pkgbuild, args.check),
        write(HERE / "aur/.SRCINFO", srcinfo, args.check),
    ]
    for name, text in winget(version, h).items():
        written.append(write(HERE / "winget" / name, text, args.check))

    if args.publish or args.check:
        print("channel repositories:")
        publish(version, args.check)

    if args.check and all(written):
        print("every manifest already matches this release")
    return 0


if __name__ == "__main__":
    sys.exit(main())
