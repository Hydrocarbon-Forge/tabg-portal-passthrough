using System.Collections.Generic;
using System.IO;
using UnityEngine;
using TabgPortal.Link;

namespace TabgPortal.Chamber
{
    // Builds the invisible chamber collider set on F8 (re-level).
    //
    // Per recon/physics-notes.md section 3: layer 29 "OnlyLocalPlayer" collides only with layer 13
    // "LocalPlayer" (which SetRigidbodySettings puts the local character on). Static colliders with
    // no MeshRenderer are invisible and act as real ground/walls for ONLY our own character, and
    // are NOT in the MakeSureIDontGoThroughThings auto-kill mask (0x1200 = Map|Terrain), so they
    // can't trigger the self-damage tunnelling kill.
    public sealed class ChamberConfig
    {
        public Vector3 Size = new Vector3(21f, 16f, 11f); // [X width, Y height, Z depth] in metres
        public float WallThickness = 0.5f;
        public bool WithCeiling = true;
        public bool WithFloor = true;
        public bool FloorAtFeet = true;
        public int Layer = 29;

        public static ChamberConfig Load(string path, out string note)
        {
            var cfg = new ChamberConfig();
            note = "defaults (21 x 16 x 11)";
            try
            {
                if (!File.Exists(path)) return cfg;
                if (Json.Parse(File.ReadAllText(path)) is Dictionary<string, object> map)
                {
                    if (map.TryGetValue("size", out var sz) && sz is List<object> l && l.Count == 3)
                        cfg.Size = new Vector3(ToF(l[0]), ToF(l[2]), ToF(l[1])); // json [w,depth,height] -> Unity X,Y(height),Z(depth)
                    cfg.WallThickness = (float)Json.GetDouble(map, "wall_thickness", cfg.WallThickness);
                    cfg.WithCeiling = Json.GetBool(map, "with_ceiling", cfg.WithCeiling);
                    cfg.WithFloor = Json.GetBool(map, "with_floor", cfg.WithFloor);
                    cfg.FloorAtFeet = Json.GetBool(map, "floor_at_feet", cfg.FloorAtFeet);
                    cfg.Layer = (int)Json.GetDouble(map, "layer", cfg.Layer);
                    note = "chamber.json";
                }
            }
            catch { /* keep defaults */ }
            return cfg;
        }

        private static float ToF(object o) { return o is double d ? (float)d : 0f; }

        // chamber.json stores size as [width(X), depth(Z), height(Y)]. We remap into Unity's
        // (X,Y,Z) with Y = height. Expose the three semantic extents for the `ground` message.
        public float Width => Size.x;   // X
        public float Height => Size.y;  // Y
        public float Depth => Size.z;   // Z
    }

    public sealed class ChamberBuilder
    {
        private readonly ChamberConfig _cfg;
        private GameObject _root;
        public Vector3 Origin { get; private set; }
        public float Yaw { get; private set; }

        public ChamberBuilder(ChamberConfig cfg) { _cfg = cfg; }

        // Build (or rebuild) the chamber at `origin` (player feet) with heading `yawDeg`.
        public void Build(Vector3 origin, float yawDeg)
        {
            Destroy();
            Origin = origin;
            Yaw = yawDeg;

            _root = new GameObject("PortalChamber");
            _root.transform.position = origin;
            _root.transform.rotation = Quaternion.Euler(0f, yawDeg, 0f);

            float w = _cfg.Width, h = _cfg.Height, d = _cfg.Depth, tk = _cfg.WallThickness;
            float floorY = _cfg.FloorAtFeet ? 0f : -h * 0.5f;

            if (_cfg.WithFloor)
                AddBox("Floor", new Vector3(0f, floorY - tk * 0.5f, 0f), new Vector3(w, tk, d));
            if (_cfg.WithCeiling)
                AddBox("Ceiling", new Vector3(0f, floorY + h + tk * 0.5f, 0f), new Vector3(w, tk, d));

            float midY = floorY + h * 0.5f;
            AddBox("Wall_+X", new Vector3(w * 0.5f + tk * 0.5f, midY, 0f), new Vector3(tk, h, d));
            AddBox("Wall_-X", new Vector3(-w * 0.5f - tk * 0.5f, midY, 0f), new Vector3(tk, h, d));
            AddBox("Wall_+Z", new Vector3(0f, midY, d * 0.5f + tk * 0.5f), new Vector3(w, h, tk));
            AddBox("Wall_-Z", new Vector3(0f, midY, -d * 0.5f - tk * 0.5f), new Vector3(w, h, tk));
        }

        private void AddBox(string name, Vector3 localCenter, Vector3 size)
        {
            var go = new GameObject(name);
            go.transform.SetParent(_root.transform, false);
            go.transform.localPosition = localCenter;
            go.layer = _cfg.Layer; // 29 OnlyLocalPlayer - invisible (no renderer) & local-only collisions
            var col = go.AddComponent<BoxCollider>();
            col.size = size;
            col.isTrigger = false; // solid: stops/supports the local character (static => counts as ground)
        }

        // Axis-aligned world AABB of the interior (for CharacterLock "inside chamber" tests).
        public Bounds InteriorBounds()
        {
            float floorY = _cfg.FloorAtFeet ? 0f : -_cfg.Height * 0.5f;
            var center = Origin + new Vector3(0f, floorY + _cfg.Height * 0.5f, 0f);
            // yaw rotates the box; for a conservative AABB use the diagonal of width/depth.
            float diag = Mathf.Sqrt(_cfg.Width * _cfg.Width + _cfg.Depth * _cfg.Depth);
            return new Bounds(center, new Vector3(diag, _cfg.Height, diag));
        }

        public string GroundMessage()
        {
            return Messages.Ground(Origin, Vector3.up, Yaw,
                new Vector3(_cfg.Width, _cfg.Depth, _cfg.Height)); // protocol size = [w, depth, height]
        }

        public void Destroy()
        {
            if (_root != null) { Object.Destroy(_root); _root = null; }
        }
    }
}
