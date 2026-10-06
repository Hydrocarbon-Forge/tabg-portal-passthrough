using UnityEngine;
using TabgPortal.Link;

namespace TabgPortal.Sender
{
    // Reads the local player's camera pose + feet each frame and pushes a `cam` frame to the link.
    // Driven from Plugin.Update (not a MonoBehaviour itself, so it needs no GameObject wiring).
    //
    // Decompile evidence (acs):
    //   SetMainCamera.cs:9-11  tags the active camera "MainCamera" -> Camera.main resolves it
    //   Player.cs:10 class Player; Player.cs:51 m_hasControl (HasControl); HasControl.cs:5 hasControl
    //   Player.cs:180 public Hip Hip => GetComponentInChildren<Hip>()  (feet/body anchor)
    public sealed class CameraFeetSender
    {
        private readonly LinkClient _link;
        private long _frame;
        private Player _localPlayer;
        private float _rediscoverTimer;

        public CameraFeetSender(LinkClient link)
        {
            _link = link;
        }

        // Current local player (cached; rediscovered when null or stale). Also used by the chamber.
        public Player LocalPlayer
        {
            get
            {
                if (_localPlayer == null) RediscoverPlayer();
                return _localPlayer;
            }
        }

        public bool TryGetFeet(out Vector3 feet, out float headingDeg)
        {
            feet = Vector3.zero;
            headingDeg = 0f;
            var p = LocalPlayer;
            if (p == null) return false;
            var hip = p.Hip;
            feet = hip != null ? hip.transform.position : p.transform.position;
            headingDeg = p.transform.eulerAngles.y;
            return true;
        }

        public void Tick(float dt)
        {
            if (_link == null || !_link.Connected) return;

            _rediscoverTimer += dt;
            if (_localPlayer == null && _rediscoverTimer > 0.5f)
            {
                _rediscoverTimer = 0f;
                RediscoverPlayer();
            }

            Camera cam = Camera.main;
            if (cam == null) return;

            Transform t = cam.transform;
            bool hasFeet = TryGetFeet(out Vector3 feet, out float heading);

            string msg = Messages.Cam(
                _frame++,
                t.position,
                t.forward, t.right, t.up,
                cam.fieldOfView,
                cam.aspect,
                firstPerson: true,
                hasFeet: hasFeet,
                feet: feet,
                headingDeg: heading);
            _link.Send(msg);
        }

        private void RediscoverPlayer()
        {
            // Prefer the locally controlled Player (SetRigidbodySettings puts it on layer 13).
            Player[] players = Object.FindObjectsOfType<Player>();
            for (int i = 0; i < players.Length; i++)
            {
                var hc = players[i].m_hasControl;
                if (hc != null && hc.hasControl)
                {
                    _localPlayer = players[i];
                    return;
                }
            }
            _localPlayer = null;
        }
    }
}
