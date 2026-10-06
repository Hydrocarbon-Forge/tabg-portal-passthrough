# Windows checklist & runbook (slices 2–5 + G)

Everything the Windows gaming PC needs to build, install and run the passthrough,
plus the local-only launch sequence. The Linux basement box builds the managed
parts and runs the link/frame-channel/director tests (see the repo root tests);
the Windows box has the GPU and runs the games.

> **Local-only, owned-files scope.** The host is now a **local dedicated TABG
> server** the owner runs on their own machine/LAN (`game_settings.txt` has
> `AntiCheat=false`). The G.1 server patch and G.2 client shims exist only to run
> this creative passthrough locally — never against official/matched servers.
> The `install.ps1` guard and the `LOCAL_ONLY_GATE.txt` keep it that way.

## 0. Prerequisites to install on the Windows box

**Guest — Portal server plugin (x86 native), slice C:**
- [ ] Visual Studio 2022 Build Tools with **MSVC v143 x86** (v120/VS2013 toolset
      optional for strict CRT parity with the game's linker 12.0; v143 is fine as
      the plugin shares no CRT objects across the boundary).
- [ ] `ValveSoftware/source-sdk-2013` clone for the public headers
      (`ISERVERPLUGINCALLBACKS003`, `VEngineClient014`, `VClient017`,
      `VEngineRenderView014`, `VMaterialSystem080`), x86 `tier0`/`tier1`/`mathlib`.
      The repo only vendors minimal stubs in `src/portal_plugin/sdk_stubs/`; the
      real SDK is needed for the shipping build.
- [ ] Windows SDK 10 with D3D9 headers (`d3d9.h`) for the INTZ depth fallback.
- [ ] Build with `src/portal_plugin/build.bat`.

**Host — TABG BepInEx plugin (managed, net472), slice D:**
- [ ] .NET SDK 8 (`dotnet build`); net472 targeting pack or the
      `Microsoft.NETFramework.ReferenceAssemblies` NuGet.
- [ ] **BepInEx 5.4.23.5 win_x64** installed into the *client* (Doorstop
      `winhttp.dll` proxy). Unity 2021.3.45f2 Mono x64.
- [ ] HarmonyX (bundled with BepInEx 5), UnityExplorer (BepInEx 5 Mono build) for
      live inspection.
- [ ] Plugin references the Unity/BepInEx assemblies from a **local, ignored** lib
      folder on the Windows box — never commit them (see `src/tabg_plugin/README.md`).
- [ ] Shader: author `PortalPass` against Unity 2021.3.45f2, build a D3D11
      AssetBundle, load from the plugin (see `src/tabg_plugin/Shaders/`).
- [ ] Build with `src/tabg_plugin/build.ps1` / `build.bat`.

**Shared:**
- [ ] Python 3.10+ with `websockets`, `numpy`, `Pillow`; `ffmpeg` on PATH.
- [ ] .NET 8 SDK for the server patcher (`tools/patch_server.py`, slice G.1).

## 1. Build sequence

1. **Link + frame channel (slices 2–3, host-agnostic):** no build; pure Python
   (`src/link/`, `src/framechannel/`). The C++ frame-channel header
   (`src/framechannel/cpp/framechannel.h`) is consumed by the Portal plugin.
2. **Guest (slice C):** `cd src/portal_plugin && build.bat` → `passthrough.dll`.
3. **Host (slice D):** `pwsh src/tabg_plugin/build.ps1` → `TabgPortal.dll`.

## 2. Install sequence

- **Guest:** `src/portal_plugin/install.sh` (or copy manually) places
  `passthrough.dll` + `addons/passthrough.vdf` into `games/Portal/portal/addons/`.
  It refuses to overwrite; `--remove` uninstalls.
- **Host:** `src/tabg_plugin/install.ps1` copies the plugin into the client's
  `BepInEx/plugins/`. It **refuses/warns loudly** if the client still has an
  active `EasyAntiCheat/` directory (so a shimmed client can't be pointed at an
  official server). Requires `LOCAL_ONLY_GATE.txt` + `local_only.json`
  (`{"i_understand_this_is_local_only": true}`) to enable the EAC shims.

## 3. G.1 — patch the dedicated server (run on either box)

```bash
export DOTNET_ROOT=$HOME/.dotnet PATH=$HOME/.dotnet:$HOME/.dotnet/tools:$PATH   # Linux
python3 tools/patch_server.py --check      # STATUS: UNPATCHED | PATCHED
python3 tools/patch_server.py              # backs up .orig once, patches, verifies
# undo:
restore_server.bat        (Windows)   /   ./restore_server.sh   (Linux)
```

