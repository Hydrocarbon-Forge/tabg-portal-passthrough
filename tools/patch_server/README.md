# patch_server — local-only TABG dedicated-server patcher (G.1)

A Mono.Cecil patcher that edits **one method body** in the TABG **dedicated
server's** `Assembly-CSharp.dll` so a LAN / single-machine self-hosted server
accepts made-up local player names with **no Steam, no Epic, no PlayFab, no EAC**.

## Scope and intent (read this)

This is for the **owner's own local server**. The dedicated-server package ships
with `AntiCheat=false` in `game_settings.txt`; this patch removes the one
remaining piece that still phones Epic on join (an EOS JWT verification), so the
server can run fully offline/LAN. It is **not** for official/matched play, and
the companion client plugin (slice D / G.2) refuses to install against a client
that still has an active `EasyAntiCheat/` directory. The pristine DLL is backed
up to `*.orig` and `restore_server.bat` / `restore_server.sh` undo the change.

## What it changes

Method: `Landfall.Network.RoomInitRequestCommand.Run(byte[], ServerClient, byte)`.

1. **Removes** the `Easy_AC_Server.s_PlatformInterface.GetConnectInterface()
   .VerifyIdToken(ref options, player, OnVerifiesEpicToken)` call and its
   `VerifyIdTokenOptions` setup. That call is the only real verification in the
   join chain (EOS JWT checked against the Epic backend; on failure the player is
   kicked with `EpicVerifyFail`). With no Steam/Epic the token is a placeholder,
   so leaving the call in would kick every local join.
2. **Inserts** `player.SetEpicUserID(new Utf8String("local"))` immediately after
   the existing `AntiCheatAuthorized()` block, unconditionally. This mimics the
   success path of `OnVerifiesEpicToken` minus the real verify, and satisfies the
   real match-start gate: `GameRoom.ValidatePlayerObjects` kicks any player with a
   null `EpicUserName`.
3. **Deliberately does not** call `AssignPlayerAccountIDsCommand.Run(...)`. In the
   no-Steam flow `SteamID` is empty, `TABGPlayerServer.Validated` stays false, and
   that command does `ulong.Parse(SteamID)` which would **throw**. It only
   broadcasts account ids to other clients for the scoreboard (cosmetic); the
   made-up player name reaches other clients through the separate `Login` /
   `RoomInitRequestResponse` payloads. Full rationale +
   evidence: [`../../tests/server_patch_evidence.md`](../../tests/server_patch_evidence.md).

The dead `world.ValidateSteamTicket(...)` call below the edit is left untouched:
it builds a buffer and never sends it; its only live effect is kicking an empty
`PlayerName`, which the client-side make-up name (G.2) prevents.

## Usage

Requires the .NET 8 SDK. On this Linux box:

```bash
export DOTNET_ROOT=$HOME/.dotnet PATH=$HOME/.dotnet:$HOME/.dotnet/tools:$PATH

# patch the in-repo dedicated server (backs up .orig once, verifies, writes):
python3 tools/patch_server.py

# report status without writing:
python3 tools/patch_server.py --check

# a server installed elsewhere (e.g. on the Windows box):
python3 tools/patch_server.py --dll "D:\path\to\TABG_Data\Managed\Assembly-CSharp.dll"
```

Or drive the built patcher directly:

```bash
dotnet build -c Release tools/patch_server
dotnet tools/patch_server/bin/Release/net8.0/PatchServer.dll <dll>          # patch
dotnet tools/patch_server/bin/Release/net8.0/PatchServer.dll --check <dll>  # status
```

## Safety / idempotency

- **Backup once, patch from pristine.** On first run the patcher copies the DLL
  to `<dll>.orig`. Every run reads from `.orig` and writes the real DLL, so it
  can never double-patch (re-running just reproduces the same clean patch).
- **Fails closed.** After writing a temp file it re-scans it; if the
  `VerifyIdToken` call is somehow still present it deletes the temp and aborts
  without touching the live DLL.
- **Resolver.** Cecil resolves the Unity base types of `TABGPlayerServer` while
  writing, so the patcher adds the DLL's own `Managed/` directory to Cecil's
  assembly search path.
- **Exit codes:** `0` ok / patched(`--check`), `10` unpatched(`--check`),
  `2` usage, `3` method-not-found, `4` post-write verify failed.

## Restore

```
restore_server.bat      (Windows)     restore_server.sh   (Linux)
```

Both copy `<dll>.orig` back over the DLL and leave the backup in place.

## Do not commit the binaries

`Assembly-CSharp.dll`, its `.orig` backup, and any `*.patched.tmp` are game
binaries — gitignored (see the project-root `.gitignore`). Only this patcher's
source and the evidence file are tracked.
