#!/usr/bin/env python3
"""Build the .deb and .rpm packages for a release.

    python3 packaging/pkgs.py v0.3.17                     # download the release files
    python3 packaging/pkgs.py v0.3.17 --from ~/download   # use files you already have
    python3 packaging/pkgs.py v0.3.17 --out dist          # where to put the packages

Both architectures are packaged from the same static binaries the release ships
(`mdt-linux-amd64`, `mdt-linux-arm64`), so the packages depend on **nothing**:
the binary carries its own libc.  `curl` and `xdg-utils` are recommended rather
than required - remote pictures shell out to curl, and the `o` key opens links
with xdg-open.  The man page and the three completions ride along.

The .deb is assembled with dpkg-deb, the .rpm with rpmbuild, which accepts a
`--target` other than the host architecture - nothing is compiled here, the
payload is already built, so one x86-64 machine produces all four packages.
`dpkg-deb` is part of dpkg; `rpmbuild` comes from the `rpm` package.
"""
from __future__ import annotations

import argparse
import hashlib
import pathlib
import shutil
import subprocess
import sys
import tempfile
import urllib.request

REPO = "wudream813/markdown-in-terminal"
HOMEPAGE = f"https://github.com/{REPO}"
MAINTAINER = "wudream813 <wudream813@users.noreply.github.com>"
SUMMARY = "Terminal Markdown reader: typeset maths, images, syntax highlighting"
LONG = (
    "mdt reads a Markdown file in the terminal and draws what a terminal pager "
    "normally cannot: formulas typeset by an embedded KaTeX-compatible engine, "
    "images through the kitty/iTerm2/sixel graphics protocols, tables, alerts, "
    "footnotes.  One self-contained C++17 binary, no runtime dependencies."
)

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent

# architecture -> (release asset, Debian name, RPM name)
ARCHES = {
    "amd64": ("mdt-linux-amd64", "amd64", "x86_64"),
    "arm64": ("mdt-linux-arm64", "arm64", "aarch64"),
}

# where each file goes inside the package (source path -> install path)
PAYLOAD = {
    "docs/mdt.1": "usr/share/man/man1/mdt.1",
    "completions/mdt.bash": "usr/share/bash-completion/completions/mdt",
    "completions/_mdt": "usr/share/zsh/site-functions/_mdt",
    "completions/mdt.fish": "usr/share/fish/vendor_completions.d/mdt.fish",
    "LICENSE": "usr/share/licenses/mdt/LICENSE",
}
MODES = {"usr/bin/mdt": 0o755}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def fetch(url: str) -> bytes:
    req = urllib.request.Request(url, headers={"User-Agent": "mdt-packaging/1"})
    with urllib.request.urlopen(req, timeout=120) as r:
        return r.read()


def have(tool: str) -> bool:
    return shutil.which(tool) is not None


def collect(tag: str, from_dir: pathlib.Path | None, version: str) -> dict[str, bytes]:
    """The two binaries (downloaded or from --from) plus the files in this repo."""
    files: dict[str, bytes] = {}
    for _arch, (asset, _deb, _rpm) in ARCHES.items():
        local = from_dir / asset if from_dir else None
        if local and local.exists():
            files[asset] = local.read_bytes()
        else:
            files[asset] = fetch(f"https://github.com/{REPO}/releases/download/{tag}/{asset}")
    for src in PAYLOAD:
        files[src] = (ROOT / src).read_bytes()
    # the version in the packages must be the one the binaries report
    assert version and version[0].isdigit(), version
    return files


def stage_tree(files: dict[str, bytes], binary: str, root: pathlib.Path) -> list[str]:
    """Lay the payload out under root/ and return the installed paths."""
    installed: list[str] = []
    for src, dest in PAYLOAD.items():
        path = root / dest
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(files[src])
        path.chmod(MODES.get(dest, 0o644))
        installed.append("/" + dest)
    path = root / "usr/bin/mdt"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(files[binary])
    path.chmod(0o755)
    installed.insert(0, "/usr/bin/mdt")
    return installed


def wrap(text: str, width: int = 76) -> list[str]:
    """Word wrap for the long descriptions (72-76 columns is the convention)."""
    lines: list[str] = []
    current = ""
    for word in text.split():
        if current and len(current) + 1 + len(word) > width:
            lines.append(current)
            current = word
        else:
            current = f"{current} {word}".strip()
    if current:
        lines.append(current)
    return lines


def deb_control(version: str, arch: str, installed_kb: int) -> str:
    long_lines = "\n".join(" " + line for line in wrap(LONG))
    return f"""Package: mdt
Version: {version}-1
Architecture: {arch}
Maintainer: {MAINTAINER}
Installed-Size: {installed_kb}
Section: utils
Priority: optional
Homepage: {HOMEPAGE}
Recommends: curl, xdg-utils
Description: {SUMMARY}
{long_lines}
"""


