# env.md: TABG × Portal passthrough

Filled in from recon slice 1 (2026-10-02). Evidence is in `recon/` (start at `recon/MODDING_PLAN.md`).

| var | value | notes |
|---|---|---|
| `PORTAL_DIR` | this box: `games/Portal/` (copy). Windows (Steam default): `C:\Program Files (x86)\Steam\steamapps\common\Portal` | Portal, Steam appid **400** (`steam_appid.txt`). Valve 2024 `rel_singleplayer` rebuild: engine.dll built `Nov 25 2024`; `portal/steam.inf` PatchVersion/ClientVersion **1745010**, ServerVersion 9862575. Binary `hl2.exe` (x86, LAA); game code in `portal/bin/{client,server}.dll`. **All 32-bit.** `gameinfo.txt` `type singleplayer_only`. D3D9 default (`-vulkan` = DXVK). |
| `TABG_DIR` | this box: `games/TotallyAccurateBattlegrounds/` (copy). Windows (Steam default): `C:\Program Files (x86)\Steam\steamapps\common\TotallyAccurateBattlegrounds` | **Totally Accurate Battlegrounds** (Landfall, Steam appid **823130**). It is *not* "Tag: The Game". **Unity 2021.3.45f2 (88f88f591b2e), Mono, x64**, built-in RP + PPv2. Exe/UnityPlayer PE timestamp 2025-09-26; network version key `APP_VERSION 693fa81cb14a5bc46bf18456d161a7c6`. Steam buildid not recorded (no appmanifest in the copy). |
| `PASSTHROUGH_WIN_DIR` | `C:\dev\tabgportal` (suggested) | Windows working dir: build mirrors, jdk, pyenv, takes |
| `WS_PORT` | `25600` | link only on 127.0.0.1 (TABG's own direct-connect default is 7777/ENet; browser register uses 24124, so no clash) |
| `TABG_WINDOW` | windowed 1920x1080 suggested | Portal hidden window sized to match: `-windowed -noborder -w 1920 -h 1080` |
| `PORTAL_LAUNCH` | `-insecure -dx9 -windowed -noborder -w <W> -h <H> +mat_queue_mode 0 +engine_no_focus_sleep 0` | guest loads as a **server plugin** (`portal/addons/<name>.vdf`), not a replaced client.dll |
| `UNIT_SCALE` | 1 Source unit = **0.0254 m** (engine constant) | vphysics.dll 0.0254 / 39.37008. Choose an artistic scale explicitly if wanted; the README's 1.17 cm is not an engine value. |
| `TABG_CHAMBER_LAYER` | Unity layer **29 `OnlyLocalPlayer`** | collides only with layer 13 `LocalPlayer` (see `recon/physics-notes.md` §3) |

## Game files

- `games/Portal/`: Portal copy (this box). Windows uses the Steam path above.
- `games/TotallyAccurateBattlegrounds/`: TABG copy (this box). Windows uses the Steam path above.
- Never commit game files, decompiled dumps, or leaked keys to git (universal-modder CONTRIBUTING
  rule; `um publish check` flags them). Recon decompiles live in `/tmp/tabg_decomp/`, outside the repo.

## Check before first build

- TABG anti-cheat: **EasyAntiCheat (EOS) is present and required for ALL play, official and
  community alike.**
  - `TABG_Launcher.exe` is the EAC EOS bootstrapper (1.5.0).
  - The client calls `Easy_AC_Client.BeginSession()` on every server join and disconnects if it
    fails.
  - There is no single-player/offline mode.
  - → **No BepInEx/injection into TABG** until Landfall provides a sanctioned EAC-free path, or the
    host is swapped. See `recon/eac-analysis.md` and `recon/MODDING_PLAN.md` §3.
  - Portal: no anti-cheat.
- BepInEx version for the current TABG Unity runtime: **BepInEx 5.4.23.5 `win_x64`** (Unity
  2021.3.45f2 Mono x64; plugins `net472`/`netstandard2.0`). Technically matched, **gated by the AC
  decision above**.
- Source SDK build for current Portal client: **`ValveSoftware/source-sdk-2013` public headers,
  x86** (interface versions match: `VClient017`, `VEngineClient014`, `VEngineRenderView014`,
  `VMaterialSystem080`, `ISERVERPLUGINCALLBACKS003`). Build a server-plugin DLL, MSVC x86 (game
  linker 12.0 = VS2013). Portal's own game code is not in the SDK, so don't rebuild `client.dll`.
