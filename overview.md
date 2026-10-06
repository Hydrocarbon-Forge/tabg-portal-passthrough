# Overview: universal-modder, and what we're building here

## Part 1 — The Universal Modding Framework

`universal-modder` (rehan-remade/universal-modder) is a package that lets **any AI
coding agent mod almost any PC game you own**. It isn't a game modding engine —
it's the *know-how and tooling* an agent needs to do the job: skills it can read,
a CLI it can drive, and a shared memory of what other agents already learned.

It works with Claude Code, Codex, Cursor, Gemini CLI, GitHub Copilot, OpenCode,
or anything that reads `AGENTS.md`. One install, and the agent knows how to:
find the game, figure out its engine and version, pick a safe route, read the
real code, build the mod, make the art/3D/sound, test it in the running game,
record a video, and write down what it learned.

### The three pieces

**1. Skills** (`skills/`, Agent Skills format, pre-linked for every major agent).
Each skill is a document the agent reads when the task matches:

| skill | what it covers |
|---|---|
| `mod-any-game` | The whole loop, hard safety rules, and **12 engine playbooks**: Unity, Unreal, .NET/XNA (Terraria, Stardew, Celeste), Godot, Source 1/2, Bethesda, Minecraft, AoE2/Genie, RE Engine/FromSoft/GTA/Cyberpunk/BG3, native C++, indie engines, retro decomp |
| `game-recon` | Prior field notes, engine + version, managed vs native, anti-cheat, loaders, save folders → `MODDING_PLAN.md` |
| `reverse-engineering` | ILSpy / Cpp2IL / Vineflower / Ghidra / IDA / Cheat Engine / Frida / RenderDoc; reverse a file format and prove it with a round trip |
| `fal-assets` | Generate game assets via fal.ai: sprites with real transparency, pixel art, seamless textures, PBR maps, image→3D, auto-rigging, SFX, music, voice, video |
| `asset-pipeline` | Art → engine-exact frames: cutout, nearest-neighbour fit, palettes, sheets, 3D→sprite frames |
| `game-automation` | Launch, screenshot, safe input, windowed mode, crash cleanup, in-game agent bridges |
| `showcase-video` | Record the window with the game's own audio, cut a styled video from an EDL |
| `mashup-mods` | Game-inside-game: content ports, passthrough mods (worked example: Minecraft × GTA V), decomps as libraries |
| `publish-mod` | Lint, package per platform, credits, the post |
| `share-field-notes` | Search the knowledge base, write a note, open the PR |

**2. The `um` CLI** (Python, `uv tool install` / `pipx`). Every command has
`--help` with examples:

- `um scan` — find Steam/Epic/Xbox installs; fingerprint engine + version,
  .NET vs native, anti-cheat, installed loaders, save folders, ranked routes
- `um fal` — asset generation (sprite, image, edit, rmbg, pixelate, upscale,
  texture, pbr, model3d, rig, sfx, music, voice, video) with a manifest of every
  generation for traceability
- `um sprite` / `um render3d` — local art post-processing: cutout, fit,
  pixelate, palette, sheet, slice, frames; GLB→sprite frames from the game's
  camera via Blender
- `um win` — window capture (GPU-safe), record, input that only reaches the game
- `um video` — contact sheets, EDL→titled beat-cut video
- `um backup` — snapshot/diff/restore save folders
- `um kb` — search, create, validate, index, and PR field notes
- `um publish check` — blocks shipping game files, decompiled code, leaked keys

**3. The knowledge base** (`knowledge/`) — a knowledge base **AIs write for AIs**.
Each field note records how a specific game was actually modded: the exact
versions that worked, the route and why, how the engine really behaves, how it
was verified, and the gotchas (symptom → cause → fix). Every agent that finishes
a mod opens a PR with its note, so the next agent starts where the last one
stopped instead of rediscovering the same traps.

```
um kb search "grand theft auto"      # prior art before you start
um kb new --game "Hades II" ...      # draft your note
um kb pr knowledge/... --yes         # after a human says OK: branch, push, PR
```

### The loop

`mod-any-game` runs the same loop every time:

1. search the knowledge base
2. recon, then pick a route
3. set up a safe lab (saves backed up)
4. read the actual code
5. build one working slice
6. generate assets
7. verify in the real game
8. record
9. package
10. write a field note for the next agent

### Where fal fits

Fal (fal.ai) is a hosted inference platform: ~200+ open-weight generative
models (image, 3D, audio, video) on serverless GPUs behind one API key. In this
framework it's the **asset factory** — the agent doesn't juggle a dozen image/3D/
audio tools, it just calls one API (`um fal sprite`, `um fal model3d`,
`um fal sfx`) or uses fal's hosted MCP server. Everything lands on disk with a
manifest. Fal is **optional**: the entire modding loop (scan → recon → code →
mod → test → verify) runs locally; you only need a `FAL_KEY` if the agent should
*generate* new art, 3D, sound, or video.

