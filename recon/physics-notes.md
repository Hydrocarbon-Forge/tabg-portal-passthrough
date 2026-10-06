# Physics notes: how TABG moves characters

Recon slice 1, 2026-10-02. Static inspection only; the game was not run.

**Tools used:**
- .NET 8 SDK 8.0.425 installed user-local in `~/.dotnet` via `dotnet-install.sh`.
- `ilspycmd 9.1.0.7988` as a global dotnet tool. The latest ilspycmd failed to install on .NET 8;
  9.1 works.
- Full project decompile of `Assembly-CSharp.dll` + `Assembly-CSharp-firstpass.dll` (4,078 `.cs`
  files) into `/tmp/tabg_decomp/`, outside the repo and not committed.
- `UnityPy 1.25.3` for serialized scene/prefab/settings data. Scripts are in `/tmp/tabg_scan/`.

File references below are decompiled class files (`/tmp/tabg_decomp/acs/<file>`) or serialized
asset files under `games/TotallyAccurateBattlegrounds/TotallyAccurateBattlegrounds_Data/`.

## Answer first

**TABG does not use a custom physics solver.** "Landfall physics" is an active-ragdoll *controller*:
plain `UnityEngine.Rigidbody` bodies joined by `ConfigurableJoint`s, simulated by Unity's built-in
PhysX. Hand-written C# pushes them around with `Rigidbody.AddForce` every `FixedUpdate`. There are no
`TABSPhysics` or custom-solver types. A grep for `TABSPhysics`/`TABS` finds only a UI class
`Landfall.TABG.UI/TABS.cs` (tab buttons).

Consequences:

- **Invisible colliders *will* stop TABG characters.** They are ordinary PhysX contacts.
- **Invisible colliders *will* count as ground**, under the rules in §4.
- **Characters can be flung** by kinematic movers or by forces applied to the ragdoll (§5).
- There is a built-in collision layer, **29 `OnlyLocalPlayer`**, that collides only with the
  **local** player. That lets chamber geometry affect our character without touching anyone else's
  physics.
- **Caveat that matters for "client-only": TABG movement is client-authoritative.** Our character's
  position, as moved by our colliders, is what we send to the server. So collision effects on our
  own character *are* server-visible (§6).

## 1. Character structure (player prefab)

From `resources.assets`, GameObject `Player`, the only `RigidbodyHolder` root in that file. The same
layout is in `sharedassets2.assets` `PlayerRagdoll`/`Player_NPC` and the main-menu `MenuPlayer1`/
`DripPlayer` in `level0`.

| Body | Mass (kg) | Notes |
|---|---|---|
| Hip | 35 | root body, no joint; `Hip`, `CollisionChecker`, `MakeSureIDontGoThroughThings` |
| Torso | 25 | `ConfigurableJoint`, `CollisionChecker`, `MakeSureIDontGoThroughThings` |
| Head | 7 | `ConfigurableJoint`, `HeadCollisionHandler`, `MakeSureIDontGoThroughThings` |
| Arm_L/R | 4 each | `ConfigurableJoint`, `CollisionChecker` |
| Hand_L/R | 3 each | `ConfigurableJoint`, `PID`, `MeleeWeapon`, `CollisionChecker` |
| Leg_L/R | 5 each | `ConfigurableJoint`, `ChangeJointSettingsByAnimation`, `CollisionChecker` |
| Knee_L/R | 3 each | `ConfigurableJoint`, `CollisionChecker` |
| Foot_L/R | 3 each | `ConfigurableJoint`, `CollisionChecker` |

- 13 bodies, **103 kg total**.
- Every body: `drag 9`, `angularDrag 9`, `useGravity true`, `isKinematic false`,
  `interpolation Interpolate`, `collisionDetection Discrete`.
- Root components:
  - `AnimationHandler`, `MovementHandler`, `Standing`, `Balance`, `StepHandler`, `InputHandler`
  - `SetRigidbodySettings`, `MovementDataHandler`, `StandingDataHandler`, `RotationHandler`
  - `Gravity`, `Holding`, …

`SetRigidbodySettings.Awake` (`SetRigidbodySettings.cs`) does two things:
- It sets `maxDepenetrationVelocity = 10000` on every body. This matters: overlapping a collider
  ejects the body extremely fast (§5).
- For the locally controlled player (`hasControl`), it moves every body and collider to **layer 13
  `LocalPlayer`**. Remote players stay on layer 0 (`SetRigidbodyLayers`). `Player.Initialize`
  (`Player.cs:356`) also puts the `TabsUnit` model on layer 13.

## 2. Movement loop (all `FixedUpdate`, all forces)

