#!/bin/bash
# install.sh — install the Portal passthrough server plugin into a Portal copy.
# It only ADDS files and REFUSES to overwrite anything already there (so it can
# never clobber a real game file); `install.sh --remove` deletes exactly the
# files it would have added.
#
# NEVER run against games/ during development of this project — the plugin DLL
# does not even exist on this Linux box (build it on Windows with build.bat).
# This script is for the Windows/WSL box at run time.
#
#   PORTAL_DIR  the folder that contains hl2.exe (default: this repo's games/Portal)
#   BUILD       where build.bat put passthrough.dll (default: ./build)
#   FORCE=1     allow replacing files this installer owns
#
# Layout written:
#   <PORTAL_DIR>/portal/addons/passthrough.vdf
#   <PORTAL_DIR>/portal/addons/passthrough.dll
#   <PORTAL_DIR>/portal_passthrough.cfg      (read from the exe's working dir)
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
PORTAL_DIR="${PORTAL_DIR:-$REPO/games/Portal}"
BUILD="${BUILD:-$HERE/build}"

ADDONS="$PORTAL_DIR/portal/addons"
VDF_DST="$ADDONS/passthrough.vdf"
DLL_DST="$ADDONS/passthrough.dll"
CFG_DST="$PORTAL_DIR/portal_passthrough.cfg"

if [ ! -f "$PORTAL_DIR/hl2.exe" ]; then
  echo "hl2.exe not found in: $PORTAL_DIR (set PORTAL_DIR)"; exit 1
fi

if [ "$1" = "--remove" ]; then
  rm -fv "$VDF_DST" "$DLL_DST" "$CFG_DST"
  rmdir "$ADDONS" 2>/dev/null || true
  echo "removed passthrough plugin from $PORTAL_DIR"
  exit 0
fi

if [ ! -f "$BUILD/passthrough.dll" ]; then
  echo "build it first (Windows): src/portal_plugin/build.bat  -> $BUILD/passthrough.dll"
  exit 1
fi

# Refuse to overwrite anything we did not place, unless FORCE=1.
clash=
for dst in "$VDF_DST" "$DLL_DST" "$CFG_DST"; do
  [ -e "$dst" ] && clash+=" $dst"
done
if [ -n "$clash" ] && [ -z "$FORCE" ]; then
  echo "refusing to overwrite existing files (FORCE=1 to replace):$clash"
  exit 1
fi

mkdir -p "$ADDONS"
cp -v "$HERE/addons/passthrough.vdf" "$VDF_DST"
cp -v "$BUILD/passthrough.dll" "$DLL_DST"
cp -v "$HERE/addons/portal_passthrough.cfg" "$CFG_DST"
echo "installed passthrough plugin into $PORTAL_DIR"
echo "launch Portal with:  -insecure -dx9 -windowed -noborder -w 1920 -h 1080 +mat_queue_mode 0"
