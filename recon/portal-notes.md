# Portal notes: guest-side recon

Recon slice 1, 2026-10-02. Static inspection of `games/Portal/`; nothing was run. Offsets are raw
file offsets from `strings -t x` unless marked VA (virtual address). PE data comes from Python
`pefile`. VPK directory trees were parsed with a small v2 parser in `/tmp`, which is not committed.

## 1. Exact layout and build

- `steam_appid.txt`: **`400`** (Portal).
- `portal/steam.inf`: `PatchVersion=1745010`, `ClientVersion=1745010`, `ServerVersion=9862575`,
  `ProductName=portal`, `appID=400`, `ServerAppID=0`.
- **Binary: `hl2.exe` (x86, PE32, 127,072 B).** There is no `Portal.exe`. The game dir is `portal/`,
  and code lives in `portal/bin/`:

  | File | Size |
  |---|---|
  | `client.dll` | 5,500,416 B |
  | `server.dll` | 7,959,552 B |
  | `gamepadui.dll` | 1,644,544 B |

- **This is the late-2024 Valve "singleplayer branch" rebuild (Source 2013 SP lineage), not the
  2007 Orange Box binary:**
  - build paths `c:\buildslave\rel_singleplayer_win32\...` in hl2.exe, launcher, engine, shaderapi
    and server;
  - engine.dll compile date `Nov 25 2024` @0x35c610;
  - present alongside: `bin/dxvk_d3d9.dll`, `bin/shaderapivk.dll`, `SDL2.dll` (2.0.16),
    `gamepadui.dll`, Hammer and the SDK tools, `VWorkshop001`, and `[CWorkshop] Mounted Addons:`
    @0x36b664.
- PE TimeDateStamps (UTC). Valve DLLs carry no VersionInfo StringTable.

  | File | TimeDateStamp |
  |---|---|
  | hl2.exe | 2024-11-13 |
  | engine.dll | 2024-12-09 |
  | launcher.dll | 2024-11-26 |
  | server.dll | 2024-12-09 |
  | **client.dll** | **2025-05-27** (newest) |

- Engine version strings: `Source Engine %i (build %d)` @0x3388b8, `Build Label: %8d` @0x351c54,
  `Protocol version: %8d` @0x351ce8 (engine.dll). Runtime values print in the console (`version`).
- **Everything is 32-bit.** Every .dll/.exe/.asi/.flt is machine 0x14c, linker 12.0 (VS2013), and
  there is no x64 folder. `hl2.exe` is **Large-Address-Aware** (FileHeader Characteristics bit
  0x20), so it gets 4 GB of address space on 64-bit Windows.
- `portal/gameinfo.txt`: `game "Portal"`, **`type singleplayer_only`**, `hasportals 1`,
  `SteamAppId 400`. SearchPaths:
  - `game+mod portal/custom/*` comes first. The folder does not exist yet; it is the drop-in for
    loose content and VPKs.
  - then `portal_pak.vpk`, the hl2 VPKs and platform,
  - `mod+mod_write+default_write_path |gameinfo_path|.`,
  - **`gamebin portal/bin`**.
- Maps: 26 loose BSPs in `portal/maps` (VBSP v20): `testchmb_a_00`…`_15` (no `_12`), the
  `_advanced` variants, `escape_00`–`_02`, `background1`/`2`.
- Leftovers, harmless:
  - `portal/extract/` holds 322 MB of loose content dated 2024 and is not a search path;
  - `portal/glshaders.cfg`, `videoconfig_linux.cfg` (2026-08) and a `portal/save/` folder suggest
    it was once run natively on Linux; there are no Linux binaries now.
- **The `um scan` result "loaders: ASI loader" is a false positive.** `bin/mssmp3.asi` and
  `bin/mssvoice.asi` are Miles Sound System codecs. mssmp3.asi exports `_RIB_Main@20` and contains
  "MSS MPEG Layer 3 Audio Decoder" @0x1074c. Both date from 2012.
- **Anti-cheat: none.** `um scan`: `anti-cheat: none found`.

## 2. Client-extension mechanism for *this* build

