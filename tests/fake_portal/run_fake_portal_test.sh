#!/usr/bin/env bash
# run_fake_portal_test.sh — entry point for fake_portal test suite
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
exec bash "$REPO/src/portal_plugin/tests/fake_portal/run.sh" "$@"
