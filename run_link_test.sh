#!/usr/bin/env bash
# run_link_test.sh — slice 2 acceptance: start the link hub, drive it at 60 Hz
# with pose_orbit.py, measure round-trip latency, and gate on p99 < 16 ms.
# Writes tests/link_results.json. Reproducible on this Linux box.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$REPO"

SECONDS_ARG="${1:-20}"
PORT="${PORT:-25600}"
URL="ws://127.0.0.1:${PORT}"

echo "[run_link_test] starting hub on :${PORT}"
python3 src/link/link.py --port "$PORT" &
HUB_PID=$!
cleanup() { kill "$HUB_PID" 2>/dev/null || true; wait "$HUB_PID" 2>/dev/null || true; }
trap cleanup EXIT

# wait for the port to accept connections
for _ in $(seq 1 50); do
    if python3 - "$PORT" <<'PY' 2>/dev/null
import socket, sys
s = socket.socket(); s.settimeout(0.2)
try:
    s.connect(("127.0.0.1", int(sys.argv[1]))); s.close()
except OSError:
    sys.exit(1)
PY
    then break; fi
    sleep 0.1
done

echo "[run_link_test] driving ${SECONDS_ARG}s at 60 Hz"
python3 tools/pose_orbit.py --url "$URL" --seconds "$SECONDS_ARG" \
    --out tests/link_results.json --p99-ms 16.0
RC=$?

echo "[run_link_test] result: $([ $RC -eq 0 ] && echo PASS || echo FAIL) (rc=$RC)"
exit $RC