| Class | What it does |
|---|---|
| `MovementHandler.FixedUpdate` | `movementVector += input * animationForceAmounts[state]; movementVector *= friction(0.9)`. Then for **every** body in `RigidbodyHolder.AllRigs`: `AddForce(movementVector * speed * clamp(drag/7,1,2) * strength * multiplier, ForceMode.Acceleration)`. Jump (`AddJumpForce` coroutine) zeroes vertical velocity, then adds `Vector3.up * jumpForce * jumpCurve(t)` per frame. |
| `Standing.ManagedFixedUpdate` | When `standingData.isGrounded`: lifts `rigsToLift` with `AddForce(up * force * curve(distanceToGround/offset))`. This is the "spring legs" that keep the ragdoll upright off the floor. |
| `Balance.ManagedFixedUpdate` | Computes the ragdoll centre of mass and pushes the knees with `AddForceAtPosition` toward it (`BalanceLegs`, `CenterLegs`). |
| `Gravity.FixedUpdate` | **Extra** custom gravity on top of PhysX gravity. Only while airborne (`sinceGrounded > 0`): `AddForce(down * clamp(drag/7,1,2) * sinceGrounded * scalingGravity * gravityMultiplier, Acceleration)`. Fall acceleration therefore *grows with airtime*. |
| `RotationHandler`, `StepHandler`, `AnimationHandler` | Torque/step targeting for the animation states. |
| `NetworkPlayer.ApplyRemoteUpdates` | **Remote** players are also full ragdolls, nudged toward the network position with `Rigidbody.MovePosition(pos + lerpDelta)`. They snap via `MovePosition` if more than 5 m off (50 m while skydiving) (`NetworkPlayer.cs:409-487`). |

Project physics settings (`globalgamemanagers` PhysicsManager/TimeManager):

| Setting | Value |
|---|---|
| `m_Gravity` | (0, **-20**, 0). Twice Earth, before the custom `Gravity` script. |
| Fixed timestep | **0.01666 s** (60 Hz); max allowed timestep 0.333 |
| Solver iterations | 6 position, 1 velocity |
| `m_DefaultContactOffset` | 0.01 |
| `m_BounceThreshold` | 2 |
| `m_DefaultMaxDepenetrationVelocity` | 10 (overridden to 10000 on the player) |
| `m_DefaultMaxAngularSpeed` | 7 |
| `m_AutoSimulation` | true |
| `m_AutoSyncTransforms` | true |
| `m_QueriesHitTriggers` | false |
| `m_QueriesHitBackfaces` | false |

So TABG uses stock PhysX stepping at 60 Hz.

## 3. Collision layers (the layer collision matrix matters)

Named layers (`TagManager`):

| # | Layer | # | Layer | # | Layer |
|---|---|---|---|---|---|
| 0 | Default | 9 | Map | 12 | Terrain |
| 11 | Props | 13 | **LocalPlayer** | 18 | DontCollideAndDontRender |
| 20 | DontRender | 21 | DontCollide | 22 | DontCollideWithLocalPlayer |
| 24 | DroppedRagdoll | 27 | IgnoreBullets | 29 | **OnlyLocalPlayer** |
| 30 | IgnoreProjectile | 31 | OnlyProjectile | | |

Rows decoded from `PhysicsManager.m_LayerCollisionMatrix`:

- **29 `OnlyLocalPlayer` collides with layers {3, 6, 7, 13}**: an unnamed layer, Post,
  SecondaryPost, LocalPlayer. It does **not** collide with Default (0, remote players), Map,
  Terrain, Props, or projectiles.
- 13 `LocalPlayer` has bit 29 set (the matrix is symmetric).
- 31 `OnlyProjectile` collides only with {3, 6, 7, 30}. 31 is also the layer `SetRigidbodySettings`
  uses for seated players.

**Implication for the chamber:** put the invisible floor plane and wall colliders on **layer 29**.
They then:
- stop and support only the local character,
- are invisible to remote players' local ragdolls,
- don't catch bullets or loot,
- don't intersect TABG world geometry.

Rendering: a collider with no `MeshRenderer` is invisible by definition. Layer 29 is not rendered
specially, so we just add no renderer.

## 4. Ground detection: what counts as "standing"

`CollisionChecker` (`CollisionChecker.cs`) is on the hip, torso, limbs and feet. On
`OnCollisionEnter`/`OnCollisionStay` it calls `Collide()`:

- It ignores self, early skydive (<0.3 s), and `player.neverLand`.
- If the other collider has a **Rigidbody with mass < 50 kg**, the contact is **not** ground. The
  PhysX contact still pushes; it just doesn't set grounded.
- Contacts with a normal within **60°** of up (`allowedSteepnesAngle`) call
  `StandingDataHandler.TouchGround(distance, normal, layer, collision)`.
  - That sets `isGrounded`, resets `sinceGrounded`, sets the slope, and refills jumps.
  - It triggers `Land()`. A landing after more than about 3 "gravity-seconds" calls
    `RagdollHandler.Fall`.
- Very steep contacts after more than 10 s airborne apply `+50 m/s` up, `VelocityChange`, and
  ragdoll. This is the game's "stuck on a wall" escape.