Backs up `TABG_Data/Managed/Assembly-CSharp.dll` → `.dll.orig` once, removes the
Epic `VerifyIdToken` join check, inserts `SetEpicUserID("local")`. Evidence:
`tests/server_patch_evidence.md`. Both DLLs are gitignored. See
`tools/patch_server/README.md`.

## 4. G.3 — local launch runbook

### Server
1. Edit `games/TotallyAccurateBattlegroundsDedicatedServer/game_settings.txt`:
   - `Relay=false` (clients join by direct IP), `Port=7777`, keep
     `AntiCheat=false`. `Relay=false` means clients connect to `127.0.0.1:7777`
     via the client's "join by address" path (`EnetServerConnectorUI`, default IP
     field `127.0.0.1`, default port `7777` — confirmed in client decomp,
     `recon/MODDING_PLAN.md` §4).
2. Apply the G.1 patch (§3).
3. Start `TABG.exe` (headless Unity server). No launcher, no Steam. The log shows
   it listening on 7777. (It also tries to fetch a public IP via
   `NetworkUtilities.GetPublicIPv4AddressAsync`; harmless if offline — it just
   logs the LAN bind.)

### Client
1. Install BepInEx + the slice-D plugin; create `LOCAL_ONLY_GATE.txt` and
   `local_only.json`.
2. Run `TotallyAccurateBattlegrounds.exe` **directly** — never
   `TABG_Launcher.exe` (the launcher is the EAC bootstrapper). No Steam client
   required (`SteamAPI.Init()` fails cleanly → the PlayFab login chain is skipped,
   which is what we want; see G.2 notes in `src/tabg_plugin/README.md`).
3. Start the link hub and (optionally) Portal + the guest plugin:
   ```
   python3 src/link/link.py           # the bus on 127.0.0.1:25600
   # Portal, hidden window, sized to TABG:
   hl2.exe -game portal -insecure -dx9 -windowed -noborder -w 1920 -h 1080 \
           +mat_queue_mode 0 +engine_no_focus_sleep 0
   ```

### Smoke test order (record logs/screenshots under `tests/` on the Windows box)
1. Server boots; log shows listening on 7777.
2. Client boots to the menu without Steam. Note what the login screen does (it may
   still attempt EOS/PlayFab in the background — the **join flow** is what matters).
3. Client joins `127.0.0.1:7777` via join-by-address; the made-up player name
   shows; no kick.
4. A second client joins — two made-up names visible to each other (proves the
   G.1 server path works for multiple peers).
5. Screenshot the lobby + server console log.

If the menu hard-gates before the join-by-address screen is reachable, document
the observed `MenuState`/`MenuError` transitions (client decomp) and patch the
specific transition with the smallest possible change — see
`src/tabg_plugin/README.md`.

## 5. Passthrough run (slices 2–5)

1. Link hub up (`src/link/link.py`).
2. TABG client in a local match on the patched server; BepInEx plugin connects as
   `host`, sends `cam`/`ground`.
3. Portal hidden window with the guest plugin connected as `guest`; it renders the
   received pose, exports colour+depth to the `TABGPortalFrame` shared mapping.
4. TABG composite pass reads the mapping, depth-tests against `_CameraDepthTexture`
   (reversed-Z), reprojects, draws the overlay.
5. Drive scenarios: `python3 tools/director.py run tools/shots/demo.json`.
6. Record and cut: `python3 tools/cut.py <take.mp4> out.mp4 --events tools/shots/demo.events.json`.

## 6. Acceptance tests that already pass on the Linux box

| slice | test | result |
|---|---|---|
| A (link) | `./run_link_test.sh 20` → `tests/link_results.json` | 60 Hz, p99 ≈ 1 ms < 16 ms |
| B (frame channel) | `python3 tests/framechannel_test.py` | ordering/pose/pixels/depth/stale ✓ |
| B (C++ interop) | `python3 tests/framechannel_cpp_interop.py` | C++ writer ↔ Python reader layout ✓ |
| C (fake guest) | `tests/fake_portal/run_fake_portal_test.sh` | see `src/portal_plugin/README.md` |
| D (plugin build) | `dotnet build src/tabg_plugin` | see `src/tabg_plugin/README.md` |
| E (director) | `python3 tests/director_test.py` | ops delivered + events round-trip ✓ |
| E (cut) | `python3 tools/cut.py --selftest` | 1080p titled cut ✓ |
| G.1 (server patch) | `python3 tools/patch_server.py --check` | PATCHED; `tests/server_patch_evidence.md` ✓ |
