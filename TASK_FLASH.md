# TASK: Finish slices C + D (flash)

The main build agent died on a credit wall. State of the project (read `TASK_BUILD.md`
for full specs; everything in `src/`, `tools/`, `tests/` already exists from the
previous run). What's left:

## 1. Slice D: get the TABG BepInEx plugin compiling HERE
- `src/tabg_plugin/TabgPortal.csproj` needs `BepInEx.Core` 5.4.21 +
  `BepInEx.PluginInfoProps` 2.1.0 from the BepInEx NuGet feed
  (`https://nuget.bepis.dev/v3/index.json`), which is **TLS-blocked from this
  Linux box** (curl gives `ssl tlsv1 alert internal error`), while nuget.org
  is reachable and HarmonyX 2.10.1 is on nuget.org.
- Fix: download the two BepInEx packages as .nupkg (GitHub releases at
  github.com/BepInEx/BepInEx publish them: look for BepInEx_v5.4.21*.zip or the
  nuget package files; if that fails, any working source is fine — the goal is
  getting the .nupkg bytes locally), place them in `src/tabg_plugin/packages/`
  (gitignored? NO — commit them, they're ~1MB and make the build hermetic on
  both boxes), add a local folder feed to `src/tabg_plugin/NuGet.config`
  (keep nuget.org + bepis feeds too, for the Windows box), then
  `dotnet build src/tabg_plugin/TabgPortal.csproj` (with
  `export DOTNET_ROOT=$HOME/.dotnet PATH=$HOME/.dotnet:$HOME/.dotnet/tools:$PATH`)
  must SUCCEED here against the real game assemblies (referenced by absolute
  path into `games/TotallyAccurateBattlegrounds/.../Managed/` — reading those
  files is allowed, never modify them).
- Fix whatever C# compile errors come out (the plugin source was written but
  never fully built). The source must stay functionally correct per TASK_BUILD
  slice D + G.2 (EAC shims in `src/tabg_plugin/Shims/EacShims.cs`, local gate
  in `Shims/LocalGate.cs`, camera sender, chamber builder, composite pass) —
  don't delete features to make it compile; fix them.
- If the game assemblies reference types that need stubs, prefer the real
  assemblies (they're all there in Managed/). Only stub what's genuinely
  missing.

## 2. Slice C: fake-Portal harness (was missing when the agent died)
- `src/portal_plugin/` has `passthrough.cpp/.h` (Source server-plugin logic:
  WS link, camera drive, frame+depth export to the framechannel), `ws.cpp/.h`
  (vendored WS client), `sdk_stubs/` (stubs for the Source interfaces, incl.
  `d3d9_stub.h`), `build_stubcheck.sh` (compiles ws.cpp + passthrough.cpp
  against the stubs — currently PASSES).
- Missing: the **fake_portal harness** per TASK_BUILD slice C: a test target
  that links the SAME `passthrough.o` logic against a synthetic scene renderer
  (D3D9 on Windows; on this Linux box a stub scene that produces a known
  gradient + known pose) and proves the camera-drive + export path end to end:
  run `tools/pose_orbit.py` (the orbiting fake host) → fake portal receives
  `cam` messages → renders synthetic scene at that pose → writes framechannel
  slots → `src/framechannel/framechannel.py` reader validates pose freshness +
  pixels. Write it as `src/portal_plugin/tests/fake_portal/` with
  `build.sh` + `run.sh`, and wire it into `run_all_tests.sh` (fix the harness
  so it doesn't SKIP `bash`-invoked steps — see `run_all_tests.sh`'s `run()`
  guard: it only auto-detects `python3`/`dotnet` prefixes; make the guard
  correct for all steps).

## 3. Final pass
- `bash run_all_tests.sh` → everything OK, zero FAIL, zero SKIP.
- Update `README.md` slice statuses (2–4 ✅ with the measured numbers; D =
  source-only build verified here).
- Update `tests/server_patch_evidence.md` ONLY if you changed the patcher
  (you shouldn't need to).
- Report per-item status at the end.

## Hard constraints
- Never write into `games/` (reading is fine).
- Stay in this project dir.
- No new global pip installs; `websockets` 17.1 is already installed.
- .NET: `export DOTNET_ROOT=$HOME/.dotnet PATH=$HOME/.dotnet:$HOME/.dotnet/tools:$PATH`
- If the BepInEx package download from GitHub also fails (network), fall back
  to: reference the BepInEx assemblies as stubs you write (minimal
  BepInExBase/PluginInfo surface) in `src/tabg_plugin/stub_refs/` and document
  that the Windows box uses the real packages — but TRY the real packages first.
