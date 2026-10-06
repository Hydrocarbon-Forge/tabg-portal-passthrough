#!/bin/bash
# build.sh — build passthrough.dll under WSL/Linux IF a Source SDK + MinGW-w64
# (32-bit) toolchain is available. This box has neither (no SDK, no mingw), so
# this script is for the Windows/WSL build box; use build_stubcheck.sh here for
# the syntax check.
#
#   SDK=/path/to/source-sdk-2013/sp/src ./build.sh
#
# Requires i686-w64-mingw32-g++ and the SDK import libs. The plugin is 32-bit
# (recon §1). On a box without these, this prints what is missing and exits 1.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
FC="$HERE/../framechannel/cpp"
CXX="${CXX:-i686-w64-mingw32-g++}"

if ! command -v "$CXX" >/dev/null 2>&1; then
  echo "no 32-bit MinGW cross-compiler ($CXX). Use build.bat on Windows, or"
  echo "build_stubcheck.sh for the syntax check on this box."
  exit 1
fi
if [ -z "$SDK" ]; then
  echo "Set SDK to your source-sdk-2013/sp/src checkout."
  exit 1
fi

mkdir -p "$HERE/build"
"$CXX" -m32 -shared -O2 -fno-exceptions -fno-rtti -static \
  -DWIN32 -DWIN32_LEAN_AND_MEAN -DNOMINMAX \
  -I "$SDK/public" -I "$SDK/public/tier0" -I "$SDK/public/tier1" \
  -I "$SDK/common" -I "$FC" -I "$HERE" \
  "$HERE/passthrough.cpp" "$HERE/ws.cpp" \
  -o "$HERE/build/passthrough.dll" \
  -lws2_32 -luser32 \
  "$SDK/lib/public/tier0.lib" "$SDK/lib/public/tier1.lib"
echo "built build/passthrough.dll (x86)"
