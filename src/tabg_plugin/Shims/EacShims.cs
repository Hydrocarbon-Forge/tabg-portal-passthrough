using System;
using System.Reflection;
using BepInEx.Logging;
using HarmonyLib;
using Epic.OnlineServices;
using Epic.OnlineServices.Connect;
using Landfall;
using Landfall.Network;

namespace TabgPortal.Shims
{
    // G.2 client-side EAC / auth shims. ARMED ONLY behind LocalGate (LOCAL_ONLY_GATE.txt +
    // local_only.json). All of these exist purely so the owner's own client can connect to their
    // own LAN no-EAC dedicated server (G.1 patches out the server's real VerifyIdToken).
    //
    // Decompile evidence (client acs, ilspycmd 9.1 from Assembly-CSharp.dll; paths relative to
    // /tmp/tabg_decomp/acs):
    //   Landfall/Easy_AC_Client.cs:34-48  SetACInterface -> Application.Quit() if interface null
    //   Landfall/Easy_AC_Client.cs:69-84  BeginSession -> Disconnect() on non-Success
    //   Landfall/Easy_AC_Client.cs:86-93  EndSession (NPE-safe already, we no-op anyway)
    //   Landfall/Easy_AC_Client.cs:95-102 HandleACMessage -> m_Interface.ReceiveMessageFromServer (NPE if null)
    //   Landfall.Network/ServerConnector.cs:197-271  SendInitRequest (the two gates + EOS body)
    //   EOSSDKComponent.cs:183-199  GetUserToken(out IdToken?) -> bails to menu on non-Success
    //   EOSSDKComponent.cs:41  public static ProductUserId LocalUser { get; private set; }
    //   Landfall.Network/ClientNetworkerBase.cs:24-27  GetPlayerName() => SteamFriends.GetPersonaName()
    //   Landfall.Network/PhotonServerConnector.cs:175  public static string PlayFabID { get; private set; }
    //   PlayFabManager.cs:40,52  m_currentSteamTicket / SteamTicket (null Item1 without Steam)
    public static class EacShims
    {
        private static ManualLogSource _log;
        private static string _playerName = "LocalPlayer";
        private static string _playFabId = "local-player";

        public static void Apply(Harmony harmony, ManualLogSource log, string playerName, string playFabId)
        {
            _log = log;
            if (!string.IsNullOrEmpty(playerName)) _playerName = playerName;
            if (!string.IsNullOrEmpty(playFabId)) _playFabId = playFabId;

            // ---- Easy_AC_Client no-ops (skip original entirely) ----
            PatchSkip(harmony, typeof(Easy_AC_Client), "SetACInterface");
            PatchSkip(harmony, typeof(Easy_AC_Client), "BeginSession");
            PatchSkip(harmony, typeof(Easy_AC_Client), "EndSession");
            PatchSkip(harmony, typeof(Easy_AC_Client), "HandleACMessage");

            // ---- made-up player name (also dodges SteamFriends.GetPersonaName when Steam is absent) ----
            Patch(harmony, typeof(ClientNetworkerBase), "GetPlayerName",
                  postfix: nameof(GetPlayerName_Postfix));

            // ---- EOS token fake ----
            Patch(harmony, typeof(EOSSDKComponent), "GetUserToken",
                  prefix: nameof(GetUserToken_Prefix));

            // ---- SendInitRequest gate setup (runs BEFORE the body's null checks) ----
            Patch(harmony, typeof(ServerConnector), "SendInitRequest",
                  prefix: nameof(SendInitRequest_Prefix));

            log.LogWarning("[G.2] EAC/auth shims applied: Easy_AC_Client no-ops, EOS token fake, " +
                           "PlayFab/Steam gate bypass, player name='" + _playerName + "'.");
        }

        private static void PatchSkip(Harmony harmony, Type type, string method)
        {
            var mi = AccessTools.Method(type, method);
            if (mi == null) { _log.LogError("[G.2] target not found: " + type.Name + "." + method); return; }
            harmony.Patch(mi, prefix: new HarmonyMethod(typeof(EacShims), nameof(SkipOriginal)));
        }

        private static void Patch(Harmony harmony, Type type, string method, string prefix = null, string postfix = null)
        {
            var mi = AccessTools.Method(type, method);
            if (mi == null) { _log.LogError("[G.2] target not found: " + type.Name + "." + method); return; }
            harmony.Patch(mi,
                prefix: prefix != null ? new HarmonyMethod(typeof(EacShims), prefix) : null,
                postfix: postfix != null ? new HarmonyMethod(typeof(EacShims), postfix) : null);
        }

