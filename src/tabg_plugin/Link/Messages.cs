using System;
using System.Collections.Generic;
using UnityEngine;

namespace TabgPortal.Link
{
    // Builders for the host->guest frames in src/link/protocol.md. Field names here match
    // the schema EXACTLY: hello, cam (f/ts/p/basis/fov/aspect/fp/pl/h), ground, key, slot.
    public static class Messages
    {
        private static long NowMicros()
        {
            // microseconds since Unix epoch
            return (DateTime.UtcNow - new DateTime(1970, 1, 1, 0, 0, 0, DateTimeKind.Utc)).Ticks / 10L;
        }

        public static string Hello(string name)
        {
            var m = new Dictionary<string, object>
            {
                ["t"] = "hello",
                ["role"] = "host",
                ["ver"] = 1,
                ["name"] = name,
                ["caps"] = new List<object> { "cam", "ground", "key", "slot", "depth", "event" },
            };
            return Json.Write(m);
        }

        public static string Bye(string reason)
        {
            return Json.Write(new Dictionary<string, object> { ["t"] = "bye", ["reason"] = reason });
        }

        // cam - camera pose, 60 Hz. Uses an orthonormal basis (protocol prefers basis over q).
        public static string Cam(long frame, Vector3 pos, Vector3 fwd, Vector3 right, Vector3 up,
                                 float fovDeg, float aspect, bool firstPerson, bool hasFeet, Vector3 feet, float headingDeg)
        {
            var m = new Dictionary<string, object>
            {
                ["t"] = "cam",
                ["f"] = frame,
                ["ts"] = NowMicros(),
                ["p"] = Vec3(pos),
                ["basis"] = new Dictionary<string, object>
                {
                    ["fwd"] = Vec3(fwd),
                    ["right"] = Vec3(right),
                    ["up"] = Vec3(up),
                },
                ["fov"] = (double)fovDeg,
                ["aspect"] = (double)aspect,
                ["fp"] = firstPerson,
            };
            if (hasFeet)
            {
                m["pl"] = Vec3(feet);
                m["h"] = (double)headingDeg;
            }
            return Json.Write(m);
        }

        // ground - chamber floor/level. origin = feet at re-level, normal = up, size = chamber extents.
        public static string Ground(Vector3 origin, Vector3 normal, float yawDeg, Vector3 size)
        {
            var m = new Dictionary<string, object>
            {
                ["t"] = "ground",
                ["origin"] = Vec3(origin),
                ["normal"] = Vec3(normal),
                ["yaw"] = (double)yawDeg,
                ["size"] = new List<object> { (double)size.x, (double)size.y, (double)size.z },
            };
            return Json.Write(m);
        }

        public static string KeyMouse(float dx, float dy, int buttons, float wheel)
        {
            var m = new Dictionary<string, object>
            {
                ["t"] = "key",
                ["ts"] = NowMicros(),
                ["code"] = "mouse",
                ["dx"] = (double)dx,
                ["dy"] = (double)dy,
                ["buttons"] = buttons,
                ["wheel"] = (double)wheel,
            };
            return Json.Write(m);
        }

        public static string KeyName(string name, bool down)
        {
            var m = new Dictionary<string, object>
            {
                ["t"] = "key",
                ["ts"] = NowMicros(),
                ["code"] = "key",
                ["name"] = name,
                ["down"] = down,
            };
            return Json.Write(m);
        }

        public static string Slot(int n)
        {
            return Json.Write(new Dictionary<string, object> { ["t"] = "slot", ["n"] = n });
        }

        private static List<object> Vec3(Vector3 v)
        {
            return new List<object> { (double)v.x, (double)v.y, (double)v.z };
        }
    }
}
