#!/bin/bash
# build_stubcheck.sh — compile the Portal server plugin on THIS Linux box as a
# syntax/type check only (no Source SDK, no GPU). It compiles passthrough.cpp and
# ws.cpp to object files against the minimal sdk_stubs/ and the real
# src/framechannel/cpp/framechannel.h, proving the plugin source is well-formed
# and that it uses the shared frame-channel layout without redefining it.
#
# The shipped artifact is a 32-bit Windows DLL built with MSVC (build.bat); this
# script does NOT produce that. It only guards against source rot here.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
FC="$HERE/../framechannel/cpp"
OUT="$HERE/build_stubcheck"
mkdir -p "$OUT"

CXXFLAGS="-std=c++17 -Wall -Wextra -Wno-unused-parameter -c -I$HERE -I$FC"

echo "g++ $(g++ -dumpversion) : compiling ws.cpp"
g++ $CXXFLAGS "$HERE/ws.cpp" -o "$OUT/ws.o"

echo "g++ : compiling passthrough.cpp against sdk_stubs + framechannel.h"
g++ $CXXFLAGS "$HERE/passthrough.cpp" -o "$OUT/passthrough.o"

echo "STUBCHECK PASS: objects in $OUT"
ls -l "$OUT"
