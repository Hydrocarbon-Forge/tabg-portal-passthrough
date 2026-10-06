#!/usr/bin/env bash
# restore_server.sh - restore the TABG dedicated-server Assembly-CSharp.dll from
# the pristine backup created by the patcher (G.1). Linux-side convenience twin
# of restore_server.bat.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DLL="${1:-$REPO/games/TotallyAccurateBattlegroundsDedicatedServer/TABG_Data/Managed/Assembly-CSharp.dll}"
ORIG="$DLL.orig"

if [[ ! -f "$ORIG" ]]; then
    echo "ERROR: backup not found: $ORIG" >&2
    echo "Nothing to restore (was the server ever patched on this machine?)." >&2
    exit 1
fi

cp -f "$ORIG" "$DLL"
echo "Restored original -> $DLL"
echo "(The .orig backup is left in place.)"
