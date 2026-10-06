using System;
using System.IO;
using BepInEx.Logging;
using UnityEngine;
using UnityEngine.Rendering;

namespace TabgPortal.Composite
{
    // Composite pass. DESIGN CHOICE (recon section 6): a CommandBuffer at
    // CameraEvent.BeforeImageEffects on the main camera, NOT a PPv2 custom effect.
    // Rationale: a PPv2 PostProcessEffectRenderer needs a serialized PostProcessEffectSettings
    // asset registered into the scene camera's PostProcessLayer resources - awkward to inject from
    // a BepInEx plugin at runtime. A CommandBuffer attached directly to Camera.main is
    // self-contained (needs only the camera ref) and still runs before the game's image effects.
    // Rejected alternative (PPv2 custom effect) documented in README.md.
    //
    // Reads the latest frame-channel slot (FrameChannelReader), uploads colour+depth to textures,
    // computes the guest inverse-VP (from the pose snapshot) and the current camera VP for
    // reprojection, and blits the "Hidden/TabgPortal/PortalPass" shader over the camera target.
    public sealed class PortalCompositePass : IDisposable
    {
        private readonly ManualLogSource _log;
        private readonly FrameChannelReader _reader;
        private readonly float _guestNear;
        private readonly float _guestFar;

        private Material _mat;
        private Texture2D _colorTex;
        private Texture2D _depthTex;
        private CommandBuffer _cmd;
        private Camera _attachedCam;
        private bool _shaderMissingLogged;

        public bool HasShader => _mat != null;

        public PortalCompositePass(ManualLogSource log, FrameChannelReader reader, string pluginDir, float guestNear, float guestFar)
        {
            _log = log;
            _reader = reader;
            _guestNear = guestNear;
            _guestFar = guestFar;
            TryLoadShader(pluginDir);
        }

        private void TryLoadShader(string pluginDir)
        {
            try
            {
                // Prefer an already-loaded shader (e.g. shipped in another bundle), else load our bundle.
                Shader shader = Shader.Find("Hidden/TabgPortal/PortalPass");
                if (shader == null)
                {
                    string bundlePath = Path.Combine(pluginDir, "portalpass.bundle");
                    if (File.Exists(bundlePath))
                    {
                        var bundle = AssetBundle.LoadFromFile(bundlePath);
                        if (bundle != null)
                        {
                            shader = bundle.LoadAsset<Shader>("PortalPass");
                            if (shader == null) shader = Shader.Find("Hidden/TabgPortal/PortalPass");
                        }
                    }
                }
                if (shader != null) _mat = new Material(shader);
                else _log.LogWarning("[composite] PortalPass shader not found (portalpass.bundle missing?) - composite disabled.");
            }
            catch (Exception e)
            {
                _log.LogError("[composite] shader load failed: " + e.Message);
            }
        }

        // Called each frame (main thread). Polls the frame channel and refreshes the overlay.
        public void Tick()
        {
            if (_mat == null)
            {
                if (!_shaderMissingLogged) { _shaderMissingLogged = true; }
                return;
            }

            Camera cam = Camera.main;
            if (cam == null) return;
            cam.depthTextureMode |= DepthTextureMode.Depth; // ensure _CameraDepthTexture exists

            EnsureAttached(cam);

            bool advanced = _reader.Poll();
            var fd = _reader.Latest;
            if (fd == null) return;
            if (advanced) UploadFrame(fd);

            UpdateMatrices(cam, fd);
        }

        private void EnsureAttached(Camera cam)
        {
            if (_attachedCam == cam && _cmd != null) return;
            Detach();

            _cmd = new CommandBuffer { name = "TabgPortal.Composite" };
            // Blit the overlay over the camera's current target using our material.
            _cmd.Blit(BuiltinRenderTextureType.CameraTarget, BuiltinRenderTextureType.CameraTarget, _mat, 0);
            cam.AddCommandBuffer(CameraEvent.BeforeImageEffects, _cmd);
            _attachedCam = cam;
        }

        private void UploadFrame(FrameData fd)
        {
            if (_colorTex == null || _colorTex.width != fd.Width || _colorTex.height != fd.Height)
            {
                UnityEngine.Object.Destroy(_colorTex);
                UnityEngine.Object.Destroy(_depthTex);
                _colorTex = new Texture2D(fd.Width, fd.Height, TextureFormat.RGBA32, false, true);
                _depthTex = new Texture2D(fd.Width, fd.Height, TextureFormat.RFloat, false, true);
                _colorTex.wrapMode = TextureWrapMode.Clamp;
                _depthTex.wrapMode = TextureWrapMode.Clamp;
            }
            _colorTex.LoadRawTextureData(fd.Color);
            _colorTex.Apply(false);
            // depth is float32 per pixel -> RFloat
            _depthTex.LoadRawTextureData(FloatsToBytes(fd.Depth));
            _depthTex.Apply(false);

            _mat.SetTexture("_PortalColor", _colorTex);
            _mat.SetTexture("_PortalDepth", _depthTex);
            _mat.SetInt("_PortalDepthConvention", fd.DepthConvention);
            _mat.SetInt("_PortalRowsTopDown", fd.RowsTopDown ? 1 : 0);
            _mat.SetVector("_PortalNearFar", new Vector4(_guestNear, _guestFar, 0, 0));
        }

        private void UpdateMatrices(Camera cam, FrameData fd)
        {
            // Guest view-projection from the pose snapshot (framechannel.md descriptor).
            float aspect = cam.aspect; // chamber frame matches our aspect (link cam carries it)
            Matrix4x4 guestProj = Matrix4x4.Perspective(fd.Fov, aspect, _guestNear, _guestFar);
            guestProj = GL.GetGPUProjectionMatrix(guestProj, true);
            Matrix4x4 guestView = Matrix4x4.TRS(fd.Pos, fd.Rot, new Vector3(1, 1, -1)).inverse;
            Matrix4x4 guestVP = guestProj * guestView;
            _mat.SetMatrix("_PortalInvVP", guestVP.inverse);

            Matrix4x4 curProj = GL.GetGPUProjectionMatrix(cam.projectionMatrix, true);
            Matrix4x4 curVP = curProj * cam.worldToCameraMatrix;
            _mat.SetMatrix("_CurrentVP", curVP);
        }

        private static byte[] FloatsToBytes(float[] src)
        {
            var bytes = new byte[src.Length * 4];
            Buffer.BlockCopy(src, 0, bytes, 0, bytes.Length);
            return bytes;
        }

        private void Detach()
        {
            if (_attachedCam != null && _cmd != null)
            {
                try { _attachedCam.RemoveCommandBuffer(CameraEvent.BeforeImageEffects, _cmd); } catch { }
            }
            _cmd?.Release();
            _cmd = null;
            _attachedCam = null;
        }

        public void Dispose()
        {
            Detach();
            if (_colorTex != null) UnityEngine.Object.Destroy(_colorTex);
            if (_depthTex != null) UnityEngine.Object.Destroy(_depthTex);
        }
    }
}
