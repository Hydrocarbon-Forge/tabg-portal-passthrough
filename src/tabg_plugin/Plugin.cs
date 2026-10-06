using System;
using System.Collections.Generic;
using System.IO;
using BepInEx;
using BepInEx.Logging;
using HarmonyLib;
using UnityEngine;
using TabgPortal.Chamber;
using TabgPortal.Composite;
using TabgPortal.Link;
using TabgPortal.Sender;
using TabgPortal.Shims;

namespace TabgPortal
{
    [BepInPlugin(MyPluginInfo.PLUGIN_GUID, MyPluginInfo.PLUGIN_NAME, MyPluginInfo.PLUGIN_VERSION)]
    public sealed class Plugin : BaseUnityPlugin
    {
        private ManualLogSource _log;
        private Harmony _harmony;

        private LinkClient _link;
        private CameraFeetSender _cameraSender;
        private ChamberBuilder _chamberBuilder;
        private FrameChannelReader _frameReader;
        private PortalCompositePass _compositePass;

        private bool _enableCharLock = false;
        private bool _charHidden = false;
        private readonly List<Renderer> _hiddenRenderers = new List<Renderer>();

        private void Awake()
        {
            _log = Logger;
            _log.LogInfo($"Initializing {MyPluginInfo.PLUGIN_NAME} v{MyPluginInfo.PLUGIN_VERSION}");

            string pluginDir = Path.GetDirectoryName(Info.Location);
            if (string.IsNullOrEmpty(pluginDir))
            {
                pluginDir = Paths.PluginPath;
            }

            // Read local identity (player name, playfab id)
            string playerName = "LocalPlayer";
            string playFabId = "local-player";
            string localJsonPath = Path.Combine(pluginDir, "local.json");
            if (File.Exists(localJsonPath))
            {
                try
                {
                    if (Json.Parse(File.ReadAllText(localJsonPath)) is Dictionary<string, object> dict)
                    {
                        playerName = Json.GetString(dict, "player_name", playerName);
                        playFabId = Json.GetString(dict, "playfab_id", playFabId);
                    }
                }
                catch (Exception ex)
                {
                    _log.LogWarning($"Failed to read local.json: {ex.Message}");
                }
            }

            // G.2 EAC / auth shims: evaluated behind LocalGate
            if (LocalGate.Evaluate(pluginDir))
            {
                _log.LogWarning("===============================================================");
                _log.LogWarning("[LOCAL ONLY] LocalGate is ARMED. EAC and auth shims active.");
                _log.LogWarning($"Local identity: player='{playerName}', playfab_id='{playFabId}'.");
                _log.LogWarning("This setup is for local LAN / dedicated server ONLY (AntiCheat=false).");
                _log.LogWarning("===============================================================");

                _harmony = new Harmony(MyPluginInfo.PLUGIN_GUID);
                EacShims.Apply(_harmony, _log, playerName, playFabId);
            }
            else
            {
                _log.LogInfo($"LocalGate disarmed ({LocalGate.Reason}). Running in composite-only mode.");
            }

            // Chamber configuration
            string chamberJsonPath = Path.Combine(pluginDir, "chamber.json");
            var chamberCfg = ChamberConfig.Load(chamberJsonPath, out string note);
            _log.LogInfo($"[chamber] Loaded configuration from {note}");
            _chamberBuilder = new ChamberBuilder(chamberCfg);

            // Link client (WebSocket to 127.0.0.1:25600)
            try
            {
                _link = new LinkClient("ws://127.0.0.1:25600", "tabg", 240, msg => _log.LogInfo($"[link] {msg}"));
                _link.Start();
            }
            catch (Exception ex)
            {
                _log.LogError($"Failed to start LinkClient: {ex.Message}");
            }

            // Camera + feet sender
            _cameraSender = new CameraFeetSender(_link);

            // Composite pass (FrameChannel shared memory reader + Blit pass)
            _frameReader = new FrameChannelReader("TABGPortalFrame");
            _compositePass = new PortalCompositePass(_log, _frameReader, pluginDir, 0.1f, 1000f);

            _log.LogInfo("Plugin Awake complete.");
        }

        private void Update()
        {
            float dt = Time.deltaTime;

            // 1. Tick camera sender
            _cameraSender?.Tick(dt);

            // 2. Tick composite pass
            _compositePass?.Tick();

            // 3. F8 keypress to re-level / spawn chamber at feet
            if (Input.GetKeyDown(KeyCode.F8))
            {
                if (_cameraSender != null && _cameraSender.TryGetFeet(out Vector3 feet, out float yaw))
                {
                    _log.LogInfo($"[chamber] F8 pressed: building chamber at feet {feet}, yaw {yaw:F1}°");
                    _chamberBuilder?.Build(feet, yaw);
                    if (_link != null && _link.Connected && _chamberBuilder != null)
                    {
                        _link.Send(_chamberBuilder.GroundMessage());
                    }
                }
                else
                {
                    _log.LogWarning("[chamber] F8 pressed but local player feet could not be determined.");
                }
            }

            // 4. Character hide / lock while inside chamber AABB (if enabled)
            if (_enableCharLock && _chamberBuilder != null)
            {
                HandleCharacterHide();
            }
        }

        private void HandleCharacterHide()
        {
            var player = _cameraSender?.LocalPlayer;
            if (player == null)
            {
                RestoreCharacterRenderers();
                return;
            }

            Vector3 pos = player.transform.position;
            Bounds bounds = _chamberBuilder.InteriorBounds();
            bool inside = bounds.Contains(pos);

            if (inside && !_charHidden)
            {
                _charHidden = true;
                _hiddenRenderers.Clear();
                foreach (var r in player.GetComponentsInChildren<Renderer>(true))
                {
                    if (r != null && r.enabled)
                    {
                        r.enabled = false;
                        _hiddenRenderers.Add(r);
                    }
                }
                _log.LogInfo("[char-lock] Local player entered chamber AABB: character hidden.");
            }
            else if (!inside && _charHidden)
            {
                RestoreCharacterRenderers();
                _log.LogInfo("[char-lock] Local player exited chamber AABB: character restored.");
            }
        }

        private void RestoreCharacterRenderers()
        {
            if (!_charHidden) return;
            _charHidden = false;
            foreach (var r in _hiddenRenderers)
            {
                if (r != null) r.enabled = true;
            }
            _hiddenRenderers.Clear();
        }

        private void OnDestroy()
        {
            RestoreCharacterRenderers();
            _compositePass?.Dispose();
            _frameReader?.Dispose();
            _link?.Dispose();
            _chamberBuilder?.Destroy();
            _harmony?.UnpatchSelf();
            _log?.LogInfo("Plugin destroyed and cleaned up.");
        }
    }
}
