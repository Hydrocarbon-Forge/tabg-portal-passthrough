# TASK: Recon for TABG × Portal passthrough (Slice 1)

You are doing the recon slice of the TABG × Portal passthrough project (see
`README.md`, `overview.md` in this directory for the full design). Games are at:

- `games/TotallyAccurateBattlegrounds/` — TABG (Landfall Games), Unity/Mono, 4.0G
- `games/Portal/` — Portal, classic Source engine (hl2.exe + vpks), 4.9G

**Constraint from the owner: mods will only be used to play on community
self-hosted servers — never official/matched play.** This shapes the anti-cheat
analysis: we need to know what the client-side EAC actually does, what it
protects, and whether a client-only visual passthrough (no server-visible state
changes) is viable on community servers.

## Deliverables (all under `recon/` in this project dir)

1. **`recon/MODDING_PLAN.md`** — the master plan:
   - Engine/version fingerprint of both games (exact Unity runtime version from
     `TotallyAccurateBattlegrounds_Data/Managed/UnityEngine.dll` metadata or
     `UnityPlayer.dll` strings; Portal: engine build markers)
   - Ranked modding routes for the TABG side (BepInEx on Unity/Mono vs other
     loaders; note which BepInEx version matches the Unity runtime —
     check `UnityEngine.dll` for the version number)
   - Anti-cheat verdict (from eac-analysis.md) and what "community
     self-hosted server only" means for each route
   - Networking model: how does TABG join servers? (Unity.Services.Multiplay /
     Relay / Matchmaker are in the assembly list — determine whether servers are
     dedicated processes or host-authoritative peer, where the server list /
     server address comes from, how a community server would be joined)
   - Physics answer (from physics-notes.md) — this is the biggest unknown:
     TABG uses custom physics, not Unity physics. Can invisible colliders/props
     stop or fling TABG characters?
   - Windows build prerequisites checklist (what must exist on the Windows box:
     MSVC, Source SDK, Unity/BepInEx toolchain, ReShade-or-Unity-composite
     decision)
2. **`recon/eac-analysis.md`** — evidence-based:
   - Which binaries reference EasyAntiCheat (search `EasyAntiCheat`, `EAC`,
     `eac` in `games/TotallyAccurateBattlegrounds/` — start with
     `EasyAntiCheat/` dir, `TABG_Launcher.exe`, `TotallyAccurateBattlegrounds.exe`,
     and grep strings in `TotallyAccurateBattlegrounds_Data/Managed/*.dll`)
   - Is EAC loaded by the CLIENT, the LAUNCHER, or both? Does the client exe
     init it (look for `EAC_Init`-style imports / EasyAntiCheat.dll references)?
   - What does it likely gate (official matchmaking? all play?)
   - Verdict: on community self-hosted servers (no EAC enforcement), does a
     BepInEx-loaded client plugin risk anything? What about the portal-side
     (no AC at all on Portal)?
3. **`recon/physics-notes.md`** —
   - How TABG moves characters: decompile/inspect `Assembly-CSharp.dll`
     (no .NET toolchain yet — install mono or dotnet SDK + `ilspycmd`, or use
     `strings`/`monodis`-style inspection if installation is blocked; note what
     you used)
   - Find the physics entry points (search class/method names for TABS
     physics — Landfall's "Accurate" physics; check for `TABSPhysics`,
     `Rigidbody`, custom solver references, `Assembly-CSharp-firstpass.dll`)
   - Determine: character state (position/velocity) — is it driven by
     `UnityEngine.Rigidbody` or a custom transform system? This decides how
     invisible-collider interaction can work
4. **`recon/portal-notes.md`** —
   - Confirm exact Portal layout (classic Portal = old Source client; check
     `steam_appid.txt`, `hl2/` contents, presence of `portal` dir or
     `Portal.exe`/`hl2.exe` as the portal binary)
   - Source client extension mechanism for THIS build: where a custom client
     dll must sit, how it's loaded (Source's client extension loading), what
     single-player-only restrictions apply
   - Frame+depth export options on old Source (depth-to-colour material +
     readback; `k_RenderFrame` callbacks; whether this Portal build's client
     supports it — check for `client.dll`/engine dll strings)
   - Coordinate/units facts you can confirm from the game files (if any)

## Method notes

- `universal-modder/` in this dir is the framework clone. Try
  `pip install ./universal-modder` (or `uv tool install`) to get `um`, then
  `um scan games/TotallyAccurateBattlegrounds` and `um scan games/Portal` —
  if that fails, do the fingerprinting manually (same outputs: engine, version,
  loaders, anti-cheat, save folders, ranked routes).
- Install what you need (mono/dotnet, ilspycmd) — this is a Linux box (Python
  3.14, no .NET installed yet). Use the package manager; keep installs
  user-local.
- Do NOT modify anything under `games/`. Do NOT run the games (Windows binaries).
  Do NOT commit game files — only your recon outputs under `recon/` plus an
  updated `env.md` (fill in the fill-in table with real findings: paths,
  engine versions, EAC status, BepInEx target, Source SDK needs).
- This box cannot build the Windows targets; your Windows-prereqs checklist is
  the bridge to that. No C++/C# code in this slice — recon and plan only.
- Be evidence-first: every claim in the deliverables should cite the file
  (and offset/string where it matters) you saw it in.
