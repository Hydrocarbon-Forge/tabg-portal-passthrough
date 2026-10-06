# MODDING_PLAN: TABG × Portal passthrough (recon slice 1)

Date: 2026-10-02. Box: Linux build/recon host (no GPU), static inspection only. Neither game was run,
and nothing under `games/` was modified.

Supporting notes, each with full evidence:
- [`eac-analysis.md`](eac-analysis.md)
- [`physics-notes.md`](physics-notes.md)
- [`portal-notes.md`](portal-notes.md)

## 0. Status in one paragraph

**Portal (guest) is green.** It is a 32-bit 2024 Source SP-branch build with no anti-cheat. It
supports server plugins, and depth RTs and shaders exist.

**TABG (host) is technically ideal but BLOCKED by anti-cheat.**
- On the technical side it is Unity 2021.3.45f2 Mono x64, so BepInEx 5.4.23.5 fits perfectly. It
  uses PhysX ragdolls, so invisible colliders work, and it uses the built-in render pipeline with
  camera depth already enabled.
- The blocker: the client starts an Easy Anti-Cheat (EOS) session on **every** server join and
  disconnects itself if that fails. "Community self-hosted servers only" still means an
  EAC-protected client process.
- Injecting BepInEx into it is the framework's anti-cheat stop sign. Running the client without
  EAC is bypass.

The design needs an owner decision (§3) before slice 2 work touches TABG. Slices 2–4 (link, guest
frame export, fakehost composite) can proceed now, because they need no TABG process.

> **Naming note:** `README.md`, `overview.md` and `env.md` call the host "Tag: The Game". The
> installed host is **Totally Accurate Battlegrounds** (Landfall Games, Steam appid **823130**,
> `ClientSteamLobby.cs:44`). Every finding here is for TABG.

## 1. Engine and version fingerprint

`um` was installed with `pip install --user ./universal-modder` (v0.2.0, `~/.local/bin/um`); `uv`
and `pipx` aren't on this box. The `um scan` results were verified by hand.

### TABG: `games/TotallyAccurateBattlegrounds/` (4.0 GB)

