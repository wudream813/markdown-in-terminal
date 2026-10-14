#!/usr/bin/env python3
"""Check that every URL and sha256 in packaging/ is real.

gen.py trusts the files it just wrote; this trusts nothing.  It reads the
manifests as text, pulls out every (url, sha256) pair it can find, downloads
each URL and compares the hash.  Run it after a release, before publishing the
manifests anywhere - it is the difference between "the version strings look
right" and "scoop/winget/brew will not fail on a 404 or a stale hash".

    python3 packaging/verify.py
"""
from __future__ import annotations

import hashlib
import json
import pathlib
import re
import sys
import urllib.request

HERE = pathlib.Path(__file__).resolve().parent
PAIRS: list[tuple[str, str, str]] = []  # (where, url, sha256)


def add(where: str, url: str, digest: str) -> None:
    if url and digest:
        PAIRS.append((where, url, digest.lower()))


def from_scoop(path: pathlib.Path) -> None:
    d = json.loads(path.read_text())
    for arch, spec in d["architecture"].items():
        add(f"{path.name}[{arch}]", spec["url"], spec["hash"])
        auto = d.get("autoupdate", {}).get("architecture", {}).get(arch, {}).get("url")
        if auto:
            # $version is the placeholder scoop fills in; check the shape only
            assert "$version" in auto, f"{path.name}: autoupdate url has no $version"
    assert d["version"] in d["architecture"]["64bit"]["url"], f"{path.name}: version not in url"


def from_homebrew(path: pathlib.Path) -> None:
    text = path.read_text()
    for url, digest in re.findall(r'url "([^"]+)"\s*\n\s*sha256 "([0-9a-f]{64})"', text):
        add(path.name, url, digest)
    assert text.count("sha256") == text.count('sha256 "'), f"{path.name}: a sha256 is missing"


def from_winget(path: pathlib.Path) -> None:
    text = path.read_text()
    for url, digest in re.findall(r"InstallerUrl:\s*(\S+)\s*\n\s*InstallerSha256:\s*([0-9a-fA-F]{64})", text):
        add(path.name, url, digest)
    for url, digest in re.findall(r'url "([^"]+)"\s*\n\s*sha256 "([0-9a-f]{64})"', text):
        add(path.name, url, digest)


def from_aur() -> None:
    """The PKGBUILD keeps its source urls in variables, so expand them first."""
    pkgbuild = (HERE / "aur/PKGBUILD").read_text()
    srcinfo = (HERE / "aur/.SRCINFO").read_text()

    def var(name: str) -> str:
        m = re.search(rf"^{name}=[\"']?([^\"'\n]+)[\"']?$", pkgbuild, re.M)
        assert m, f"PKGBUILD: no {name}"
        return m.group(1)

    subs = {"$_gh": var("_gh"), "$_raw": var("_raw"), "$pkgver": var("pkgver")}

    body = re.search(r"source=\((.*?)\n\s*sha256sums", pkgbuild, re.S).group(1)
    urls: list[tuple[str, str]] = []
    for line in body.splitlines():
        line = line.strip().strip('"').rstrip(')').rstrip('"').strip()
        if not line:
            continue
        for name, value in subs.items():
            line = line.replace(name, value)
        stem, sep, url = line.partition("::")
        url = url if sep else stem
        urls.append((stem if sep else url.rsplit("/", 1)[-1], url))

    sums = re.search(r"sha256sums=\((.*?)\)", pkgbuild, re.S).group(1)
    digests = re.findall(r"[0-9a-f]{64}", sums)
    assert len(urls) == len(digests), f"PKGBUILD: {len(urls)} sources but {len(digests)} sums"
    for (_, url), digest in zip(urls, digests):
        add("aur/PKGBUILD", url, digest)

    # .SRCINFO is what the AUR actually serves, it has to agree
    info_sources = re.findall(r"\tsource = (.+)", srcinfo)
    info_sums = re.findall(r"\tsha256sums = ([0-9a-f]{64})", srcinfo)
    assert len(info_sources) == len(info_sums) == len(urls), "SRCINFO: source count differs from the PKGBUILD"
    by_url = {url: digest for (_, url), digest in zip(urls, digests)}
    for src, digest in zip(info_sources, info_sums):
        tail = src.split("::")[-1]
        assert tail in by_url, f"SRCINFO: {tail} is not in the PKGBUILD"
        assert by_url[tail] == digest, f"SRCINFO: hash for {tail} differs from the PKGBUILD"
    assert pkgbuild_version(pkgbuild) == srcinfo_version(srcinfo), "SRCINFO: pkgver differs from the PKGBUILD"


def pkgbuild_version(text: str) -> str:
    return re.search(r"^pkgver=(\S+)", text, re.M).group(1)


def srcinfo_version(text: str) -> str:
    return re.search(r"^\tpkgver = (\S+)", text, re.M).group(1)


def fetch(url: str) -> bytes:
    req = urllib.request.Request(url, headers={"User-Agent": "mdt-packaging/1"})
    with urllib.request.urlopen(req, timeout=300) as r:
        return r.read()


def main() -> int:
    from_scoop(HERE / "scoop/mdt.json")
    from_homebrew(HERE / "homebrew/mdt.rb")
    for w in sorted((HERE / "winget").glob("*.yaml")):
        from_winget(w)
    from_aur()

    if not PAIRS:
        sys.exit("no url/sha256 pairs found - did the manifests move?")

    unique: dict[str, str] = {}
    for where, url, digest in PAIRS:
        unique.setdefault(url, digest)
        if unique[url] != digest:
            sys.exit(f"{where}: {url} has two different hashes in the manifests")

    print(f"{len(PAIRS)} pairs in the manifests, {len(unique)} distinct files:")
    bad = 0
    for url, digest in sorted(unique.items()):
        try:
            data = fetch(url)
        except Exception as exc:  # noqa: BLE001 - the point is to report it
            print(f"  FAIL  {url}: {exc}")
            bad += 1
            continue
        got = hashlib.sha256(data).hexdigest()
        if got == digest:
            print(f"  ok    {url.rsplit('/', 1)[-1]:<24} {len(data):>9} bytes  sha256 {digest[:16]}…")
        else:
            print(f"  FAIL  {url}\n        manifest {digest}\n        actual   {got}")
            bad += 1

    if bad:
        print(f"{bad} problem(s)")
        return 1
    print("every url resolves and every sha256 matches")
    return 0


if __name__ == "__main__":
    sys.exit(main())
