# TASK: Build the passthrough — link, frame export, fakehost (slices 2–4) + full source for the gated parts

Follow-up to the recon (see `recon/MODDING_PLAN.md`, `env.md`, and the corrected
`README.md`). You are building everything in this project that does NOT require a
running TABG process (blocked by EAC — see recon §3). Write ALL source for the
gated parts too, so the Windows box can build immediately when the host-side
decision lands.

## Hard constraints

- **Never** install anything into `games/TotallyAccurateBattlegrounds/` and never
  launch it. Source for the BepInEx plugin is written but never installed/run here.
- Never modify `games/`. All work under `src/`, `tools/`, `tests/`, `docs/` in
  this project dir.
- This box is Linux, no GPU: C++/C# code for the Windows side must still be
  written completely and correctly, with build scripts for the Windows box (MSVC
  x86 for the Source plugin, .NET 8 for the BepInEx plugin). Use WSL-friendly
  bash wrappers where the reference does (`universal-modder/examples/.../gradle.sh`
  style), but here the Windows build happens ON the Windows box — provide
  `build.bat`/`build.sh` pairs like the reference.
- Reference implementation: `universal-modder/examples/minecraft-gta5-passthrough/`
  — study its `script.cpp` (link protocol), `compositor.cpp` + `MCPassthrough.fx`
  (composite), `mcframe.py` (frame reading), `fakegta.cpp` (fake host),
  `ws_test.cpp`, `host/` (Python tools), `video/` (director). Adapt, don't copy
  blindly: our guest is Source (32-bit, D3D9) not Minecraft, and our host is
  Unity (x64, D3D11, reversed-Z) not GTA.

## Deliverables

### A. Link layer + protocol (slice 2) — builds and RUNS here
- `src/link/` — the shared protocol, in one place both sides use:
  - `protocol.md` — the message schema: `cam` (host→guest: position, forward/right/up
    basis or quaternion, fov, aspect, frame index, timestamp µs), `ground`
    (host→guest: plane + origin for the chamber floor; we have a static chamber
    so this is one-shot + re-level events, not 160 probes), `key`/`slot`/`cmd`
    (host→guest input forwarding), `explosion`/`event` (guest→host event list,
    extensible), `hello`/`bye` (handshake with capability/version negotiation,
    like the reference keeps reconnecting).
  - `link.py` — a minimal RFC6455 WebSocket server on `127.0.0.1:25600`
    (pure Python, stdlib + `websockets` or a hand-rolled implementation if the
    package isn't available; check what's installed and prefer no-new-deps) that
    speaks the protocol both directions with ring-buffer semantics and
    back-pressure.