| Item | Value | Evidence |
|---|---|---|
| Engine | **Unity 2021.3.45f2 (88f88f591b2e)**, **Mono** (not IL2CPP) | `UnityPlayer.dll` VersionInfo `ProductVersion = 2021.3.45f2 (88f88f591b2e)`, `FileVersion 2021.3.45.8976527`; string `2021.3.45f2` @0x1783f60, `2021.3.45f2_88f88f591b2e` @0x1783f80; serialized-file headers `globalgamemanagers`/`level0` @0x30 = `2021.3.45f2`; `MonoBleedingEdge/EmbedRuntime/mono-2.0-bdwgc.dll`; `Managed/Assembly-CSharp.dll` present. `um scan`: `Unity (Mono) [100%] version=2021.3.45f2` |
| `UnityEngine.dll` version | Assembly version **0.0.0.0** (Unity module stubs carry no version). Metadata runtime `v2.0.50727`. | dnfile on `Managed/UnityEngine.dll` / `UnityEngine.CoreModule.dll`. The runtime version comes from UnityPlayer.dll and the data headers above. |
| Profile | `mscorlib 4.0.0.0`, `netstandard 2.1.0.0`; `Assembly-CSharp` metadata `v4.0.30319` | dnfile |
| Arch | `TotallyAccurateBattlegrounds.exe` PE32+ x64 (Unity stub, imports only `UnityPlayer.dll!UnityMain`); `UnityPlayer.dll` x64 | pefile |
| Build | exe/UnityPlayer TimeDateStamp **1758884505 / 1758884518 (2025-09-26)**; network compat key `ServerConnector.APP_VERSION = "693fa81cb14a5bc46bf18456d161a7c6"`; `PlayerSettings.bundleVersion "1.0"` (unused) | pefile; `Landfall.Network/ServerConnector.cs:16` |
| Render pipeline | **Built-in** (GraphicsSettings `m_CustomRenderPipeline` = null), linear colour space, PPv2 (`Unity.Postprocessing.Runtime.dll`; `PostProcessLayer` on cameras in every gameplay scene) | UnityPy scan of `globalgamemanagers`, `level2`–`8` |
| Depth texture | Already enabled on gameplay cameras by game scripts: `EnableCamDepthInForward.cs:9-11`, `AmplifyOcclusionBase.cs:493-499`, `DeepSky.Haze/DS_HazeView.cs:565-567`, PPv2 `EffectBase.cs:54-56` | decompile |
| Physics | Unity PhysX, gravity (0,-20,0), fixed step 1/60 | `physics-notes.md` §2 |
| Online stack | ENet (`Plugins/x86_64/enet.dll` + `ENet` in firstpass), Unity Transport + Relay, Unity Matchmaker/Multiplay/QoS, PlayFab, EOS SDK 1.15.4, Steamworks.NET 2025.162.1 (SDK 1.62) | Managed/ and Plugins/ listings; `Plugins/Steamworks.NET.txt` |
| Anti-cheat | **EasyAntiCheat (EOS)**: bootstrapper `TABG_Launcher.exe` + client-side session gate. ACTk bundled but no detectors used. | `eac-analysis.md` |
| Loaders installed | none | `um scan` |
| Saves/config | Not in the install. Unity `PlayerPrefs` go to the registry under `HKCU\Software\Landfall Games\Totally Accurate Battlegrounds`, and `Player.log` lives in `%USERPROFILE%\AppData\LocalLow\Landfall Games\Totally Accurate Battlegrounds\`. This is the Unity convention from `app.info` company/product; confirm on Windows. Gear selection uses ACTk `ObscuredPrefs` (`GearDatabase.cs:164`). | `TotallyAccurateBattlegrounds_Data/app.info` |

### Portal: `games/Portal/` (4.9 GB)

| Item | Value | Evidence |
|---|---|---|
| Engine | **Source 1, Valve 2024 "rel_singleplayer" rebuild** (Source 2013 SP lineage) | build paths `c:\buildslave\rel_singleplayer_win32\...`; engine.dll `Nov 25 2024` @0x35c610; `um scan`: `Source (1) [100%]` |
| Build | `portal/steam.inf` `PatchVersion=1745010` / `ClientVersion=1745010` / `ServerVersion=9862575`; `steam_appid.txt` = `400` | files |
| Binaries | `hl2.exe` (x86, LAA) → `bin/engine.dll` → `portal/bin/client.dll` (2025-05-27) + `server.dll` (2024-12-09). **All 32-bit.** | pefile |
| Graphics | D3D9 by default (`shaderapidx9.dll` → system `d3d9.dll`), DXVK via `-vulkan` (`shaderapivk.dll` → `dxvk_d3d9.dll`) | `portal-notes.md` §3 |
| Anti-cheat | none; `gameinfo.txt` `type singleplayer_only` | `um scan`; gameinfo |
| Loaders | none. **`um scan`'s "ASI loader" is a false positive** (Miles `mssmp3.asi`/`mssvoice.asi` codecs). | `portal-notes.md` §1 |
| Saves | `portal/save/` (present) | listing |

## 2. Modding routes for the TABG side (technical ranking, before the AC gate)

| # | Route | Fit for 2021.3.45f2 Mono x64 | Notes |
|---|---|---|---|
| 1 | **BepInEx 5.4.23.5, `BepInEx_win_x64_5.4.23.5.zip`** (latest stable, released 2026-02-08 per the GitHub API) + HarmonyX | **Exact fit.** BepInEx 5 targets Unity Mono. x64 matches the exe. | Doorstop `winhttp.dll` proxy + `doorstop_config.ini`. Plugins target `net472`/`netstandard2.0`, referencing `Managed/Assembly-CSharp.dll`, `UnityEngine.*.dll` and BepInEx core. Log: `BepInEx/LogOutput.log`. UnityExplorer for live inspection. Playbook: `universal-modder/skills/mod-any-game/references/engines/unity.md` "Route 2". |
| 2 | BepInEx 6.0.0-pre.2 `Unity.Mono` x64 | Works, but pre-release | Only worth it if a BE-only feature is needed. Not needed here. |
| 3 | MelonLoader 0.7.3 | Works on Unity Mono | No advantage over BepInEx for this project. The reference example and `unity.md` use BepInEx. |
| 4 | Native proxy DLL + hooks (MinHook) | Possible | Pointless for a managed game, and it hits the same AC wall. |
| — | Asset-only (AssetRipper/UABEA to *read* scenes/prefabs) | Read-only use is fine | Use it to measure geometry and layer setups offline. Don't ship extracted assets. |

**The AC gate applies to routes 1–4 identically.** Each one loads code into the protected client.

## 3. Anti-cheat verdict and what "community self-hosted only" means per route

Summary of `eac-analysis.md`:

- `TABG_Launcher.exe` is the EAC EOS bootstrapper (`start_protected_game.pdb` @0x30e754).
- On every server join the client calls `Easy_AC_Client.BeginSession()`
  (`PhotonServerHandler.cs:3716`, `Mode = ClientServer`) and **disconnects itself if it fails**
  (`Landfall/Easy_AC_Client.cs:76-82`).
- Without the bootstrapper, EOS uses `FAntiCheatClientNull`, which returns
  `EOS_AntiCheat_ClientProtectionNotAvailable`.
- So the only client that can play on a community server is the EAC-protected one.

| Route | On official play | On community/self-hosted servers |
|---|---|---|
| BepInEx/Harmony plugin in the bootstrapped client | Forbidden | **Still forbidden.** It is the same protected process. The community server not enforcing AC doesn't change what the client's EAC module sees, and the sanction risk is per account. |
| Exe without bootstrapper / EOS null client / patching `Easy_AC_Client` | Forbidden | **Forbidden: this is AC circumvention**, not an offline mode. It is not offered as an option. |
| Memory reading TABG from outside | Forbidden | Forbidden (ESP-class) |
| Out-of-process overlay, no injection | OK in principle | OK in principle, but it gets no depth and no camera |
| Portal-side plugin | n/a | **OK.** Portal is single-player with no AC. |

### Owner decision: options, ranked

- **A. Get a sanctioned EAC-free path from Landfall (recommended first step, no code).**
  - Ask Landfall, through the TABG Discord or support, whether there is or will be a client mode
    without EAC for community servers, and whether client mods are permitted there.
  - Also check the TABG dedicated-server package (not in this install) for an AC on/off setting.
    The client-side gate in `Easy_AC_Client` means a server setting alone isn't enough; Landfall
    would also need to ship a client path.
  - If they say yes, the plan below runs as written.
- **B. Keep the passthrough, swap the host to an unprotected Unity game.**
  - Totally Accurate Battle Simulator (TABS, Landfall) is the natural candidate: single-player,
    Unity, the same active-ragdoll family.
  - It is **not installed and not verified**. It needs its own recon slice: Unity version, Mono vs
    IL2CPP, any AC.
  - Everything in §5–§7 (BepInEx, PhysX colliders, depth composite) is expected to carry over and
    must be re-checked.
- **C. TABG + out-of-process overlay (demo-grade only).**
  - A separate topmost, click-through window draws the Portal frame over TABG.
  - No injection, no TABG memory access, no synthetic input into TABG.
  - Losses: no occlusion against TABG depth, no camera lock (the Portal camera can only mirror the
    user's raw mouse deltas read in our own process), and no collisions/events.
  - Some overlay styles are frowned on by AC vendors. Keep it to a plain layered window. Even so,
    this is a "picture-in-picture" stunt, not the passthrough in `README.md`.
- **D. Stop the TABG side** and finish the guest + fakehost slices (2–4) as a reusable Portal guest
  for a future host.

Until A or B is chosen, **no BepInEx install into `games/TotallyAccurateBattlegrounds` and no TABG
launches with mods.** This applies to the Windows box too.

## 4. Networking model (how TABG joins servers)

- **Dedicated, server-authoritative-for-game-state processes, not host-peer.**
  - The client only ever *connects*, through
    `ServerConnector.ConnectToServerIP(ip, port)` → `EnetClientNetworker.Connect`, or
    `ConnectToRelayServer(joinCode)` → `UnityTransportClientNetworker.ConnectViaRelay`
    (`Landfall.Network/ServerConnector.cs:151-183`).
  - The server answers `RoomInitRequestResponse`: accept/deny, game mode, player index, group
    index. The client then loads the scene (`HandlePlayerRoomInit`, `LoadGameScene`).
  - Server-side code shipped (unused) in the client confirms a separate server build:
    `ServerBrowserRegisterClient` registers a server with the browser register service on ENet
    port **24124** (`ServerBrowserRegisterClient.cs`, `AttemptRegister`). Nothing in the client
    instantiates it, and there is no `AntiCheatServer` caller.
  - "Photon" names (`PhotonServerHandler`, `PhotonServerConnector`) are legacy. The transport is
    ENet by default (`ServerConnector.Start`: `new EnetClientNetworker(this)`), or Unity Transport
    for Relay.
- **Where server addresses come from:**

  | Path | Mechanism | Evidence |
  |---|---|---|
  | Official matchmaking | Unity Matchmaker → `MultiplayAssignment.Ip/Port` → `ConnectToServerIP` (Unity Multiplay-hosted servers), or Landfall backend `Matchmake` → `"ip:port"` or a Relay join code | `MatchmakingHandler.cs:145-191`; `CommunityBackendAPI.cs` |
  | **Community server browser** | `POST https://tabgcommunitybackend.azurewebsites.net/GetServerList` with `{Version: APP_VERSION}` → `CommunityServerInfo[]` (`id, serverName, playersOnServer, maxPlayers, acceptingPlayers, serverDescription, squadMode, gamemode, passworded`). Then `POST /GetServerInfo {id, password}` → `"ip:port"` (direct ENet) or a Relay join code. | `Landfall.TABG/CommunityBackendAPI.cs:9-53`; `Landfall.TABG.UI/ServerBrowserUI.cs`; `ServerBrowserSelectedServerUI.cs:55-97` |
  | **Direct connect** | `EnetServerConnectorUI` (present in MainMenu `level0`): IP field (default `127.0.0.1`), `ip:port` with **default port 7777**, plus a Relay join-code field | `EnetServerConnectorUI.cs`; UnityPy scan of `level0` |
  | Squad / Steam lobby | Squad host shares `ip:port` or a join code via lobby data → `ForceConnectToIP` / `ConnectToRelayServer` | `Landfall.Network/ClientSteamLobby.cs:416-421` |