def build_deb(files: dict[str, bytes], version: str, arch_key: str, out: pathlib.Path) -> pathlib.Path:
    binary, deb_arch, _rpm = ARCHES[arch_key]
    with tempfile.TemporaryDirectory() as tmp:
        root = pathlib.Path(tmp) / "root"
        installed = stage_tree(files, binary, root)
        size_kb = sum((root / p.lstrip("/")).stat().st_size for p in installed) // 1024 + 1
        control = root / "DEBIAN"
        control.mkdir(parents=True)
        (control / "control").write_text(deb_control(version, deb_arch, size_kb))
        (control / "control").chmod(0o644)
        target = out / f"mdt_{version}-1_{deb_arch}.deb"
        subprocess.run(
            ["dpkg-deb", "--root-owner-group", "--build", str(root), str(target)],
            check=True, stdout=subprocess.DEVNULL,
        )
        return target


def rpm_spec(version: str) -> str:
    long_text = "\n".join(wrap(LONG))
    # rpm's brp-compress gzips man pages before packaging, so that one is a
    # glob (the .deb keeps the file as it ships in the release)
    files = "\n".join(
        "/usr/share/man/man1/mdt.1*" if path.endswith("man1/mdt.1") else "/" + path
        for path in sorted(PAYLOAD.values()) if not path.endswith("LICENSE"))
    return f"""Name:           mdt
Version:        {version}
Release:        1
Summary:        {SUMMARY}
License:        MIT
URL:            {HOMEPAGE}
Recommends:     curl
Recommends:     xdg-utils
AutoReqProv:    no

%global debug_package %{{nil}}
%global _build_id_links none

%description
{long_text}

%prep

%build

%install
mkdir -p %{{buildroot}}
cp -a %{{_stage}}/. %{{buildroot}}/

%files
/usr/bin/mdt
{files}
%license /usr/share/licenses/mdt/LICENSE

%changelog
"""


def build_rpm(files: dict[str, bytes], version: str, arch_key: str, out: pathlib.Path) -> pathlib.Path:
    binary, _deb_arch, rpm_arch = ARCHES[arch_key]
    with tempfile.TemporaryDirectory() as tmp:
        tmpdir = pathlib.Path(tmp)
        stage = tmpdir / "stage"
        stage_tree(files, binary, stage)
        top = tmpdir / "top"
        (top / "SPECS").mkdir(parents=True)
        for sub in ("BUILD", "RPMS", "SOURCES", "SRPMS"):
            (top / sub).mkdir()
        (top / "rpmdb").mkdir()
        spec = top / "SPECS" / "mdt.spec"
        # No BuildArch: the architecture comes from --target (below), which rpm
        # accepts for an architecture other than the host.  BuildArch would make
        # rpm demand a build host of that architecture instead.
        spec.write_text(rpm_spec(version))
        subprocess.run(
            ["rpmbuild", "-bb",
             "--define", f"_topdir {top}",
             "--define", f"_stage {stage}",
             # keep rpm from looking at (and failing to open) the system rpmdb
             "--define", f"_dbpath {top}/rpmdb",
             "--target", rpm_arch,
             str(spec)],
            check=True, stdout=subprocess.DEVNULL,
        )
        built = next((top / "RPMS").glob(f"*/mdt-{version}-1.{rpm_arch}.rpm"))
        target = out / built.name
        shutil.copy2(built, target)
        return target


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("tag", help="the release tag, e.g. v0.3.17")
    ap.add_argument("--from", dest="from_dir", type=pathlib.Path,
                    help="a directory holding the release binaries (skips the download)")
    ap.add_argument("--out", dest="out", type=pathlib.Path, default=pathlib.Path("dist"),
                    help="where to write the packages (default: dist)")
    args = ap.parse_args()

    version = args.tag.lstrip("v")
    for tool, why in (("dpkg-deb", "the .deb"), ("rpmbuild", "the .rpm")):
        if not have(tool):
            sys.exit(f"packaging/pkgs.py: {tool} is missing (needed for {why}); "
                     f"install dpkg / the rpm package")

    files = collect(args.tag, args.from_dir, version)
    args.out.mkdir(parents=True, exist_ok=True)
    print(f"building packages for {version}:")
    for arch_key in ARCHES:
        for build in (build_deb, build_rpm):
            path = build(files, version, arch_key, args.out)
            digest = sha256(path.read_bytes())
            print(f"  {path.name:34s} {path.stat().st_size:>9d} B  sha256 {digest[:16]}…")
    return 0


if __name__ == "__main__":
    sys.exit(main())
