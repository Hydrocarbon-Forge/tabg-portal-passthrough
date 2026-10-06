#!/usr/bin/env python3
"""patch_server.py — build + drive the local-only TABG dedicated-server patcher.

This is a thin wrapper around the Mono.Cecil patcher in ``tools/patch_server/``
(a .NET 8 console app). It builds the patcher once, then invokes it against the
dedicated server's ``Assembly-CSharp.dll``.

SCOPE: for the owner's own LAN / single-machine self-hosted TABG server, which
already runs with ``AntiCheat=false``. See ``tools/patch_server/README.md``.

Usage:
    python3 tools/patch_server.py [--check] [--dll PATH]

Defaults to the in-repo dedicated server DLL. On the Windows box, pass --dll or
run restore_server.bat to undo.

Requires .NET 8 SDK. On this Linux box:
    export DOTNET_ROOT=$HOME/.dotnet PATH=$HOME/.dotnet:$HOME/.dotnet/tools:$PATH
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
PATCHER_DIR = REPO / "tools" / "patch_server"
DEFAULT_DLL = (
    REPO
    / "games"
    / "TotallyAccurateBattlegroundsDedicatedServer"
    / "TABG_Data"
    / "Managed"
    / "Assembly-CSharp.dll"
)


def _dotnet() -> str:
    exe = shutil.which("dotnet")
    if exe:
        return exe
    guess = Path(os.environ.get("DOTNET_ROOT", str(Path.home() / ".dotnet"))) / "dotnet"
    if guess.exists():
        return str(guess)
    sys.exit(
        "ERROR: dotnet not found. Install the .NET 8 SDK and set "
        "DOTNET_ROOT/PATH (see module docstring)."
    )


def _build(dotnet: str) -> Path:
    out = PATCHER_DIR / "bin" / "Release" / "net8.0"
    dll = out / "PatchServer.dll"
    print(f"[patch_server] building patcher in {PATCHER_DIR} ...")
    subprocess.run(
        [dotnet, "build", "-c", "Release", "--nologo", "-v", "minimal"],
        cwd=PATCHER_DIR,
        check=True,
    )
    if not dll.exists():
        sys.exit(f"ERROR: build did not produce {dll}")
    return dll


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--check", action="store_true", help="report status; do not write")
    ap.add_argument("--dll", default=str(DEFAULT_DLL), help="server Assembly-CSharp.dll")
    args = ap.parse_args()

    dll_path = Path(args.dll)
    if not dll_path.exists():
        sys.exit(f"ERROR: target DLL not found: {dll_path}")

    dotnet = _dotnet()
    patcher = _build(dotnet)

    cmd = [dotnet, str(patcher)]
    if args.check:
        cmd.append("--check")
    cmd.append(str(dll_path))

    print(f"[patch_server] {'checking' if args.check else 'patching'}: {dll_path}")
    proc = subprocess.run(cmd)
    return proc.returncode


if __name__ == "__main__":
    raise SystemExit(main())