- **A community server is joined** via the browser (version-filtered by `APP_VERSION`, so client and
  server builds must match), by direct `ip:port` (LAN `127.0.0.1:7777` for a self-hosted server on
  the same box), or by Relay join code.
- Every path requires, in this order:
  1. Steam, then PlayFab `LoginWithSteam` (`PlayFabManager.cs:122-158`)
  2. EOS Connect login, then Unity Authentication `SignInWithSteamAsync`
  3. `SendInitRequest`, which sends name, Steam ticket, EOS ID-token JWT, EOS ProductUserId, PlayFab
     ID, gear, colour and the optional password (`ServerConnector.cs` `SendInitRequest`)
  4. **EAC `BeginSession`** after the room init
- **Authority:**
  - Player movement is **client-authoritative**: the client sends its hip position via
    `EventCode.PlayerUpdate` (`NetworkPlayer.cs:717-719`, `PhotonServerHandler.cs:4333`).
  - Damage, loot and pickups go through server-confirmed events (`WeaponPickUpAccepted`,
    `PlayerDamaged`, `LootPackGiven`… in `ServerConnector.OnEvent`).
  - So **any physical effect on our own character is visible to the server and other players**
    (§5).

## 5. Physics answer (the biggest unknown, now answered)

From `physics-notes.md`:

