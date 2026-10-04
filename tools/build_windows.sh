#!/bin/sh
# Builds the Windows release (MinGW-w64) and packages a zip.
#
#   sh tools/build_windows.sh              # 64 bit
#   ARCH=32 sh tools/build_windows.sh      # 32 bit
#
# The result is release/mdt-<version>-win64/{mdt.exe,README.md,demo/} plus a zip.
set -e

cd "$(dirname "$0")/.."
VERSION=$(sed -n 's/^VERSION  ?= //p' Makefile | head -1)
ARCH=${ARCH:-64}

if [ "$ARCH" = "32" ]; then
  PREFIX=${MINGW_PREFIX:-i686-w64-mingw32}
  TAG=win32
else
  PREFIX=${MINGW_PREFIX:-x86_64-w64-mingw32}
  TAG=win64
fi

if ! command -v "${PREFIX}-g++" >/dev/null 2>&1; then
  echo "error: ${PREFIX}-g++ not found." >&2
  echo "       debian/ubuntu: sudo apt install mingw-w64" >&2
  echo "       fedora:        sudo dnf install mingw64-gcc-c++ mingw32-gcc-c++" >&2
  echo "       macos:         brew install mingw-w64" >&2
  exit 1
fi

echo "== building mdt $VERSION for Windows ($TAG, ${PREFIX}) =="
make -j"$(nproc 2>/dev/null || echo 4)" \
     BUILD=build/win \
     CXX="${PREFIX}-g++" CC="${PREFIX}-gcc" \
     VERSION="$VERSION"

STAGE="release/mdt-${VERSION}-${TAG}"
rm -rf "$STAGE"
mkdir -p "$STAGE/demo"
cp build/win/mdt.exe "$STAGE/"
cp README.md "$STAGE/"
cp demo/demo.md demo/logo.png "$STAGE/demo/"

# dependency check: a static build must only pull in system DLLs
if command -v "${PREFIX}-objdump" >/dev/null 2>&1; then
  echo "== imported DLLs =="
  "${PREFIX}-objdump" -p build/win/mdt.exe | awk '/DLL Name/ {print "   " $3}' | sort -u
  if "${PREFIX}-objdump" -p build/win/mdt.exe | grep -qiE "libgcc|libstdc\+\+|libwinpthread"; then
    echo "warning: MinGW runtime DLLs are required - the build may not run on a clean system" >&2
  else
    echo "   (no MinGW runtime DLLs needed)"
  fi
fi

echo "== packaging =="
( cd release && zip -qr "mdt-${VERSION}-${TAG}.zip" "$(basename "$STAGE")" )
ls -la "$STAGE/mdt.exe" "release/mdt-${VERSION}-${TAG}.zip"
echo "done: $STAGE"
