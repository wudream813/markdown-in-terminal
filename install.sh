#!/bin/sh
# Installs mdt (the prebuilt binary for this machine) without a package manager.
#
#   curl -fsSL https://raw.githubusercontent.com/wudream813/markdown-in-terminal/main/install.sh | sh
#   sh install.sh --prefix "$HOME/.local/bin"
#   sh install.sh --version v0.3.8
#
# What it does: pick the release asset for this OS/architecture, download it,
# check that it runs, copy it next to your other programs.  Nothing else - no
# config file, no shell profile edit.  Read the script before piping it into a
# shell; it is short on purpose.
set -eu

REPO="wudream813/markdown-in-terminal"
VERSION="latest"
PREFIX=""

usage() {
  cat <<'EOF'
usage: install.sh [--prefix DIR] [--version vX.Y.Z] [--help]

  --prefix DIR      where to put the binary (default: /usr/local/bin when that
                    is writable, otherwise $HOME/.local/bin)
  --version vX.Y.Z  a specific release instead of the latest one
EOF
}

while [ $# -gt 0 ]; do
  case "$1" in
    --prefix) PREFIX="${2:?--prefix needs a directory}"; shift 2 ;;
    --prefix=*) PREFIX="${1#*=}"; shift ;;
    --version) VERSION="${2:?--version needs a tag}"; shift 2 ;;
    --version=*) VERSION="${1#*=}"; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "install.sh: unknown argument '$1'" >&2; usage >&2; exit 2 ;;
  esac
done

os=$(uname -s 2>/dev/null || echo unknown)
arch=$(uname -m 2>/dev/null || echo unknown)
case "$os" in
  Linux)  os_tag=linux ;;
  Darwin) os_tag=macos ;;
  *) echo "install.sh: no prebuilt binary for '$os'.  Build from source:" >&2
     echo "  git clone https://github.com/$REPO && cd markdown-in-terminal && make -j" >&2
     echo "Windows: download mdt-win64.exe (or the .zip) from the releases page." >&2
     exit 1 ;;
esac
case "$arch" in
  x86_64|amd64) [ "$os_tag" = linux ] && asset=mdt-linux-amd64 || { echo "install.sh: macOS builds are Apple silicon only; build from source" >&2; exit 1; } ;;
  aarch64|arm64) [ "$os_tag" = macos ] && asset=mdt-macos-arm64 || { echo "install.sh: no prebuilt arm64 Linux binary; build from source" >&2; exit 1; } ;;
  *) echo "install.sh: no prebuilt binary for '$arch'" >&2; exit 1 ;;
esac

if [ "$VERSION" = latest ]; then
  url="https://github.com/$REPO/releases/latest/download/$asset"
else
  url="https://github.com/$REPO/releases/download/$VERSION/$asset"
fi

tmp=$(mktemp -d "${TMPDIR:-/tmp}/mdt-install-XXXXXX")
trap 'rm -rf "$tmp"' EXIT INT TERM

echo "downloading $asset ($VERSION)"
if command -v curl >/dev/null 2>&1; then
  fetch() { curl -fsSL -o "$1" "$2"; }
elif command -v wget >/dev/null 2>&1; then
  fetch() { wget -q -O "$1" "$2"; }
else
  echo "install.sh: neither curl nor wget is available" >&2
  exit 1
fi
if ! fetch "$tmp/mdt" "$url"; then
  echo "install.sh: could not download $url" >&2
  [ "$VERSION" = latest ] || echo "  (is '$VERSION' a released tag?  see https://github.com/$REPO/releases)" >&2
  exit 1
fi
chmod +x "$tmp/mdt"

# The binary must be able to say what it is before it lands in your PATH.
if ! version_out=$("$tmp/mdt" --version 2>/dev/null); then
  echo "install.sh: the downloaded binary did not run - your platform may be too old for it." >&2
  echo "  Build from source instead: git clone https://github.com/$REPO && cd markdown-in-terminal && make -j" >&2
  exit 1
fi
echo "downloaded $version_out"

if [ -z "$PREFIX" ]; then
  if [ -w /usr/local/bin ] 2>/dev/null; then
    PREFIX=/usr/local/bin
  elif [ -w /usr/local ] 2>/dev/null; then
    PREFIX=/usr/local/bin
  else
    PREFIX="$HOME/.local/bin"
  fi
fi
mkdir -p "$PREFIX"
install -m 0755 "$tmp/mdt" "$PREFIX/mdt" 2>/dev/null || cp "$tmp/mdt" "$PREFIX/mdt"
echo "installed $PREFIX/mdt"

case ":$PATH:" in
  *":$PREFIX:"*) ;;
  *) echo
     echo "note: $PREFIX is not in your PATH.  Add it, for example:"
     echo "  echo 'export PATH=\"$PREFIX:\$PATH\"' >> ~/.profile" ;;
esac

echo
echo "next:  mdt --list-caps   # cell size, graphics protocol, fonts"
echo "       mdt README.md"