**Key constraint:** Portal's game code (portal-specific client/server: portal rendering, portal
physics shadow clones, the portalgun) is **not** in the public Source SDK 2013. That SDK ships HL2 /
Episodic / HL2MP sources only. A rebuilt `client.dll` would therefore lose Portal itself.
**Don't replace `portal/bin/client.dll`.** Load *alongside* it.

Ranked options:

1. **Server plugin (recommended).** The engine supports it, and so does the singleplayer listen
   server:
   - `addons/*.vdf` @0x34370c, `plugin_load` @0x343ab0, `ISERVERPLUGINCALLBACKS003` @0x343614,
     `Loaded plugins:` @0x34375c, `Unable to load plugin "%s"` @0x3436c4 (engine.dll).
   - `bin/serverplugin_empty.dll` ships with the game as a template.
   - **Placement:** `portal/addons/<name>.vdf` → `"Plugin" { "file" "addons/<name>" }` loads
     `portal/addons/<name>.dll` at map load (or `plugin_load addons/<name>` from the console).
   - The plugin DLL gets the engine `CreateInterface` factories. From there it can query:
     - `VEngineClient014`/`013`
     - `VClient017` (from `client.dll`'s factory, via `Sys_LoadInterface`/`GetModuleHandle`)
     - `VEngineRenderView014`, `VMaterialSystem080`, `VModelInfoClient006`
     - `VClientEntityList003`, `VEngineCvar004`, `VDebugOverlay003`, `VPhysics031`

     The interface strings are in engine.dll and client.dll; the subagent's list was spot-checked:
     client.dll `VClient017` @0x3b6420, `VEngineClient014` @0x3b6468.
   - Precedent: SourcePauseTool (SPT), the HL2/Portal speedrun tool, is exactly this kind of
     plugin. It hooks client/engine functions found by signature scanning. That comes from general
     knowledge, not verified here; check SPT's current support for the 2024 SP build.
2. **`-game <moddir>` mod folder** with its own `gameinfo.txt` whose `gamebin` points at our own bin.
   - `-game` handling: launcher.dll `Check your -game parameter...`; engine.dll `-game` @0x34a9c0.
   - `GAMEBIN` @0x32215c + `client.dll` @0x322164 + `Could not get client.dll interface from
     library client` @0x322184 show the engine loads `client.dll` from GAMEBIN.
   - This only helps if we ship a *full* client.dll, which we can't for Portal (see above). Useful
     only for a proxy `client.dll` that forwards `CreateInterface` to the real one. That is more
     fragile than option 1.
   - No `sourcemods` string appears in launcher/engine/FileSystem_Stdio, so Steam's sourcemods
     folder route is unconfirmed for this build.
3. **Proxy DLL / D3D9 hook** (`d3d9.dll` proxy for `shaderapidx9.dll`, which imports `d3d9.dll`).
   This is the fallback for frame capture only.

**Single-player restrictions / signatures:**

- Loading unsigned modules only cuts off secure servers: `Loading unsigned module %s` @0x350af8 →
  `Access to secure servers is disabled.` @0x350b13; also `-insecure` @0x3446dc, `VAC secure mode is
  activated.` @0x344938.
- `Can't use cheat command %s in multiplayer...` @0x349f30: cheat cvars are only restricted in MP.
- `gameinfo` `type singleplayer_only` means we run a local listen server. Plugins and cheats are
  fine there. Launch with `-insecure` anyway, as belt and braces.
- No VScript in this build. There is no `vscript.dll`, and `script_execute`/`RunScript` don't appear
  in client/server/engine. All logic has to be native in the plugin.
- `README.md` says "Source disables [client extensions] online". More precisely: there is no
  "client extension" API at all. The supported entry point is the server plugin, and unsigned code
  only disables *secure online* play. Irrelevant for a single-player game.

**Interface ABI vs SDK headers:** `VClient017`, `VEngineClient014`, `VEngineRenderView014`,
`VMaterialSystem080`, `ISERVERPLUGINCALLBACKS003`, `VClientEntityList003` and `VModelInfoClient006`
have the same version strings as the Source SDK 2013 `sp/src/public` headers. Build against
**`ValveSoftware/source-sdk-2013`** public headers, x86, using the same toolset family as the game
(VS2013 / v120 linker 12.0, or any MSVC with the v120-compatible CRT caveats). Verify vtable layouts
on first load: the 2024 rebuild may have appended methods, so call only methods that exist in the
SDK header, and log any interface that comes back NULL.

## 3. Frame + depth export options

What the build has:

- **Render targets** (client.dll):
  - `_rt_FullFrameDepth` @0x3d0a7c and `_rt_ResolvedFullFrameDepth` @0x3db558 (also in engine.dll
    @0x33c1fc/@0x33c210)
  - `_rt_FullFrameFB`, `_rt_Portal1`/`_rt_Portal2`, `_rt_DepthDoubler`
- **Depth debug views:**
  - client.dll `r_depthoverlay` @0x3d9ce0 ("Replaces opaque objects with their grayscaled depth
    values. r_showz_power…"), using `debug/showz`
  - `stdshader_dbg.dll` `DebugDepth`, `mat_debugdepthmode`
- **Depth shaders:**
  - `stdshader_dx9.dll` has the `DepthWrite` shader, `depthtodestalpha_ps20b` and
    `WRITE_DEPTH_TO_DESTALPHA` / `ShouldWriteDepthToDestAlpha()`
  - VPK `hl2_misc` holds `shaders/fxc/depthwrite_*.vcs`, `depthtodestalpha_*.vcs`,
    `debugdrawdepth_*.vcs`, `showz_*.vcs`, `materials/engine/depthwrite.vmt`,
    `materials/debug/showz.vmt`
- **Depth formats:** MaterialSystem `NV_INTZ` and `mat_reversedepth` @0xaaadc. shaderapidx9 supports
  the INTZ/DF16/DF24 depth formats.
- **Threading:** `mat_queue_mode` (MaterialSystem @0xab868). client.dll notes `mat_queue_mode must be
  0 to dump debug rendertargets` @0x3db254, and `mat_dump_rts` exists.
- **Graphics backend:**
  - **Desktop default is D3D9** (`shaderapidx9.dll` imports system `d3d9.dll`).
  - `-vulkan` selects `shaderapivk.dll`, which is **DXVK** (imports `DXVK_D3D9.DLL`).
  - With env `SteamDeck` set, the default is Vulkan unless you pass `-dx9`.
  - Selection logic was disassembled at MaterialSystem VA 0x10042489–0x100424fa; the log line is
    `Using shader api: %s`.
  - **Pin `-dx9`** so the capture path is deterministic.
- **View classes:** `IViewRender`, `CViewSetup`, `CViewRender::RenderView` RTTI and strings are in
  client.dll.

Recommended export path, in order of preference:

1. **Second depth pass into our own RT, rendered from the plugin.**
   - Create an `R32F` RT (or RGBA8 with packed depth) via `IMaterialSystem` (`VMaterialSystem080`).
   - Each frame after the main view, render the same `CViewSetup` with
     `IVRenderView`/`IViewRender::RenderView` while overriding materials with a
     linear-depth-to-colour material. Two ways to get that material:
     - stock `engine/depthwrite.vmt` / `DepthWrite` writes into the bound depth surface;
     - for linear eye-depth, a tiny custom pixel shader in a shader DLL (Source SDK 2013
       `materialsystem/stdshaders`, compiled with the SDK's `fxc` tooling), or a `debug/showz`-style
       material with `r_showz_power`-like scaling.
   - Then `CopyRenderTargetToTexture` → staging → readback.
   - Pros: engine-sanctioned and portal-aware. The portal stencil views come along for free, because
     we reuse the engine's own view.
   - Cons: costs one extra scene draw.
2. **Dest-alpha depth** (`WRITE_DEPTH_TO_DESTALPHA`): Source already writes scaled depth into the
   framebuffer alpha for DOF/fog. It is cheap but low precision (8-bit alpha on LDR) and range-
   limited. It is good enough only for a coarse occlusion test.
3. **D3D9-level grab.** Hook `IDirect3DDevice9::Present` (or `EndScene`) and read back the
   backbuffer with `GetRenderTargetData` into a `D3DPOOL_SYSTEMMEM` surface. For depth, use an INTZ
   depth texture (the build supports `NV_INTZ`). Run 3 rotating staging surfaces with `D3DQUERYTYPE_
   EVENT` fences. This is the async-readback pattern from the MC×GTA reference. Pros: independent of
   Source internals. Cons: depth via INTZ means replacing the engine's depth surface, which is
   invasive.

Use `mat_queue_mode 0` while developing, since queued mode moves rendering to another thread.
Memory per slot at 1920×1080 is RGBA8 8.3 MB + R32F 8.3 MB ≈ 16.6 MB, so three slots ≈ 50 MB. That
fits comfortably in the LAA 32-bit process.

## 4. Coordinates and units (confirmed from binaries)

- **1 Source unit = 1 inch = 2.54 cm**, per the engine's own physics conversion constants:
  - vphysics.dll float `0.0254` @VA 0x100cb00c and `39.37008` @VA 0x100cb010 (double 0.0254 @VA
    0x100ac5d8);
  - server.dll `39.37008` @VA 0x10527180.
  - **`README.md`'s "1 Source unit ≈ 1.17 cm" is not what the engine uses.** The familiar
    "1.905 cm" figure is a level-design rule of thumb (player-scale), not a constant either. For
    physically consistent mapping into TABG metres, use **0.0254 m/unit**. For "looks right next to
    a TABG character", scale by character height instead (Portal player 72 u ≈ 1.83 m) and record
    the factor in `env.md`.
- **Player hull (server.dll VA 0x10676e90, the same values in client.dll VA 0x104ae2b0):**
  - standing (-16,-16,0)–(16,16,72)
  - eye height 64
  - crouch hull height 36, crouched eye 28
  - dead eye 14
- **Movement cvar defaults (server.dll ConVar ctors):**

  | Cvar | Default |
  |---|---|
  | `sv_gravity` | **600** u/s² (= 15.24 m/s²; TABG uses 20 m/s² + extra airborne gravity) |
  | `sv_maxspeed` | 320 |
  | `sv_stepsize` | 18 |
  | `sv_accelerate` | 10 |
  | `sv_airaccelerate` | 10 |
  | `sv_friction` | 4 |
  | `sv_maxvelocity` | 3500 |
  | `cl_forwardspeed` / `cl_sidespeed` | 450 |

- Portal-specific cvars: `sv_portal_*` (placement/trace/collision sim bounds),
  `r_portal_stencil_depth`, `r_portal_use_stencils`, `r_portalsopenall`,
  `sv_player_trace_through_portals 1`.
- **Axes:** Source is right-handed **Z-up** (x forward, y left). Unity is left-handed **Y-up**
  (z forward, x right). Mapping: `unity = (−src.y, src.z, src.x) * scale` (x_u = −y_s, y_u = z_s,
  z_u = x_s). Angles: Source `QAngle(pitch, yaw, roll)`, where yaw is about +Z, counter-clockwise
  seen from above. Unity yaw is about +Y, clockwise seen from above, so `yaw_u = −yaw_s` (plus the
  heading offset). This is standard engine convention, not read from these files. Verify with the
  fakehost (slice 4) before trusting it.

## 5. Guest-side to-dos for later slices

- Confirm plugin load on this build: `plugin_load` + `plugin_print`, under `-insecure -dx9
  -windowed -noborder -w W -h H`.
- Hidden window: Source has no "hidden" flag. Use `-windowed -noborder`, move it off-screen or set
  `WS_EX_TOOLWINDOW` from the plugin, and keep it rendering with `engine_no_focus_sleep 0`
  (present: engine.dll @0x35ce5c).
- Sky off: `r_drawskybox 0` (present: engine.dll @0x33a528).
- Camera: drive `CViewSetup` origin/angles in a `RenderView` hook, or set the local player view via
  `IVEngineClient::SetViewAngles` plus noclip teleport (`setpos`/`setang` via
  `IVEngineClient::ClientCmd_Unrestricted`) for a first slice.
