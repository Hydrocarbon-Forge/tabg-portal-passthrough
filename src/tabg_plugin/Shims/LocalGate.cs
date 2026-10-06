using System.Collections.Generic;
using System.IO;
using TabgPortal.Link;

namespace TabgPortal.Shims
{
    // The mandatory safety gate for the G.2 EAC/auth shims.
    //
    // The EAC shims (no-op Easy_AC_Client, fake EOS token, fake PlayFab id, etc.) are what make a
    // client connect to a NO-EAC server. That is only legitimate for the owner's OWN LAN /
    // single-machine self-hosted server running AntiCheat=false. To hold it there, the shims are
    // armed ONLY when BOTH are true:
    //   1. a file  LOCAL_ONLY_GATE.txt  exists next to the plugin DLL, AND
    //   2. a file  local_only.json      exists next to it with
    //        { "i_understand_this_is_local_only": true }
    //
    // If either is missing, the shims stay OFF and the plugin logs that it is in composite-only mode.
    // When they ARE armed, Plugin.cs logs LOUDLY on boot.
    public static class LocalGate
    {
        public const string GateFileName = "LOCAL_ONLY_GATE.txt";
        public const string FlagFileName = "local_only.json";

        public static bool Armed { get; private set; }
        public static string Reason { get; private set; } = "not evaluated";

        public static bool Evaluate(string pluginDir)
        {
            string gatePath = Path.Combine(pluginDir, GateFileName);
            string flagPath = Path.Combine(pluginDir, FlagFileName);

            if (!File.Exists(gatePath))
            {
                Armed = false;
                Reason = GateFileName + " not present next to the plugin";
                return false;
            }
            if (!File.Exists(flagPath))
            {
                Armed = false;
                Reason = FlagFileName + " not present next to the plugin";
                return false;
            }

            bool understood = false;
            try
            {
                if (Json.Parse(File.ReadAllText(flagPath)) is Dictionary<string, object> map)
                    understood = Json.GetBool(map, "i_understand_this_is_local_only", false);
            }
            catch
            {
                understood = false;
            }

            if (!understood)
            {
                Armed = false;
                Reason = FlagFileName + " present but i_understand_this_is_local_only != true";
                return false;
            }

            Armed = true;
            Reason = "gate file + flag both present and acknowledged";
            return true;
        }
    }
}