So a **static collider (no Rigidbody)** on layer 29 at chamber-floor height is real ground: Standing
lift, jumps, footstep sounds and all. A kinematic Rigidbody also works if its mass is ≥ 50. That is
useful for moving platforms or lifts.

## 5. Stop or fling?

| Mechanism | Works? | Evidence / notes |
|---|---|---|
| **Static invisible collider** (box/plane/mesh, layer 29) | **Stops.** Acts as wall/floor. | PhysX contact against the 13 bodies; ground per §4. |
| **Kinematic Rigidbody moved with `MovePosition`** | **Pushes, and flings if fast.** | PhysX treats it as infinite mass. Because the player's `maxDepenetrationVelocity = 10000`, any *overlap* resolves almost instantly. Sweep movers (don't teleport them into the character), or expect launches. |
| **Direct force on the ragdoll** | **Flings, deterministically.** | This is the game's own pattern: `JumpPlatform.OnCollisionEnter` calls `MovementHandler.Jump(forceJump:true)` and `PlayerKnockback.AddForce(up*force)` / `AddForceOverTime`. `ApplyForcesToAllRigids` loops `AddForce(..., Acceleration)` over child bodies. For portal exits with "velocity preserved", call `PlayerKnockback.AddForce` or set the velocity on every body in `RigidbodyHolder.AllRigs`. |
| **Dynamic prop < 50 kg** (e.g. the companion cube as a TABG prop) | Pushes and is pushed; **not standable** | §4 mass rule |
| **Teleport** | `Player.MovePlayer(newPos, posIsDelta)` exists | `Player.cs:303-316`. It shifts the root transform by (newPos - hip) and calls `ResetMe()` on every `MakeSureIDontGoThroughThings`. Use this, not raw transform writes (see the gotcha below). |

**Gotcha: the anti-tunnelling auto-kill.** `MakeSureIDontGoThroughThings` (on Hip, Torso, Head)
raycasts from last frame's position to this frame's against mask `0x1200` = layers {9 Map,
12 Terrain}. The mask was decoded from the serialized component. On a hit it:
- reflects the velocity,
- puts the body back at the hit point,
- after **15 consecutive frames** sends `WasDamaged(100000 * up)` in a network match. That is a
  self-kill the server sees.

Layer-29 colliders are not in that mask, so they never trigger it. But a portal warp that moves the
character *through Map/Terrain* without `MovePlayer` (which resets the tracker) can.

## 6. What is server-visible

- `NetworkPlayer.SendUpdate` does `m_Player.UpdatePosition(m_Hip.position)` and then
  `SendPlayerUpdate(m_Player.PlayerPosition, ...)` (`NetworkPlayer.cs:717-719`).
  `PhotonServerHandler.SendPlayerUpdate` (`PhotonServerHandler.cs:4333-4351`) serialises that as
  `EventCode.PlayerUpdate` (unreliable). So **the client's own hip position** goes out every send
  tick, and movement is client-authoritative.
- So anything that physically moves *our* character goes to the server and to every other player:
  - standing on an invisible floor where TABG has none,
  - being stopped by invisible walls,
  - being flung,
  - warping through a portal.

  To them it looks like floating, wall-blocking or teleporting. A server may also flag it. The
  dedicated-server code isn't in this client, so its movement validation is unknown.
- **Strictly invisible to the server:** rendering (the composite), colliders that only affect
  *other* objects in our local scene, and anything on remote players' local ragdolls. Remote
  ragdolls are dragged back to their network position each frame anyway (§2), so local colliders
  only make them jitter locally.
- This is a design-level conflict with "no server-visible state changes" in `TASK_RECON.md`. A
  purely visual passthrough has no server-visible effect. The interaction slices (6 collision,
  7 portal warp, turret damage/ragdoll) *do* change our networked state. See `MODDING_PLAN.md` §5.

## 7. Other physics facts worth keeping

- `Assembly-CSharp-firstpass.dll` contains `RootMotion.FinalIK` (IK on the ragdoll), RealtimeCSG,
  HighlightingSystem, ENet and ACTk. It has no physics solver.
- Vehicles are standard Rigidbody cars. `CollisionChecker` applies car-impact damage when
  `impulse*0.05 > 15`, the car speed is over 8 m/s and its mass is over 100 kg
  (`CollisionChecker.Collide`).
- Physics bodies are counted per scene (UnityPy scan, `/tmp/tabg_scan/scene_scan.json`). For
  example, `level8` (`Area_COMBINED`) has 13,257 CapsuleColliders, 43,229 BoxColliders and 18,146
  MeshColliders, with 64 Terrains and TerrainColliders. The world is ordinary static PhysX geometry.
- Unity's world unit is metres. The TABG character is roughly 2 m tall (hull and head heights are
  not measured here; check in UnityExplorer). Portal's player hull is 72 in ≈ 1.83 m
  (`portal-notes.md`).
