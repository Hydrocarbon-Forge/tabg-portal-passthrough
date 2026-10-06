#!/usr/bin/env bash
# run_all_tests.sh — run every host-side acceptance test that works on this box.
# Slices that only build on Windows (the Portal plugin shipping build, the TABG
# plugin install) are not exercised here; their local build/syntax checks are.
set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$REPO"
export DOTNET_ROOT="${DOTNET_ROOT:-$HOME/.dotnet}"
export PATH="$HOME/.dotnet:$HOME/.dotnet/tools:$PATH"

pass=0; fail=0; skip=0
run() {  # run <name> <cmd...>
    local name="$1"; shift
    printf '\n=== %s ===\n' "$name"
    if ! command -v "$1" >/dev/null 2>&1 && [ ! -e "$1" ]; then
        echo "SKIP (missing: $1)"; skip=$((skip+1)); return
    fi
    if [ "$#" -ge 2 ] && [[ "$2" == *.sh || "$2" == *.py ]] && [ ! -e "$2" ]; then
        echo "SKIP (missing: $2)"; skip=$((skip+1)); return
    fi
    if "$@"; then echo "[$name] OK"; pass=$((pass+1))
    else echo "[$name] FAILED (rc=$?)"; fail=$((fail+1)); fi
}

run "A: link latency"          ./run_link_test.sh 10
run "B: frame channel"         python3 tests/framechannel_test.py --seconds 4
run "B: C++/Python interop"    python3 tests/framechannel_cpp_interop.py
run "C: portal stub compile"   bash src/portal_plugin/build_stubcheck.sh
run "C: fake_portal harness"   bash tests/fake_portal/run_fake_portal_test.sh
run "D: tabg plugin build"     dotnet build -c Release src/tabg_plugin
run "E: director"              python3 tests/director_test.py
run "E: cut pipeline"          python3 tools/cut.py --selftest
run "G.1: server patch check"  python3 tools/patch_server.py --check

printf '\n========================================\n'
printf 'PASS=%d  FAIL=%d  SKIP=%d\n' "$pass" "$fail" "$skip"
[ "$fail" -eq 0 ] && [ "$skip" -eq 0 ]