- `tools/pose_orbit.py` — drives `link.py` with a synthetic 60 Hz orbiting
  camera (like `host/fakehost.py`'s camera flight) and measures round-trip
  latency (send timestamped pose, guest echoes it, measure µs; report p50/p99).
- Acceptance: `link.py` + `pose_orbit.py` running simultaneously on this box at
  sustained 60 Hz with p99 round-trip < 16 ms, logged to `tests/link_results.json`.
  A `run_link_test.sh` reproduces it.

### B. Shared-memory frame channel (slice 3, host-agnostic half)
- `src/framechannel/` — the named shared-memory format (Windows named section
  `\\.\Global\<name>`; on Linux use a memfd/posix analog for the tests):
  - layout spec in `framechannel.md`: 3 rotating slots, each with header
    (frame_index, timestamp, width, height, pose snapshot of the pose it was
    rendered with — position + rotation + fov —, payload size), RGBA8 plane,
    depth plane (float32, in the RENDERER's depth convention with an explicit
    `depth_convention` flag: linear 0..1 vs reversed-Z vs GL-style), mutex/seqlock
    for the writer, reader-side validation (stale-slot detection).
  - `framechannel.py` — writer/reader implementations (the reader is the
    `mcframe.py` equivalent: dump latest slot to PNG + dump a depth heatmap PNG
    + verify the pose snapshot against expected).
  - `tests/framechannel_test.py` — writer thread produces synthetic gradient
    frames with known pose at 60 Hz; reader validates ordering, pose freshness
    (within 1 frame), and pixel correctness. Must pass here.
- The C++ side (for the Windows box): `src/framechannel/cpp/framechannel.h`
  — the identical layout with C/C++ APIs (writer: create/attach section, seqlock
  write; reader: attach, validate, copy-out), x86-ready (the Source plugin is
  32-bit), no C++ exceptions across the boundary, CRT-safe (static library, no
  global constructors).

### C. Guest: Portal source plugin (slice 3, Windows-side source)
- `src/portal_plugin/` — a Source **server plugin** (NOT a client dll — see recon
  `portal-notes.md`: `portal/addons/*.vdf`, hooks client interfaces; interface
  versions `ISERVERPLUGINCALLBACKS003`, `VEngineClient014`, `VClient017`,
  `VEngineRenderView014`, `VMaterialSystem080` per the 2013 SDK):
  - `passthrough.cpp/.h` — plugin: connects to `link.py` over WebSocket (x86
    static lib — vendor a small RFC6455 client like the reference's `ws.cpp`),
    receives `cam`, drives the local client camera (find the correct 2013-SDK
    path for controlling the SP camera from a server plugin — use the camera
    entity if it exists in this Portal build; fall back to
    `VEngineClient014::ClientCmd`-driven `sv`/viewstate; document the chosen
    route in `camera.md` with evidence from the SDK headers and what the
    reference did for the MC side), renders with the received pose,
    skybox off, then exports world colour + depth to the frame channel.
  - Depth export route: prefer the render-view path the recon found
    (`IViewRender` re-entry / depth-to-colour material + D3D9 readback);
    implement BOTH behind a `depth_method` config (recon unknown #4), with the
    D3D9 INTZ capture as the fallback.
  - `addons/passthrough.vdf` — the addon file; `build.bat` (MSVC x86) + `build.sh`
    (for WSL building if the Source SDK is available there) + `install.sh`
    (copies into `games/Portal/portal/addons/`, refuses to overwrite, `--remove`).
  - Since this box can't build against the real SDK, also provide
    `tests/fake_portal/` — a stub `client.cpp` that implements the same
    interface surface with a D3D9 (or, on Linux, an SDL2/GL) test window
    rendering a synthetic scene, proving the plugin's camera-drive + export
    code paths end to end without the real game.
- Acceptance (on the Windows box, checklist in `docs/windows-checklist.md`):
  Portal runs with the addon, `pose_orbit.py` drives it, `framechannel.py`
  reader dumps matching PNGs, latency reported. On THIS box: the plugin compiles
  against stub headers you extract from the SDK (vendor only what you need into
  `src/portal_plugin/sdk_stubs/` — do NOT vendor the whole SDK), and the
  fake_portal harness passes its own test.

### D. Host: TABG BepInEx plugin (slice 5 — SOURCE ONLY, never install)
- `src/tabg_plugin/` — C# (net472) BepInEx 5.4.23.5 plugin source:
  - Camera/feet sender: reads the local player's camera pose + feet position each
    frame (find the actual types in the decompiled code — the recon kept a
    decompile at `/tmp/tabg_decomp/`, it may be gone; re-decompile from
    `games/TotallyAccurateBattlegrounds/TotallyAccurateBattlegrounds_Data/Managed/Assembly-CSharp.dll`
    with the user-local ilspycmd if needed, `export
    DOTNET_ROOT=$HOME/.dotnet PATH=$HOME/.dotnet:$HOME/.dotnet/tools:$PATH`)
    and sends `cam` over the WebSocket.
  - Chamber setup: on F8 (re-level) spawns the invisible chamber-collider set on
    layer 29 (`OnlyLocalPlayer`) at the plane it receives: floor plane + wall
    colliders sized to the Portal chamber (dimensions: measure from the Portal
    game files if possible, else a config file `chamber.json` you fill with the
    standard test chamber 21×16×~11 units; document the assumption).
  - Composite pass: PPv2 custom effect (or CommandBuffer at
    `BeforeImageEffects` — pick per recon §6) that reads the latest frame-channel
    slot, depth-tests against `_CameraDepthTexture` (reversed-Z, linearise with
    `_ZBufferParams`), re-projects the guest frame from the pose it was rendered
    with to the current camera (the reference's latency-hiding trick), draws the
    overlay pass on top. Shader in `src/tabg_plugin/Shaders/PortalPass.cs.hlsl`.
  - Character hide: while the local player's position is inside the chamber AABB,
    hide the character's renderers (per recon: the character-lock + input
    forwarding is the GATED part — implement it behind a
    `enable_char_lock` config defaulting false, documented as the
    server-visible piece).
  - Input forwarding (mouse/wheel/1-9 → `key`/`slot` messages) + suppression of
    the game's own reactions while "inside" — implement, default off, same
    gate.
  - `build.bat` + `build.ps1` for the Windows box (BepInEx.Templates NuGet
    references copied to `src/tabg_plugin/libs/` — do NOT copy from `games/`;
    instead reference the BepInEx assemblies by path with a note, per recon §7:
    references come from a local ignored lib folder on the Windows box).
  - `install.ps1` — the one that's gated: it checks a `TABG_AC_CLEARANCE.txt`
    file in the project root (written by a human after the owner decision) and
    refuses to run without it. This is the enforcement of the recon §3 gate.
- Acceptance: `dotnet build` compiles on this box against the reference
  assemblies you vendor as stubs where needed (Unity assemblies you can extract
  metadata-only from `Managed/` — that's reading, allowed; note it in
  `docs/`. If building here proves impossible, at minimum: `csc`-level syntax
  verification of every file + a build that succeeds on the Windows checklist).

### E. Director + host tools (slice 8, host-agnostic half)
- `tools/director.py` — shot-list driver over `link.py` (teleport, look, time,
  weather ops; guest-side ops for Portal: portal pair placement, turret fire,
  cube launch, cake state) + `tests/director_test.py` against the fake guest.
- `tools/cut.py` — ffmpeg wrapper: take → 1080p cut with title card (keep it
  simple: title PNG overlay, no font-sheet archaeology for now).

### F. Docs
- `docs/windows-checklist.md` — everything the Windows box needs (from recon §7)
  plus the exact build/install/run sequence for slices 2–5, and the
  `TABG_AC_CLEARANCE.txt` gate explanation.
- Update `README.md` slices 2–4 with ✅ when their acceptance criteria pass.

## Order of work
1. protocol.md + link.py + pose_orbit.py + link test (A)
2. framechannel spec + python + test (B)
3. portal plugin source + fake harness (C)
4. tabg plugin source (D)
5. director + cut (E)
6. docs (F)

Report per-slice status in your final message. If a design decision forces a
tradeoff (e.g. camera-drive route on the Source side), pick the evidence-based
one, document the choice and the rejected alternative in the relevant `*.md`.

## G. EAC + auth bypass (NEW — do this FIRST, it unblocks everything)

The host is now a **local dedicated server** (`games/TotallyAccurateBattlegroundsDedicatedServer/`),
LAN-only, no official matchmaking, no relay. Goal: run the whole thing with **no Steam, no
Epic, no PlayFab, no EAC** — made-up local player names. All of this is in files we control
(client install + dedicated server install). Evidence below is from the decompiled code
(`../tabg_decomp` = client acs, `/tmp/tabg_server_decomp` = server, both available; re-decompile
with the user-local ilspycmd if missing: `export DOTNET_ROOT=$HOME/.dotnet PATH=$HOME/.dotnet:$HOME/.dotnet/tools:$PATH`).

### G.1 Server-side patch (ONE method body) — build a patcher that runs HERE
File: `games/TotallyAccurateBattlegroundsDedicatedServer/TABG_Data/Managed/Assembly-CSharp.dll`
Method: `Landfall.Network.RoomInitRequestCommand.<ctor or the join-handler that contains these lines>` —
it currently does, on player join:
1. `if (!gameRoomReference.CurrentGameSettings.UseAntiCheat) tABGPlayerServer3.AntiCheatAuthorized();`
   (with `AntiCheat=false` in game_settings.txt this ALREADY runs — good, keep it)
2. **unconditionally** `Easy_AC_Server.s_PlatformInterface.GetConnectInterface().VerifyIdToken(ref options, tABGPlayerServer3, OnVerifiesEpicToken);`
   — the only REAL verification in the chain (EOS JWT vs Epic backend; fail → `PlayerKickCommand ... EpicVerifyFail`).
3. `world.ValidateSteamTicket(tABGPlayerServer3);` — DEAD CODE (builds a buffer, never sends it;
   `AuthenticateTicketResponse` is empty). Its only live check: kicks if `PlayerName` is empty.

Patch (Mono.Cecil, .NET 8, runs on this Linux box against the Windows DLL):
- In the join handler, REMOVE the `VerifyIdToken` call (and its options setup).
- Add after the existing `AntiCheatAuthorized()` block: `tABGPlayerServer3.SetEpicUserID("local");`
  (mimics the OnVerifiesEpicToken success path minus the real verify; `SetEpicUserID` takes a
  string/Utf8String — check the signature) and, if `tABGPlayerServer3.Validated` is false after
  `AntiCheatAuthorized()`, call `AssignPlayerAccountIDsCommand.Run(m_TempWorld, tABGPlayerServer3, false)`
  (mimics the success path so other players' scoreboard gets an account slot; confirm by reading
  `AssignPlayerAccountIDsCommand` in the server decomp and what happens if it never runs — if it's
  only cosmetic for other clients, a no-op is also acceptable; document the choice).
- Deliverables: `tools/patch_server.py` (wraps a small C# patcher in `tools/patch_server/`,
  Mono.Cecil via dotnet; idempotent: detects already-patched and refuses to double-patch),
  `tools/patch_server/README.md`, `restore_server.bat` (restores the original DLL from backup),
  and a **verification step you run here**: after patching, decompile the patched DLL with
  ilspycmd to `/tmp/tabg_server_patched/` and grep the join handler to confirm the
  `VerifyIdToken` call is gone; keep the evidence in `tests/server_patch_evidence.md`.
- Do NOT modify the original DLL in place without first backing it up to
  `games/TotallyAccurateBattlegroundsDedicatedServer/TABG_Data/Managed/Assembly-CSharp.dll.orig`.
  The patcher itself must do: backup once → patch → write. Keep the `.orig` for restore.
- Note: `games/` is normally off-limits for modification, BUT the user explicitly owns these
  files and requested this (local-only use). The backup + restore story is the safety net.
  Do not commit the patched/orig DLLs to git (they're game binaries; .gitignore both).

### G.2 Client-side (goes INSIDE the BepInEx plugin from slice D — no DLL patching)
Add to `src/tabg_plugin/` (or a sibling `src/tabg_local/` if you prefer separation — document):
- HarmonyX no-ops:
  - `Landfall.Easy_AC_Client.SetACInterface(AntiCheatClientInterface)` → make it a no-op
    (the original quits the app if the interface is null — "Anti Cheat Boot Error").
  - `Landfall.Easy_AC_Client.BeginSession()` → no-op (original: `m_Interface.BeginSession`,
    on fail → `m_ServerHandler.Disconnect()`; also NPEs if `m_Interface` is null).
  - `EndSession` / `HandleACMessage` → no-ops (safety: called on disconnect / server AC messages;
    server won't send AC messages with AC off, but null-guard them anyway).
- Gate bypass in `Landfall.Network.ServerConnector.SendInitRequest` (client):
  - Gate 1: `if (string.IsNullOrEmpty(PhotonServerConnector.PlayFabID)) { Disconnect(); return; }`
    → set `PhotonServerConnector`'s private `PlayFabID` field to `"local-player"` via reflection
    BEFORE the check (HarmonyX prefix on `SendInitRequest`, or patch
    `Landfall.Network.PhotonServerConnector.SetPlayfabAccountInfo` usage — pick the cleanest,
    document).
  - Gate 2: `Result userToken = EOSSDKComponent.GetUserToken(out IDtoken);` → on non-Success the
    original bails to main menu (`MenuError.TokenError`). HarmonyX prefix on
    `EOSSDKComponent.GetUserToken`: set the out `IdToken` to a fake token (JsonWebToken bytes =
    `"local"` — content is irrelevant: the server's `VerifyIdToken` is patched out in G.1, and
    `EOSSDKComponent.LocalUser.ToString(out var outBuffer)` right after must also succeed or log
    an error — check `LocalUser`'s null path in the client decomp (`EOSSDKComponent.LocalUser`
    is an `Epic.OnlineServices.Connect.ProductUserId` field; if it's default/null the
    `ToString` call may fail: patch `EOSSDKComponent.LocalUser`'s backing field to a
    valid-looking ProductUserId via reflection if needed, or wrap the whole
    `SendInitRequest` body if the gates are too tangled — document what you chose and why).
- Make-up player name: find where `m_clientNetworker.GetPlayerName()` resolves (client decomp:
  `ServerConnector.PlayerName => Instance.m_clientNetworker.GetPlayerName()`, networker is
  `EnetClientNetworker`/`UnityTransportClientNetworker`); patch that method (or its backing
  field) to return a configured name (default `LocalPlayer`, configurable via a
  `local.json` next to the plugin). The server only requires NON-EMPTY (G.1 evidence).
- `SteamManager`/`PlayFabManager` hard-fail paths: `SteamAPI.Init()` will fail without a Steam
  client (`SteamManager.m_bInitialized=false` → `PhotonServerConnector` line ~433 skips the
  PlayFab login chain — GOOD, that's what we want). Audit for any OTHER place that hard-quit /
  NPEs when Steam is absent (search client decomp for `SteamAPI.` call sites without null/initialized
  guards + `AuthenticationService.Instance.PlayerId` null path — `SendInitRequest` already
  null-guards it) and document findings; patch only what actually breaks.
- **Do NOT ship G.2 as a separate installable if it can live in the slice-D plugin** — one
  plugin, one `TABG_AC_CLEARANCE.txt`-style gate renamed to `LOCAL_ONLY_GATE.txt`: the plugin
  checks a `local_only.json` with `{"i_understand_this_is_local_only": true}` and logs loudly
  on boot if EAC shims are active. (This replaces the old clearance-file concept: the whole
  thing is now local-only by design.)
- Update `install.ps1` accordingly (it no longer needs the EAC decision gate — it still must
  refuse to install if `games/TotallyAccurateBattlegrounds/TotallyAccurateBattlegrounds_Data`
  has an active EasyAntiCheat dir, so the user can't accidentally run a shimmied client against
  an official server: check for `EasyAntiCheat/` presence and warn loudly — the shim makes
  official-server play the thing that gets flagged; keep the install itself allowed but
  print a big warning).

### G.3 Launch + runbook (docs/windows-checklist.md, updated)
- Server: edit `game_settings.txt` → `Relay=false`, `Port=7777`, keep `AntiCheat=false`;
  `Relay=false` means clients join by direct IP (`127.0.0.1:7777`) via the
  `EnetServerConnectorUI` "join by address" path (confirm the UI field name from decomp).
  Start: `TABG.exe` (headless Unity server) — no launcher, no Steam.
- Client: run `TotallyAccurateBattlegrounds.exe` DIRECTLY (never `TABG_Launcher.exe` — the
  launcher is the EAC bootstrapper), with BepInEx present. No Steam client required.
- Smoke test order (Windows box): (1) server boots, log shows listening on 7777; (2) client
  boots to menu without Steam — record what happens at the login screen (menu may still try
  EOS/PlayFab login in the background — the join flow is what matters); (3) client joins
  `127.0.0.1:7777`, player name shows the make-up name, no kick; (4) a second client joins
  (two made-up names visible to each other — proves the server-side patch path works for
  multiple peers); (5) screenshot the lobby + the server console log (keep logs in
  `tests/` on the Windows box).
- If the menu hard-gates before you can reach the join-by-address screen, that's the one
  unknown: document the menu state machine you observe (decomp `MenuState` / `MenuError`
  handling) and patch the specific transition — smallest possible patch.

## Revised order of work
0. **G.1 server patcher** (runs + verifies HERE) → evidence file
1. A (link layer + test)
2. B (frame channel)
3. C (portal plugin source + fake harness)
4. D (tabg plugin source, INCLUDING G.2 shims)
5. E (director + cut)
6. F (docs incl. G.3 runbook)