- **TABG has no custom solver.** Characters are 13 PhysX `Rigidbody` bodies (103 kg, drag 9) joined
  by `ConfigurableJoint`s and driven by `AddForce` controllers:
  - `MovementHandler`, `Standing`, `Balance`, and the extra airborne `Gravity`
  - `CollisionChecker` → `StandingDataHandler.TouchGround` for grounding
- **Invisible colliders stop TABG characters:** yes, as plain PhysX contacts.
- **They count as ground:** yes, if the contact normal is within 60° of up and the collider is
  static (or a Rigidbody ≥ 50 kg).
- **They fling characters:** yes. Kinematic movers push hard. The player sets
  `maxDepenetrationVelocity = 10000`, so overlaps eject violently; sweep movers instead of
  teleporting them. For deliberate launches, use the game's own `PlayerKnockback.AddForce` (as
  `JumpPlatform` does).
- **Use layer 29 `OnlyLocalPlayer`.** It collides only with layer 13 `LocalPlayer`, which
  `SetRigidbodySettings` puts the local character on. Chamber colliders on 29 never touch remote
  players, bullets, props or the map.
- **Teleport with `Player.MovePlayer(pos)`.** It resets the `MakeSureIDontGoThroughThings` tunnel
  tracker. That tracker raycasts against Map|Terrain (mask `0x1200`) and auto-kills after 15
  consecutive tunnelling frames, which is a server-visible self-damage.
