# G.1 server-patch evidence

Verification of the local-only dedicated-server patch, run **on this Linux box**
(2026-10-02). Scope: the owner's own LAN / single-machine self-hosted TABG
server, which already runs with `AntiCheat=false` in `game_settings.txt`. See
[`../tools/patch_server/README.md`](../tools/patch_server/README.md).

## Target

`games/TotallyAccurateBattlegroundsDedicatedServer/TABG_Data/Managed/Assembly-CSharp.dll`

| file | sha256 | size |
|---|---|---|
| `Assembly-CSharp.dll.orig` (pristine backup) | `df80af939d29a5721e69a3ba12e12bbb2b0083e844fc31efbda3d125e51c7521` | 3,794,432 |
| `Assembly-CSharp.dll` (patched) | `0129ebc5e10b7d8cf17cd42da5f1b25b4ba9044124df5614bf10c902418357ea` | 3,780,608 |

Method patched: `Landfall.Network.RoomInitRequestCommand.Run(byte[], ServerClient, byte)`.

## How it was produced + verified

```bash
export DOTNET_ROOT=$HOME/.dotnet PATH=$HOME/.dotnet:$HOME/.dotnet/tools:$PATH
python3 tools/patch_server.py            # backs up .orig once, then patches
python3 tools/patch_server.py --check    # -> STATUS: PATCHED

# independent decompile of the PATCHED dll:
ilspycmd -t Landfall.Network.RoomInitRequestCommand \
  games/.../Managed/Assembly-CSharp.dll > /tmp/tabg_server_patched/RoomInitRequestCommand.cs
```

The patcher (`tools/patch_server/`, Mono.Cecil 0.11.6 on .NET 8) always reads
from the pristine `.orig` and writes the real DLL, so it can never double-patch;
`--check` reports status without writing; the patcher also fails closed if a
post-write re-scan still finds the `VerifyIdToken` call.

## Before (original `Run`, decompiled from `.orig`, line 200–227)

```csharp
if (!gameRoomReference.CurrentGameSettings.UseAntiCheat)
{
    tABGPlayerServer3.AntiCheatAuthorized();
}
VerifyIdTokenOptions options = default(VerifyIdTokenOptions);
IdToken value = new IdToken {
    JsonWebToken = jsonWebToken,
    ProductUserId = ProductUserId.FromString(productUserIdString)
};
options.IdToken = value;
m_TempWorld = world;
Easy_AC_Server.s_PlatformInterface.GetConnectInterface()
    .VerifyIdToken(ref options, tABGPlayerServer3, OnVerifiesEpicToken);   // <-- the real EOS JWT check
world.Server.AcceptPeer(playerID);
...
world.ValidateSteamTicket(tABGPlayerServer3);   // dead code; only live effect is kicking empty PlayerName
```

## After (patched `Run`, decompiled from the patched DLL, line 198–205)

```csharp
if (!gameRoomReference.CurrentGameSettings.UseAntiCheat)
{
    tABGPlayerServer3.AntiCheatAuthorized();       // still runs (AntiCheat=false)
}
VerifyIdTokenOptions verifyIdTokenOptions = default(VerifyIdTokenOptions);  // leftover unused local (cosmetic)
_ = ref verifyIdTokenOptions;
tABGPlayerServer3.SetEpicUserID(new Utf8String("local"));   // <-- inserted, unconditional
world.Server.AcceptPeer(playerID);
```

### Grep proof

```
# patched Run contains NO ".VerifyIdToken(" / "GetConnectInterface" call:
$ grep -n '\.VerifyIdToken(\|GetConnectInterface' /tmp/tabg_server_patched/RoomInitRequestCommand.cs
(no matches)

# the original still does:
$ ilspycmd -t ...RoomInitRequestCommand  Assembly-CSharp.dll.orig | grep VerifyIdToken
212:   Easy_AC_Server.s_PlatformInterface.GetConnectInterface().VerifyIdToken(ref options, ...);
```

The two residual `VerifyIdToken` substrings in the patched file are (1) the type
name `VerifyIdTokenOptions` on the dead local at line 202, and (2) the now-unused
`OnVerifiesEpicToken` method (line 260+), which nothing calls anymore. Neither is
an invocation of the Epic verification.

## Why these exact edits

- **Removed** `VerifyIdToken` + its `VerifyIdTokenOptions` setup. This is the only
  real verification in the join chain (EOS JWT vs the Epic backend; failure →
  `PlayerKickCommand(..., EpicVerifyFail)`). With no Steam/Epic the client's token
  is a placeholder, so the live call would kick every local player. Removing it
  lets the server accept the join locally.
- **Inserted** `SetEpicUserID("local")`, unconditionally, right after the existing
  `AntiCheatAuthorized()` block. Rationale: at match start,
  `GameRoom.ValidatePlayerObjects` kicks any player whose `EpicUserName == null`
  (`KickReason.ValidatePlayersNull`). Setting a non-null Epic id satisfies that
  gate. `"local"` is arbitrary and never verified.
- **Did NOT** call `AssignPlayerAccountIDsCommand.Run(...)` on the no-Steam path.
  `TABGPlayerServer.Validated` is `!IsNullOrEmpty(SteamID) && EpicUserName != null`,
  and `SteamID` is never assigned in this no-Steam flow, so `Validated` stays
  false. More importantly, `AssignPlayerAccountIDsCommand` ultimately calls
  `ulong.Parse(SteamID)`, which **throws** on an empty SteamID — calling it would
  crash the join. Its only effect is broadcasting steam/playfab/epic ids to other
  clients for the scoreboard (cosmetic). Skipping it is safe; the task explicitly
  allows a no-op here. The scoreboard will show the made-up name (sent separately
  via the `Login`/`RoomInitRequestResponse` payloads, which carry `PlayerName`).

## Restore

`restore_server.bat` (Windows) / `restore_server.sh` (here) copy `.orig` back over
the DLL. The `.orig` is retained as the safety net and is gitignored along with
the patched binary.
