#!/usr/bin/env bash
# run.sh — fake-Portal harness integration test (Slice C).
# Drives fake_portal end-to-end:
#   1. Starts link hub on 127.0.0.1:25600
#   2. Starts fake_portal guest (links passthrough.o logic)
#   3. Drives synthetic orbiting host poses via pose_orbit.py --no-guest
#   4. Validates framechannel slots, pose freshness, pixel gradient, and depth via FrameReader.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE" && while [ ! -f "README.md" ] && [ "$PWD" != "/" ]; do cd ..; done && pwd)"
cd "$REPO"

# Clean stale shared memory
rm -f /dev/shm/TABGPortalFrame*

# 1. Build fake_portal binary if needed
if [ ! -x "$HERE/fake_portal" ]; then
    bash "$HERE/build.sh"
fi

HUB_PID=""
PORTAL_PID=""

cleanup() {
    if [ -n "$PORTAL_PID" ] && kill -0 "$PORTAL_PID" 2>/dev/null; then
        kill -9 "$PORTAL_PID" 2>/dev/null || true
    fi
    if [ -n "$HUB_PID" ] && kill -0 "$HUB_PID" 2>/dev/null; then
        kill -9 "$HUB_PID" 2>/dev/null || true
    fi
    rm -f /dev/shm/TABGPortalFrame*
}
trap cleanup EXIT INT TERM

# 2. Start Link Hub
python3 src/link/link.py --port 25600 >/dev/null 2>&1 &
HUB_PID=$!
sleep 0.5

# 3. Start fake_portal
"$HERE/fake_portal" --seconds 10 >/dev/null 2>&1 &
PORTAL_PID=$!
sleep 0.5

# 4. Run pose_orbit.py with --no-guest (fake_portal is the guest)
ORBIT_OUT="$HERE/orbit_results.json"
python3 tools/pose_orbit.py --no-guest --seconds 3 --hz 60 --p99-ms 16.0 --out "$ORBIT_OUT"

# 5. Read frame from framechannel and verify pixels + depth + pose freshness
OUT_FRAMES="/tmp/fake_portal_frames"
python3 src/framechannel/framechannel.py --name TABGPortalFrame --out "$OUT_FRAMES" --wait 3.0

python3 - << 'EOF'
import sys
import numpy as np
from pathlib import Path

sys.path.insert(0, "src/framechannel")
import framechannel as fc

reader = fc.FrameReader("TABGPortalFrame")
fr = reader.latest()
if fr is None:
    sys.exit("FAIL: no frame available in TABGPortalFrame")

print(f"[fake_portal_test] read frame {fr.frame_index}: {fr.width}x{fr.height}, pos={tuple(round(v,2) for v in fr.pos)}")

if fr.width != 1920 or fr.height != 1080:
    sys.exit(f"FAIL: unexpected resolution {fr.width}x{fr.height}")

if fr.frame_index <= 0:
    sys.exit(f"FAIL: invalid frame_index {fr.frame_index}")

# Verify synthetic gradient: R = horizontal ramp, G = vertical ramp
assert fr.rgba[0, 0, 0] == 0, f"R top-left expected 0, got {fr.rgba[0, 0, 0]}"
assert fr.rgba[0, -1, 0] >= 254, f"R top-right expected >=254, got {fr.rgba[0, -1, 0]}"
assert fr.rgba[0, 0, 1] == 0, f"G top-left expected 0, got {fr.rgba[0, 0, 1]}"
assert fr.rgba[-1, 0, 1] >= 254, f"G bottom-left expected >=254, got {fr.rgba[-1, 0, 1]}"

# Verify depth ramp: reversed-Z (near ~1.0, far ~0.05)
assert fr.depth[0, 0] > 0.95, f"depth near expected >0.95, got {fr.depth[0, 0]}"
assert fr.depth[0, -1] < 0.1, f"depth far expected <0.1, got {fr.depth[0, -1]}"

print("PASS: fake_portal end-to-end camera drive + framechannel export verified")
EOF

echo "[fake_portal] ALL CHECKS PASSED"
