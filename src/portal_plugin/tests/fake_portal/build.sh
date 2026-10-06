#!/usr/bin/env bash
# build.sh — build the fake_portal test binary
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PORTAL_DIR="$(cd "$HERE/../.." && pwd)"
FC_DIR="$PORTAL_DIR/../framechannel/cpp"
OUT="$HERE"

echo "[fake_portal] building ws.o and passthrough.o..."
g++ -std=c++17 -O3 -Wall -Wextra -Wno-unused-parameter -c -I"$PORTAL_DIR" -I"$FC_DIR" \
    "$PORTAL_DIR/ws.cpp" -o "$OUT/ws.o"

g++ -std=c++17 -O3 -Wall -Wextra -Wno-unused-parameter -c -I"$PORTAL_DIR" -I"$FC_DIR" \
    "$PORTAL_DIR/passthrough.cpp" -o "$OUT/passthrough.o"

echo "[fake_portal] compiling fake_portal executable..."
g++ -std=c++17 -O3 -Wall -Wextra -Wno-unused-parameter -I"$PORTAL_DIR" -I"$FC_DIR" \
    "$HERE/fake_portal.cpp" "$OUT/passthrough.o" "$OUT/ws.o" \
    -pthread -o "$OUT/fake_portal"

echo "[fake_portal] build successful: $OUT/fake_portal"
