# Link protocol (slice 2)

The link is a local WebSocket bus on `ws://127.0.0.1:25600` that both sides of the
passthrough speak. `link.py` is the **hub**: a standalone RFC6455 server that
relays messages between roles. The two roles are:

- **host** — TABG (the Unity BepInEx plugin). Sends `cam`, `ground`, `key`,
  `slot`, `cmd`, `director` ops downstream. Receives `event`, `explosion`,
  `echo`.
- **guest** — Portal (the Source server plugin). Receives host→guest messages,
  renders, and sends `event`/`explosion`/`echo` upstream.

A third role, **tool**, is used by `pose_orbit.py`, `director.py` and test
harnesses; it behaves like a host for routing (its messages go to guests) but is
tagged so real hosts and tools can coexist.

Everything is JSON text frames (small, human-debuggable; the bulk pixel data goes
through the shared-memory frame channel in slice 3, never the socket). All
coordinates are in the **sender's** engine convention; conversion happens at the
consumer (see `recon/portal-notes.md` §4 for the Source↔Unity mapping). Every
message is a JSON object with a `t` (type) field.

## Routing

- `hello` is handled by the hub (handshake), not relayed.
- host/tool → relayed to all **guest** peers.
- guest → relayed to all **host** and **tool** peers.
- The hub never reflects a message back to its own sender.

## Handshake: `hello` / `bye`

The first frame a client sends MUST be `hello`. The hub replies with its own
`hello` (capability/version negotiation, like the reference reconnect loop).

```jsonc
// client -> hub
{ "t": "hello", "role": "host|guest|tool", "ver": 1,
  "name": "tabg-plugin",            // free-form, for logs
  "caps": ["cam", "event", "depth"] // optional capability tags
}
// hub -> client
{ "t": "hello", "role": "hub", "ver": 1,
  "peers": { "host": 1, "guest": 0, "tool": 1 },  // current counts
  "caps": ["relay", "ring", "backpressure"] }
```

`bye` is optional and advisory; the hub also cleans up on socket close.

```jsonc
{ "t": "bye", "reason": "shutdown" }
```

## Host → guest

### `cam` — camera pose (60 Hz)
The host's current camera, so the guest can render that exact view.

```jsonc
{ "t": "cam",
  "f": 1234,                 // frame index (monotonic, host-assigned)
  "ts": 1696100000000000,    // host send time, microseconds since epoch (int)
  "p": [x, y, z],            // camera position, host units
  "basis": {                 // orientation as an orthonormal basis (preferred)
    "fwd":   [x, y, z],
    "right": [x, y, z],
    "up":    [x, y, z] },
  "q": [x, y, z, w],         // OR orientation as a quaternion (if "basis" absent)
  "fov": 60.0,               // vertical field of view, degrees
  "aspect": 1.7777,          // width / height
  "fp": true,                // first-person (vs third-person chase)
  "pl": [x, y, z],           // optional: player feet position (host units)
  "h": 0.0                   // optional: player body heading, degrees
}
```

A consumer should prefer `basis` when present (no quaternion-convention
ambiguity across engines) and fall back to `q`. `echo` (below) carries `f`/`ts`
back for latency measurement.

### `ground` — chamber floor/level (one-shot + re-level)
Our chamber is static, so this is sent once at setup and again on each F8
re-level event — not per-frame probing.

```jsonc
{ "t": "ground",
  "origin": [x, y, z],       // chamber origin in host units (player feet at re-level)
  "normal": [0, 1, 0],       // floor plane normal, host units
  "yaw": 0.0,                // chamber heading, degrees
  "size": [21, 16, 11]       // chamber extents (units); see chamber.json in slice D
}
```

### `key` / `slot` / `cmd` — input forwarding (host → guest)
Forwarded while the player is "inside the chamber" (gated, default off — see
slice D). `key` is mouse/keyboard/wheel; `slot` is hotbar 1-9; `cmd` is a raw
console command for the guest (e.g. Source `ClientCmd`).

```jsonc
{ "t": "key",  "ts": 1696100000000000,
  "code": "mouse", "dx": 3.0, "dy": -2.0, "buttons": 1, "wheel": 0 }
{ "t": "key",  "code": "key", "name": "e", "down": true }
{ "t": "slot", "n": 3 }                     // select hotbar slot 3 (0..8) / weapon
{ "t": "cmd",  "c": "r_drawskybox 0" }      // raw guest command
```

### `director` — shot-list op (slice 8)
Scenario ops (teleport, look, time, weather, and guest-side Portal ops: portal
pair placement, turret fire, cube launch, cake state). Extensible: unknown `op`
values are ignored by a guest that doesn't implement them.

```jsonc
{ "t": "director", "op": "portal_pair",
  "a": {"pos": [x,y,z], "normal": [x,y,z]},
  "b": {"pos": [x,y,z], "normal": [x,y,z]} }
{ "t": "director", "op": "turret_fire", "id": 2 }
{ "t": "director", "op": "cube_launch", "vel": [x,y,z] }
{ "t": "director", "op": "cake", "state": "revealed" }
{ "t": "director", "op": "teleport", "pos": [x,y,z], "yaw": 0.0 }
```

## Guest → host

### `event` / `explosion` — world events (extensible)
A generic, extensible event list going back to the host. `explosion` is the one
named concrete type the reference carries; anything else rides `event` with a
`kind`.

```jsonc
{ "t": "explosion", "pos": [x,y,z], "radius": 3.0, "power": 1.0 }
{ "t": "event", "kind": "portal_warp",
  "from": [x,y,z], "to": [x,y,z], "vel": [x,y,z] }
{ "t": "event", "kind": "turret_hit", "pos": [x,y,z], "dmg": 20.0 }
{ "t": "event", "kind": "cube_through_portal", "pos": [x,y,z], "vel": [x,y,z] }
```

### `echo` — latency echo
The guest echoes back the `f` and `ts` of a `cam` it received, as soon as it has
processed it. The host measures round-trip as `now_us - ts`. This is how
`pose_orbit.py` reports p50/p99.

```jsonc
{ "t": "echo", "f": 1234, "ts": 1696100000000000 }
```

## Ring-buffer + back-pressure semantics

Each connected peer has a bounded **outgoing** queue (default 240 messages ≈ 4 s
at 60 Hz). The hub is realtime: for a slow consumer the newest message matters
most, so when a peer's queue is full the hub **drops the oldest** queued message
for that peer (ring-buffer) and increments a per-peer `dropped` counter, rather
than blocking the whole bus on one slow reader. Handshake (`hello`) and `bye`
are never dropped. The hub logs the drop counter on disconnect.

Rationale: `cam` and `echo` are latest-wins; a stale pose is useless. This mirrors
the reference's "keep reconnecting / newest frame" philosophy and keeps one stuck
reader from stalling the 60 Hz loop.

## Versioning

`ver` is an integer. The hub accepts any client `ver` and reports its own; a
client may refuse to proceed on mismatch. `caps` lets either side light up
optional features (e.g. `depth`, `input`) without a protocol bump. Unknown `t`
values are relayed verbatim (forward-compatible) and ignored by consumers that
don't handle them.