### Safety model

Hard rules baked into the skills: story-mode-only when anti-cheat exists, no
automating input on landing pages while a human is at the keyboard, saves backed
up before mutation, processes killed by exact PID, link listeners on 127.0.0.1
only, and `um publish check` blocking game files / decompiled dumps / leaked keys
from ever shipping.

---

## Part 2 — What we're doing in this project

We're building the most interesting class of mod the framework supports: a
**passthrough** — two real game engines running side by side, one drawn inside
the other, with interaction crossing between them. Modeled directly on the
framework's own worked example, `examples/minecraft-gta5-passthrough` (real
Minecraft inside GTA V story mode).

### The pattern (from the example)

Both games run as separate processes. One is the **host** (visible window), one
the **guest** (hidden window, resized to the host's exact resolution):

- **Camera:** every frame the host's camera pose goes to the guest over a local
  WebSocket; the guest renders *that exact pose* (sky off) where the host's
  player stands
- **Frames:** the guest exports colour + depth to a shared-memory mapping (async
  GPU readback, 3 rotating slots); a compositing layer in the host's render
  pipeline depth-tests the guest frame against the host's depth buffer (real
  occlusion), re-projects it from the pose it was rendered with to the host's
  *current* camera (hides link latency), and optionally relights/grades it
- **Ground:** the host's terrain is probed and mirrored into the guest as
  invisible collision, so guest objects rest on the host's world
- **Events:** everything that happens in one game is translated into the other
  over the same link — explosions, projectiles, damage, movement
- **Input:** the host keeps keyboard focus; the plugin forwards the relevant
  inputs to the guest and suppresses the host's own reactions

### Our variant

**Host = Totally Accurate Battlegrounds (TABG)** (Unity, visible window, driven by a
BepInEx plugin), **guest = Portal** (Source, hidden window, driven by a custom
**server plugin** — `portal/addons/*.vdf`, per recon: this build has no
client-extension API).

Why this pair is interesting: the Portal *chamber* is a sealed, static,
recognizable space — you're not pasting a second world over an open one, you're
putting a **machine** (portals, turrets, the cube, the cake) inside a living
world, and the portals *actually work on TABG's terrain*.

Key differences from the MC×GTA example (this one is easier in some ways):

- **No ground probing.** The chamber is static — one invisible collision plane
  at chamber-floor height plus wall colliders, instead of 160 probes a frame.
- **Simpler host tech.** TABG is Unity with BepInEx — a managed C# plugin with a
  post-process composite pass. No ScriptHookV, no MSVC, no ASI loader, no ReShade.
- **Heavier guest.** Portal needs a Source **server plugin** built against the
  Source SDK 2013 headers (extension dlls are single-player — that's fine, we're
  single-player).

Interaction goals (see README slices):

- Portals painted onto TABG walls/ground warp TABG characters into the chamber
  (and back through the pair)
- While a character is "in the chamber", TABG's inputs drive the Portal player
  and the Portal avatar's position locks the TABS character — it's physically in
  Source's sim
- Turrets shoot TABG characters; damage and ragdolls cross over
- The Portal cube through a portal spawns as a TABG prop with velocity
- The cake at the end of the level: a TABG character walks in and eats it
- Audio captured per-process, mixed at record time (like the reference)

### Open questions (recon answers these before any code)

1. TABG engine/version via `um scan`; confirm BepInEx works on the current build
2. TABG anti-cheat status — if any, strictly single-player
3. **Biggest unknown:** TABG uses its own physics, not Unity physics — do
   invisible dynamic props/colliders actually stop or fling TABG characters?
4. Source **server plugin** (`portal/addons/*.vdf`) on the current Steam Portal
   build (recon-confirmed: no client-extension API)
5. Unity depth texture availability for the composite pass

### Hardware reality

Can't run on the basement box (no GPU, ~7GB RAM). Runs on the Windows gaming PC:
TABG windowed, Portal hidden window, GPU. This box hosts builds (WSL builds the
guest dll) and runs the link/fakehost test harnesses.

### Files

- `README.md` — full design: coordinate mapping, stack table, 8 build slices
  with acceptance criteria, open questions
- `env.md` — fill-in table: game install paths, working dir, WS port, pre-build
  checks
- `games/portal/`, `games/tabg/` — drop the games here (don't commit them to git)
- `universal-modder/` — the framework clone (skills, `um` CLI, knowledge base,
  examples)
