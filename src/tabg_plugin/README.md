# TABG Portal Passthrough Plugin (Slice D + G.2)

BepInEx 5 plugin for Totally Accurate Battlegrounds (host side).

## Overview
- **Link Client:** Connects to `ws://127.0.0.1:25600` and streams camera pose & player feet at 60 Hz.
- **Chamber Setup:** On F8, spawns invisible chamber colliders on layer 29 (`OnlyLocalPlayer`) at the local player's feet position.
- **Composite Pass:** Shared-memory frame channel reader (`TABGPortalFrame`) with CommandBuffer blit pass on the main camera.
- **G.2 EAC/Auth Shims:** Behind `LocalGate` (requiring `LOCAL_ONLY_GATE.txt` and `local_only.json`). Shims `Easy_AC_Client`, fakes EOS token and PlayFab ID for local LAN dedicated server joins.

## Building
Requires .NET SDK 8:
```bash
export DOTNET_ROOT=$HOME/.dotnet
export PATH=$HOME/.dotnet:$HOME/.dotnet/tools:$PATH
dotnet build -c Release src/tabg_plugin
```
BepInEx packages are cached locally in `packages/` to allow hermetic building without external TLS dependencies.
On Windows:
```cmd
build.bat
```
or
```powershell
.\build.ps1
```