- **Design conflict to resolve:** with client-authoritative movement, slices 6–7 (standing in the
  chamber, portal warps, flings) change our networked position. They are not "client-only visual".
  A pure visual composite (slice 5 minus the character-hiding lock) is server-invisible.

## 6. Composite pass on TABG (for when/if the host is unblocked)

- Built-in RP + PPv2. Gameplay cameras already generate `_CameraDepthTexture` (scripts in §1).
- Hook options:
  - a `CommandBuffer` at `CameraEvent.AfterForwardOpaque` / `BeforeImageEffects`, or
  - a PPv2 custom effect (`PostProcessEffectRenderer`) on the `PlayerCamera`.
- Unity on D3D11 uses **reversed-Z** (`UNITY_REVERSED_Z`). This matches the fakehost
  "reversed-Z, host conventions" note in `README.md`. Linearise with `_ZBufferParams`.
- Confirm the graphics API in `Player.log` (`Direct3D: Version: Direct3D 11.0 ...`). The build's
  API list isn't recoverable from the stripped PlayerSettings. D3D11 is the 2021.3 Windows default.

## 7. Windows build prerequisites checklist (the gaming PC)

**Guest (Portal, x86 native):**
- [ ] Visual Studio 2022 (or Build Tools) with **MSVC v143 x86** plus the **v120 (VS2013) toolset**
      if strict CRT parity with the game's linker 12.0 is wanted. v143 is fine for a plugin that
      shares no CRT objects across the boundary.
- [ ] **`ValveSoftware/source-sdk-2013`** clone: public headers for `ISERVERPLUGINCALLBACKS003`,
      `VEngineClient014`, `VClient017`, `VEngineRenderView014`, `VMaterialSystem080`. Use
      `sp/src/public`, `tier0`/`tier1`/`mathlib` libs, x86.
- [ ] Windows SDK 10 with the D3D9 headers (`d3d9.h`) for the readback path.
- [ ] x86 WebSocket library, e.g. IXWebSocket or a minimal RFC6455 server, built x86 static. Bind
      to `127.0.0.1:25600` only.
- [ ] Portal launch options: `-insecure -dx9 -windowed -noborder -w <TABG W> -h <TABG H>
      +mat_queue_mode 0 +engine_no_focus_sleep 0`.

