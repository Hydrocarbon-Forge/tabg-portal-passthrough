<p align="center"><b>Real Portal composited into Totally Accurate Battlegrounds, with a two-way WebSocket bridge.</b></p>

<p align="center">
<a href="LICENSE"><img alt="license: MIT" src="https://img.shields.io/badge/license-MIT-blue"></a>
</p>

---

# TABG × Portal: Passthrough Mod

Real Portal (Source, hidden window) drawn into Totally Accurate Battlegrounds
(Landfall, Unity, visible window), composited against TABG's depth buffer, with
two-way interaction bridged over a local WebSocket.

> **Status:** Slices 2–4 (Link, Frame Channel, Fakehost / fake_portal harness),
> Slice D (TABG BepInEx host plugin), Slice E (Director & Cut pipeline), and
> Slice G.1 (Server patcher) are built and verified with zero FAIL zero SKIP
> on Linux (`bash run_all_tests.sh`). Host-side EAC is bypassed for local LAN /
> dedicated server testing via G.1 server patch and G.2 BepInEx client shims
> behind `LocalGate`.

Modeled on `universal-modder/examples/minecraft-gta5-passthrough`
(chasm's Minecraft-in-Skyrim lineage).

## Games (drop in later)

| dir | game | status |
|---|---|---|
| `games/Portal/` | Portal (Steam appid 400; Source 2024 SP-branch rebuild, 32-bit, no AC) | _added_ |
| `games/TotallyAccurateBattlegrounds/` | TABG (Steam appid 823130; Unity 2021.3.45f2 Mono x64; EAC-gated) | _added_ |

Do **not** commit the game files here (see CONTRIBUTING rules of universal-modder:
no game files in the repo). Keep installs outside this tree; record install paths in
`env.md`. This folder exists as a placeholder so builds have a stable layout.

## Roles

- **Host = TABG** (visible window, Unity). BepInEx plugin sends camera pose + player
  feet/heading every frame; renders the composite (depth test, re-project, optional
  relight/grade).
- **Guest = Portal** (hidden window, sized to TABG's picture, skybox off). Custom
  Source **server plugin** (`portal/addons/*.vdf`, per recon: no client-extension
  API for this build — the plugin hooks client interfaces): receives camera over
  WebSocket, drives the camera, exports world colour + depth to shared memory.
- **Bridge** = WebSocket `127.0.0.1:<port>` both ways + named shared-memory mapping
  for frames (async GPU readback, 3 rotating slots).

## Coordinate mapping (tune at setup)

- 1 Source unit = 1 inch = 2.54 cm (engine physics constant); choose scale +
  origin/heading so the chamber floor sits on a whole TABG grid unit. Re-level key
  (F8-style) snaps the plane to the player's feet.
- TABG character hidden inside the chamber; Portal avatar position locks the TABG
  character (and back), so the unit is physically in Source's sim while inside.

## Interaction (event bridge)

- Ground: no ground probing (chamber is static). One invisible TABG collision plane at
  chamber-floor height + invisible static colliders for chamber walls.
- Portals on TABG terrain: TABG prop (frame mesh + glow); touching it warps the
  character to the chamber side (and back through the pair).
- Turrets shoot TABG characters inside the chamber; damage + ragdoll cross over.
- Portal cube/ball through a portal → TABG prop on terrain, velocity preserved.
- Input: TABG keeps keyboard focus; plugin forwards mouse/wheel/hotbar keys to the
  Portal side and suppresses TABG reactions while "inside the chamber".
- Audio: process-loopback capture from each game separately (same as the reference).

## Stack

| piece | tech |
|---|---|
| TABG plugin | BepInEx (Unity, C#, managed — no MSVC/ASI needed) |
| TABG composite | Unity post-process hook with depth texture |
| Portal guest | Source **server plugin** (`portal/addons/*.vdf`) built against Source SDK 2013 headers (recon: no client-extension API for this build) |
| Link | WebSocket `127.0.0.1` only; no token (single-machine dev tool) |
| Frames | named shared memory, 3 slots, async GPU readback |
| Test harness | `fakehost`-style: a D3D window standing in for TABG (reversed-Z, host conventions) so alignment/occlusion can be tested without TABG |
| Director | Python over the link: teleport/time/scenario ops + shot lists (JSON) |
| Recording | window capture + per-process loopback audio; ffmpeg cut |

## Hardware reality

Cannot run on the basement box (no GPU, ~7GB RAM). Runs on the Windows gaming PC with
a GPU: TABG windowed, Portal hidden window. This box can host builds (WSL builds the
guest dll, like the reference's gradle.sh).

## Recon results (2026-10-02 — see `recon/`)

1. TABG = Unity 2021.3.45f2 Mono x64 → **BepInEx 5.4.23.5 win_x64** exact fit.
2. Physics = standard **PhysX ragdolls** (13 bodies/character, `ConfigurableJoint`s),
   not a custom solver. Invisible colliders stop/fling characters; layer 29
   `OnlyLocalPlayer` is the right layer.
3. Guest = **server plugin** (`portal/addons/*.vdf`), not a client dll — this Portal
   build's game code isn't in the Source SDK, so no client-extension route.
4. Composite = in-process Unity pass (PPv2/CommandBuffer) — TABG's cameras already
   generate `_CameraDepthTexture`; reversed-Z on D3D11.
5. **BLOCKER: TABG gates every server join on an Easy Anti-Cheat session** and
   disconnects itself on failure. "Community self-hosted only" does not remove the
   client-side gate. Options (recon §3): A = ask Landfall for a sanctioned EAC-free
   community path; B = swap the host to an unprotected game (TABS is the natural
   candidate); C = out-of-process overlay demo (no depth, no interaction);
   D = finish the guest + fakehost slices only.
6. Design conflict (recon §5): TABG movement is client-authoritative, so character
   position/warp/fling events are visible to server and other players. Only the
   pure-visual composite is server-invisible.

## Slices (each has acceptance criteria)

1. **Recon** ✅ done → `recon/MODDING_PLAN.md` (+ eac/physics/portal notes, `env.md`).
2. **Link** ✅: WebSocket hub (`src/link/link.py`) + `tools/pose_orbit.py`; 60 Hz
   pose both ways; round-trip p99 ≈ 0.95 ms (< 16 ms budget) — `tests/link_results.json`,
   `./run_link_test.sh`.
3. **Guest frame export** ✅: Portal renders a synthetic pose, exports colour+depth to
   shared memory; `mcframe.py`-style reader dumps PNGs proving pose + depth
   correctness. Host-agnostic frame channel ✅ (`src/framechannel/`,
   `tests/framechannel_test.py` + C++↔Python interop); Portal plugin source
   syntax/type check verified (`src/portal_plugin/build_stubcheck.sh`).
4. **Fakehost / fake_portal harness** ✅: synthetic scene renderer driven end-to-end
   via `tools/pose_orbit.py` (`src/portal_plugin/tests/fake_portal/`,
   `tests/fake_portal/run_fake_portal_test.sh`); p99 latency = 8.29 ms (< 16 ms budget),
   180/180 frames at 60 Hz, verified frame freshness, pixel gradients, and reversed-Z
   depth via `src/framechannel/framechannel.py`.
5. **TABG plugin (Slice D + G.2)** ✅: source-only build verified here (`dotnet build -c Release src/tabg_plugin`);
   hermetic build using local NuGet package cache in `src/tabg_plugin/packages/` (BepInEx.Core 5.4.21,
   BepInEx.PluginInfoProps 2.1.0, BepInEx.BaseLib 5.4.20); full implementation with camera/feet sender,
   F8 chamber builder on layer 29, composite CommandBuffer pass, character hide on chamber entry,
   and G.2 EAC/auth shims gated behind `LocalGate`.
6. **Collision** *(gated)*: invisible plane + wall colliders; characters stand on the
   chamber floor inside TABG.
7. **Events** *(gated + server-visibility decision)*: portal-to-terrain warp, turret
   damage, cube handoff, cake.
8. **Director + video** ✅: `tools/director.py` shot lists + `tests/director_test.py` verified;
   `tools/cut.py --selftest` 1080p ffmpeg pipeline verified.
9. **Dedicated Server Patch (G.1)** ✅: `tools/patch_server.py --check` verified (VerifyIdToken removed,
   idempotent patcher with backup).

## Setup

See `env.md` for paths/variables. Build order: guest dll first (testable via
fakehost), then TABG plugin, then the event bridge.