        // Generic "no-op the original" prefix.
        public static bool SkipOriginal()
        {
            return false;
        }

        // GetPlayerName -> return configured local name (server only requires NON-EMPTY, G.1).
        public static void GetPlayerName_Postfix(ref string __result)
        {
            __result = _playerName;
        }

        // GetUserToken(out IdToken? IDtoken): fabricate a Success token. The JWT content is
        // irrelevant because G.1 removes the server's VerifyIdToken. Parameter name must match
        // the decompiled "IDtoken".
        public static bool GetUserToken_Prefix(ref IdToken? IDtoken, ref Result __result)
        {
            IDtoken = new IdToken { JsonWebToken = (Utf8String)"local" };
            __result = Result.Success;
            return false; // skip original (which would NPE on the null EOS platform interface)
        }

        // SendInitRequest prefix: fix the inputs that the body reads but that are null/empty
        // without Steam/Epic/PlayFab, so the ORIGINAL body can run and build the room-init buffer.
        //   1. PhotonServerConnector.PlayFabID  (gate 1: empty -> Disconnect)
        //   2. PlayFabManager.m_currentSteamTicket.Item1 (null -> NPE at buffer sizing)
        //   3. EOSSDKComponent.LocalUser  (null -> LocalUser.ToString() fails -> early return)
        public static bool SendInitRequest_Prefix()
        {
            TrySetPlayFabId();
            TrySetEmptySteamTicket();
            TrySetLocalUser();
            return true; // continue into the (now-satisfiable) original body
        }

        private static void TrySetPlayFabId()
        {
            try
            {
                var prop = typeof(PhotonServerConnector).GetProperty("PlayFabID",
                    BindingFlags.Public | BindingFlags.Static);
                var setter = prop?.GetSetMethod(true);
                if (setter != null) { setter.Invoke(null, new object[] { _playFabId }); return; }
                SetBackingField(typeof(PhotonServerConnector), "PlayFabID", null, _playFabId);
            }
            catch (Exception e) { _log.LogError("[G.2] set PlayFabID failed: " + e.Message); }
        }

        private static void TrySetEmptySteamTicket()
        {
            try
            {
                var fi = typeof(PlayFabManager).GetField("m_currentSteamTicket",
                    BindingFlags.NonPublic | BindingFlags.Static);
                if (fi == null) return;
                var current = fi.GetValue(null);
                // Only fill if Item1 is null/empty; leave a real Steam ticket alone.
                var item1 = fi.FieldType.GetField("Item1")?.GetValue(current) as string;
                if (string.IsNullOrEmpty(item1))
                {
                    object tuple = Activator.CreateInstance(fi.FieldType);
                    fi.FieldType.GetField("Item1").SetValue(tuple, string.Empty);
                    fi.FieldType.GetField("Item2").SetValue(tuple, new byte[0]);
                    fi.SetValue(null, tuple);
                }
            }
            catch (Exception e) { _log.LogError("[G.2] set SteamTicket failed: " + e.Message); }
        }

        private static void TrySetLocalUser()
        {
            try
            {
                var prop = typeof(EOSSDKComponent).GetProperty("LocalUser",
                    BindingFlags.Public | BindingFlags.Static);
                var current = prop?.GetValue(null) as ProductUserId;
                if (current != null && current.IsValid()) return; // real EOS user, leave it

                // A valid-looking product user id so the body's LocalUser.ToString(out buffer)
                // returns Success. FromString is a stateless EOS string<->handle conversion.
                ProductUserId fake = ProductUserId.FromString("0002" + new string('a', 28));
                var setter = prop?.GetSetMethod(true);
                if (setter != null) setter.Invoke(null, new object[] { fake });
                else SetBackingField(typeof(EOSSDKComponent), "LocalUser", null, fake);
            }
            catch (Exception e) { _log.LogError("[G.2] set LocalUser failed (EOS native?): " + e.Message); }
        }

        private static void SetBackingField(Type type, string propName, object instance, object value)
        {
            var fi = type.GetField("<" + propName + ">k__BackingField",
                BindingFlags.NonPublic | BindingFlags.Static | BindingFlags.Instance);
            fi?.SetValue(instance, value);
        }
    }
}
