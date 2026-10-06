# Camera-drive route (Portal guest) — decision + evidence

The guest must point Portal's camera at the pose the host (`cam` message) sends,
every frame, from inside a **server plugin** (not a replaced `client.dll` — see
`recon/portal-notes.md` §2). Three candidate routes were considered; the chosen
one and the rejected ones are below, with the recon evidence each rests on.

## Chosen: route B — `ClientCmd_Unrestricted` setpos/setang + `SetViewAngles`

Implemented in `passthrough.cpp::DriveCamera`.

Per received `cam`:
1. Once per level: `r_drawskybox 0` (task requirement; string present in
   engine.dll @0x33a528, recon §5), `sv_cheats 1`, `noclip` (free the view so a
   teleport sticks without physics fighting it).
2. `setpos <x> <y> <z>` and `setang <pitch> <yaw> <roll>` via
   `IVEngineClient::ClientCmd_Unrestricted` — the pose converted host→Source by
   `recon/portal-notes.md` §4 (`src = (uz, -ux, uy)/0.0254`, `yaw=atan2(fy,fx)`,
   `pitch=-asin(fz)`).
3. `IVEngineClient::SetViewAngles(QAngle)` to lock the view angles the same frame
   (setang alone can be overwritten by client input the next tick).

### Why this one (evidence)

- **It uses only documented, confirmed-present interface methods.** Recon §2
  confirms `VEngineClient014` is exported by this exact 2024 build
  (`client.dll` @0x3b6468) and that a server plugin receives the engine
  `CreateInterface` factory. `IVEngineClient::ClientCmd*` / `SetViewAngles` are
  stable across the SDK-2013 lineage, so the vtable-drift risk recon §2 warns
  about ("the 2024 rebuild may have appended methods") does not bite us — we call
  only early, long-standing methods.
- **Recon §5 names exactly this path** as the first-slice camera route: "set the
  local player view via `IVEngineClient::SetViewAngles` plus noclip teleport
  (`setpos`/`setang` via `IVEngineClient::ClientCmd_Unrestricted`)".
- **`setpos`/`setang` are available**: cheat commands are only blocked in
  *multiplayer* (recon §2: `Can't use cheat command %s in multiplayer...`
  @0x349f30). Portal is `type singleplayer_only` running a local listen server,
  so cheats are allowed; we still launch `-insecure` as belt-and-braces.
- **No scripting dependency**: recon §2 confirms there is **no VScript** in this
  build (`script_execute`/`RunScript` absent), so any route that needs in-engine
  scripting is out.

### Known limitations (documented, not blocking)

- `setpos` sets the *player origin* (feet); the eye is `origin + viewheight`
  (64 u standing, recon §4). `DriveCamera` currently teleports origin to the host
  camera position; on the Windows box, subtract the eye height (or drive a
  noclip spectator whose origin *is* the eye) — a one-line offset, flagged in
  the README.
- There is a one-frame latency between `setang` and the engine actually
  rendering that view; the frame-channel stores the pose it rendered with so the
  host reprojects (the reference's latency-hiding trick, `framechannel.md`).

## Rejected: route A — camera entity (`point_viewcontrol` / `point_camera`)

Portal's `server.dll` has the usual `point_viewcontrol`/`trigger_camera`
entities, and a plugin *can* create entities through the game-server factory.
Rejected because:

- Activating such a camera as the client's view is an **entity-I/O** dance
  (`Enable`/`SetTarget`), and with **no VScript** (recon §2) there is no clean
  way to orchestrate it from a plugin without hooking game code. It also routes
  through the player's networked view, which is more state to fight than a direct
  `SetViewAngles`.
- It buys nothing over route B for a static single-player chamber, while adding a
  server-entity lifecycle we would have to manage across level transitions.

## Rejected: route C — `CViewSetup` / `IViewRender::RenderView` hook

Recon §3 lists re-entering the engine's own view (`IViewRender`, `CViewSetup`,
`CViewRender::RenderView` — RTTI present in `client.dll`) as the *best* path for
the **depth second-pass**, and one could also override origin/angles there.
Rejected as the *camera-drive* mechanism because:

- `IViewRender`/`CViewRender` live **inside `client.dll`** and are not exposed
  through a versioned engine interface a server plugin can query; driving them
  means **signature-scanning and hooking** client functions, exactly what
  SourcePauseTool does (recon §2 names SPT as precedent) — fragile, and recon §2
  explicitly flags that the 2024 rebuild's offsets are unverified.
- That fragility is acceptable for the *depth export* (we isolate it behind
  `depth_method` and it is the preferred route there, `renderpass`), but not for
  the always-on camera drive, where a stable interface call (route B) is clearly
  safer.

## Summary

| Route | Mechanism | Verdict | Key evidence |
|---|---|---|---|
| **B (chosen)** | `ClientCmd_Unrestricted` setpos/setang + `SetViewAngles` | **used** | recon §5 names it; §2 confirms `VEngineClient014`; §2 cheats allowed in SP |
| A | camera entity (`point_viewcontrol`) | rejected | needs entity I/O + no VScript (§2); routes through networked view |
| C | `CViewSetup`/`RenderView` hook | rejected for camera | client.dll-internal, needs signature hooks; §2 offset drift risk |