**Host:** blocked by §3, but here is the list for when it opens:
- [ ] .NET SDK 8 (`dotnet build` for `net472` needs the .NET Framework 4.7.2 targeting pack or the
      `Microsoft.NETFramework.ReferenceAssemblies` NuGet), plus the `BepInEx.Templates` /
      `BepInEx.PluginInfoProps` NuGet.
- [ ] BepInEx **5.4.23.5 win_x64**, UnityExplorer (BepInEx 5 Mono build), HarmonyX (bundled).
- [ ] References copied from `TotallyAccurateBattlegrounds_Data/Managed/` into a *local, ignored*
      lib folder. Never commit them.
- [ ] Shader build for the composite: write the shader against **Unity 2021.3.45f2** (Unity Hub,
      editor of that exact version), and build an AssetBundle (Windows, D3D11) to load from the
      plugin.

**Composite decision:** **Unity-side composite (in-process PPv2/CommandBuffer), not ReShade.**
- ReShade injects into the protected process, so it has the same AC problem.
- In-process is the only way to get TABG depth anyway.
- Under option C (overlay) there is no composite with depth at all.

**Shared:**
- [ ] Python 3.10+ (director, `mcframe.py`-style reader), ffmpeg, `um` (`uv tool install
      git+https://github.com/rehan-remade/universal-modder`)
- [ ] Back up the TABG/Portal saves before any modded launch (`um backup`).

## 8. Corrections to existing docs (applied only in `env.md`; listed here for the other files)

| Doc claim | Finding |
|---|---|
| Host is "Tag: The Game" | Installed host is **Totally Accurate Battlegrounds** (Landfall, appid 823130) |
| `games/portal/`, `games/tabg/` | Actual dirs: `games/Portal/`, `games/TotallyAccurateBattlegrounds/` |
| "1 Source unit ≈ 1.17 cm" | Engine constant: **1 unit = 1 in = 2.54 cm** (vphysics 0.0254 / 39.37008). Pick an artistic scale explicitly if wanted. |
| "Source SDK client dll extension" | No client-extension API. Use a **server plugin** (`addons/*.vdf`) that hooks client interfaces. Rebuilding `client.dll` is impossible for Portal (its game code isn't in the SDK). |
| "TABG uses its own physics, not Unity physics" | It uses **Unity PhysX** with a custom ragdoll *controller* |
| "if any AC, strictly single-player" | TABG has no single-player mode; EAC gates all play. See §3. |
| BepInEx "no MSVC/ASI needed" | True technically, but moot until §3 is resolved |

## 9. Unknowns to resolve next

1. Landfall's answer on a sanctioned EAC-free client and mod policy for community servers (§3-A).
2. TABG dedicated-server package: AC config, movement validation (does it reject teleports or
   floating?).
3. Portal plugin load on the 2024 SP build (`plugin_load`), and the vtable sanity of
   `VEngineClient014`/`VClient017` against SDK 2013 headers.
4. Whether `IViewRender::RenderView` re-entry from a plugin (second depth pass) is stable with
   portal stencil views, versus D3D9-level INTZ capture.
5. If option B: recon of the replacement host (TABS or other).

## 10. Tooling used on this box (all user-local)

- `um` 0.2.0 via `pip install --user ./universal-modder`. `uv` is not installed;
  `pip install --user` worked on Python 3.14.7.
- .NET SDK 8.0.425 in `~/.dotnet` (`dotnet-install.sh --channel 8.0`) and
  `ilspycmd 9.1.0.7988` (`dotnet tool install -g ilspycmd --version 9.1.0.7988`; the latest
  ilspycmd needs a newer runtime).
  Use with: `export DOTNET_ROOT=$HOME/.dotnet PATH=$HOME/.dotnet:$HOME/.dotnet/tools:$PATH`.
- Python user packages: `pefile`, `dnfile`, `UnityPy 1.25.3`.
- Decompiled code: `/tmp/tabg_decomp/` (outside repo, volatile). Scan outputs: `/tmp/tabg_scan/`.
  Neither is committed (CONTRIBUTING: no decompiled code).
