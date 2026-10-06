# EAC analysis: Totally Accurate Battlegrounds (and Portal)

Recon slice 1, 2026-10-02. Everything here comes from static inspection of the files in
`games/TotallyAccurateBattlegrounds/` (no game was run). Paths are relative to that folder unless
noted. "Offset" means a raw file offset from `strings -t x`. Decompiled sources were produced with
`ilspycmd 9.1.0.7988` into `/tmp/tabg_decomp/`, which is outside this repo and is not committed.

## TL;DR verdict

- **The TABG client cannot join *any* server (official, community, or self-hosted) unless an Easy
  Anti-Cheat (EOS) session starts successfully.** The check runs in the client, unconditionally, on
  every join. It is not a server-side setting.
- That means "community self-hosted servers only" does **not** give us an EAC-free client. The
  client that joins a community server is the same EAC-protected process that joins official
  matchmaking.
- **A BepInEx plugin loaded into that process means code injection into a live EAC-protected
  process.** The framework's safety rules treat that as a stop sign
  (`universal-modder/skills/mod-any-game/references/safety.md` §"Kernel or user-mode anti-cheat is a
  stop sign"), and so does Epic's own enforcement. The risks are an immediate launch or kick error
  and an EOS sanction on the owner's account.
- Every way to make the client run *without* live EAC is anti-cheat circumvention, and this plan
  rules all of them out. That covers launching the exe directly, the EOS null-client switch, and
  Harmony-patching `Easy_AC_Client`.
- **Portal has no anti-cheat.** `type singleplayer_only` and VAC only cover secure multiplayer
  servers. The Portal guest side is clear.
- **Net: the TABG-host route is BLOCKED on the current build** until there is a sanctioned EAC-free
  path. See `MODDING_PLAN.md` §3 for the options.

## 1. Which binaries reference EAC

| File | What it is | Evidence |
|---|---|---|
| `TABG_Launcher.exe` (PE32 x86, 3,490,224 B) | **The EAC EOS bootstrapper, renamed.** It is Epic's `start_protected_game.exe` under another name. | VersionInfo: `ProductName = Easy Anti-Cheat Bootstrapper (EOS)`, `ProductVersion = 1.5.0`, `CompanyName = Epic Games, Inc.`; PDB string `start_protected_game.pdb` @0x30e754; `Settings.json` @0x30c5e0; `anticheatlauncher.log` @0x30c630; `-anticheat_settings=` @0x30c920. PE TimeDateStamp 1660292512 (2022-08-12). |
| `EasyAntiCheat/Settings.json` | Bootstrapper config | `"executable": "TotallyAccurateBattlegrounds.exe"`, plus EOS `productid`/`sandboxid`/`deploymentid` for TABG. `Settings.json.bak` is Epic's unmodified template (`"title": "Example Game"`), with the same mtime as everything else. It shipped that way and was not tampered with. |
| `EasyAntiCheat/EasyAntiCheat_EOS_Setup.exe` | EAC service installer | file present (935,344 B) |
| `EasyAntiCheat/Certificates/{base.bin,base.cer,runtime.conf}` | EAC certificate and runtime config (encrypted blob) | `base.cer` is an X.509 v3 cert; `runtime.conf` is high-entropy data |
| `TotallyAccurateBattlegrounds_Data/Plugins/x86_64/EOSSDK-Win64-Shipping.dll` | EOS SDK **1.15.4-23143668** (VersionInfo) | exports/strings `EOS_Platform_GetAntiCheatClientInterface` @0x1223552, `EOS_AntiCheatClient_*`, `EOS_AntiCheat_ClientProtectionNotAvailable` @0x1091d20, `Epic::OnlineServices::FAntiCheatClientNull::Init` @0x1069008, `EOS_USE_ANTICHEATCLIENTNULL` @0x10665a0 |
| `TotallyAccurateBattlegrounds_Data/Managed/Assembly-CSharp.dll` | Game code. It contains the EOS C# bindings (`Epic.OnlineServices.*`, including `AntiCheatClient`, `AntiCheatServer`, `AntiCheatCommon`, `Sanctions`, `Reports`) and Landfall's wrapper `Landfall.Easy_AC_Client` | 93 string hits; see §2 |
| `TotallyAccurateBattlegrounds_Data/Managed/Assembly-CSharp-firstpass.dll` | Bundles **CodeStage Anti-Cheat Toolkit (ACTk)** (`CodeStage.AntiCheat.Detectors`: InjectionDetector, SpeedHackDetector, TimeCheatingDetector, ObscuredCheatingDetector, WallHackDetector) | 8 hits; see §4 |
| `TotallyAccurateBattlegrounds.exe` (PE32+ x64, 653,824 B) | Stock Unity stub | Imports only `UnityPlayer.dll!UnityMain` and KERNEL32. **There is no EAC import.** EAC is not linked into the exe. |
| `UnityPlayer.dll`, `UnityEngine.CoreModule.dll`, `com.rlabrecque.steamworks.net.dll` | **False positives** | UnityPlayer @0x173e4c8 `R Compressed EAC 4 bit` (ETC2 "EAC" texture formats). Steamworks @0x547f2 `k_EDenyIncompatibleAnticheat` (a Steam enum). |

`um scan games/TotallyAccurateBattlegrounds` reports `anti-cheat: EasyAntiCheat`, which agrees with
the above.

## 2. Who loads EAC: launcher, client, or both?

**Both, as a pair. The launcher starts protection, and the client calls into it through the EOS
SDK.**

1. `TABG_Launcher.exe` is the EOS EAC bootstrapper. It reads `EasyAntiCheat/Settings.json`, ensures
   the EAC service is installed, and launches `TotallyAccurateBattlegrounds.exe` as a *protected
   game*. Its strings include `OnGameLauncherFinished@AntiCheatLauncher` with
   `EAC_ProtectedGameLaunchResult` and `StartModuleDownload@cGameLauncher`. The live EAC client
   module is downloaded and attached at this point.
2. The game process never imports EAC directly. It P/Invokes the EOS SDK
   (`Epic.OnlineServices/Config.cs`: `LibraryName = "EOSSDK-Win64-Shipping"`), and the SDK's
   AntiCheatClient interface talks to the protection the bootstrapper set up. If the game was not
   started protected, the SDK falls back to `FAntiCheatClientNull`, and `BeginSession` fails with
   `EOS_AntiCheat_ClientProtectionNotAvailable` (both strings are in the EOS DLL at the offsets
   above).

The Landfall code path, from the decompile:

- `PlayFabManager.OnSteamLoginSuccess` calls `EOSSDKComponent.InitEpicSDK(steamTicket)`
  (`PlayFabManager.cs:158`).
- `InitEpicSDK` creates the EOS platform, logs in via Connect with
  `ExternalCredentialType.SteamSessionTicket`, then calls
  `Easy_AC_Client.SetACInterface(GetACInterface())` (`EOSSDKComponent.cs:180`).
- `Easy_AC_Client.SetACInterface`: if the interface is null, it shows `"Anti Cheat Boot Error..."`
  and calls **`Application.Quit()`** (`Landfall/Easy_AC_Client.cs:34-48`).
- On **every** server join, `PhotonServerHandler` (after the login packet,
  `Landfall.Network/PhotonServerHandler.cs:3716`) calls `Easy_AC_Client.BeginSession()`. It sets
  `Mode = AntiCheatClientMode.ClientServer` and
  `LocalUserId = EOSSDKComponent.LocalUser`. If the result is not `Success`, it shows
  `"Anti Cheat Fail: " + result` and **disconnects** (`m_ServerHandler.Disconnect()`,
  `Easy_AC_Client.cs:69-84`). This call has no game-mode, server-type, or flag condition around it.
- AC traffic is tunnelled through TABG's own netcode. The client sends via
  `OnMessageToServerCallback` → `PhotonServerHandler.ClientSendACDataChannelRequest` →
  `EventCode.ACRequestedData`, and the server replies with `EventCode.ACMessage` →
  `Easy_AC_Client.HandleACMessage` → `ReceiveMessageFromServer`
  (`ServerConnector.cs` OnEvent, `PhotonServerHandler.cs:769-777`).
- `EndSession` is called on `PhotonServerHandler.OnDestroy` (`:4942`).
- Landfall registers only `AddNotifyPeerAuthStatusChanged` and `AddNotifyMessageToServer`. It does
  **not** register `AddNotifyClientIntegrityViolated` or `RegisterPeer`, so the client never kicks
  itself on an integrity callback. Server-side EAC, plus EAC's own client-side behaviour, decides
  that.

The join packet also carries identity material the server can verify (`ServerConnector.SendInitRequest`):

- the Steam auth ticket,
- the EOS Connect ID token (JWT) and EOS ProductUserId,
- the PlayFab ID and the Unity Authentication player ID.

## 3. What EAC gates

- **All networked play.** There is no offline or single-player mode in this client. The build scene
  list (`globalgamemanagers` BuildSettings) is `MainMenu`, `MainWorld_Base`, `WilhelmTest`,
  `ColdCastle`, `DevTest`, `TABG_CS`, `CastleFight`, `DesertCastle` and `Area_COMBINED`. Every
  gameplay scene is loaded only from `ServerConnector.LoadGameScene` after a server
  `RoomInitRequestResponse` is accepted.
- Every join path shares the same `BeginSession` requirement:
  - official matchmaking (Unity Matchmaker/Multiplay, or Landfall's backend `Matchmake`),
  - the community server browser,
  - direct IP connect,
  - Unity Relay join codes,
  - Steam lobby forced connects.

  `MODDING_PLAN.md` §4 lists the call sites.
- Without the bootstrapper, the client reaches the menu but cannot hold a server connection. We
  infer this from the null-client fallback plus the `Disconnect()` on failure. It was not observed,
  because the game was not run here.

## 4. CodeStage ACTk (the second, in-process anti-cheat library)

- ACTk is compiled into `Assembly-CSharp-firstpass.dll` (`CodeStage.AntiCheat.*` namespaces).
- The only use in game code is `ObscuredPrefs` in `GearDatabase.cs:164-180`, which obfuscates the
  locally saved gear selection.
- **No ACTk detector is instantiated anywhere.** I scanned every serialized file with UnityPy
  (`level0`–`level8`, `sharedassets0`–`8`, `resources.assets`): zero MonoBehaviours with
  `InjectionDetector`, `SpeedHackDetector`, `TimeCheatingDetector`, `ObscuredCheatingDetector` or
  `WallHackDetector` scripts. No `AddComponent` of them appears in Assembly-CSharp either. The
  detector names do appear in `globalgamemanagers.assets`, but only as MonoScript entries, which is
  normal for every compiled script.
- Conclusion: ACTk is not a factor. EAC is.

## 5. Verdict for each route on community self-hosted servers

| Route | Verdict | Why |
|---|---|---|
| BepInEx 5 (doorstop `winhttp.dll` proxy) + plugin in the normal, bootstrapper-launched client | **No.** | It is an unsigned module loaded into an EAC-protected process. The client is protected no matter which server it joins. Expected outcomes are an EAC launch or integrity error, or a reported violation that an EAC-enforcing server acts on. A sanction record is a per-account risk that does not care whether the server was "community". That last point is general EAC behaviour, not verifiable from these files. |
| Launch `TotallyAccurateBattlegrounds.exe` directly (no bootstrapper) + BepInEx | **No (and it would not work anyway).** | `BeginSession` → `ClientProtectionNotAvailable` → client-side `Disconnect()`. Making it "work" requires defeating the check (below). |
| EOS null client (`EOS_USE_ANTICHEATCLIENTNULL`, present in the bootstrapper @0x30c49c and the SDK @0x10665a0; bootstrapper string @0x30bd00 "Dummy module has been requested, will launch without a live module.") | **No: out of scope.** | It is an Epic developer facility, not a player-facing offline mode. Using it to join servers is disabling anti-cheat, which the framework rules forbid ("Never: disable or bypass anti-cheat"). It is recorded here only so nobody "discovers" it later and assumes it is fine. |
| Harmony-patch `Easy_AC_Client.BeginSession` / `SetACInterface` to fake success | **No: anti-cheat bypass.** | Same as above. |
| External memory reading of TABG for camera pose | **No.** | It is the same thing EAC exists to stop, and functionally it is ESP plumbing. |
| Out-of-process overlay window (no injection, no memory reads, no synthetic input into TABG) | **Allowed, but low fidelity.** | It never touches the protected process. It cannot get TABG's depth buffer or camera, so no occlusion and no pose lock. See `MODDING_PLAN.md` §3 option C. |
| **Portal side** (custom server plugin / client hook in `hl2.exe`) | **Yes.** | No anti-cheat in the install (`um scan games/Portal`: `anti-cheat: none found`). `portal/gameinfo.txt`: `type singleplayer_only`. Unsigned modules only disable *secure servers*: engine.dll `Loading unsigned module %s` @0x350af8 → `Access to secure servers is disabled.` @0x350b13. Details in `portal-notes.md`. |

### What would change the verdict

Any one of these, confirmed in writing or in shipped files, reopens the BepInEx-in-TABG route. All
of them have to come from Landfall/Epic, not from us:

1. Landfall ships or endorses a **client launch mode without EAC**, for example a "modded / community
   servers" launch option in Steam that runs the exe without the bootstrapper, together with a
   client build whose `BeginSession` failure is tolerated for servers that don't require AC.
2. Landfall's community/dedicated-server program documents that server owners can disable EAC
   **and** that modded clients are permitted on those servers. Note the client-side check in §2:
   this alone is not enough unless (1) also holds.
3. An official TABG modding surface appears (Workshop, mod.io, a mod kit).

Until one of those exists, nothing gets injected into TABG. That includes the community-server
case.

## 6. Open items (cannot be resolved from files on this box)

- **How Steam launches TABG by default** (bootstrapper vs exe): this lives in Steam appinfo
  (appid 823130, from `ClientSteamLobby.cs:44` `new AppId_t(823130u)`), not in the install. The
  presence of `TABG_Launcher.exe` plus the `Settings.json` `executable` field strongly suggests the
  bootstrapper is the default.
- **Whether Landfall's dedicated-server build runs `AntiCheatServer`, and whether it can be
  disabled.** Server code is not in this client: `Epic.OnlineServices.AntiCheatServer` bindings
  exist, but nothing in Landfall's code calls them. Check the separately distributed TABG dedicated
  server package and its config once it is obtained.
- **Landfall's mod policy / EULA text on client mods.** Search "Landfall mod policy TABG" and the TABG
  Discord. Record the answer in `MODDING_PLAN.md` before any change of verdict.
