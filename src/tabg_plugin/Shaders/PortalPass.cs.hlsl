// PortalPass - composite overlay for the TABG host (slice D, recon section 6).
//
// This is the SOURCE for the composite shader. Unity's built-in RP cannot compile HLSL at
// runtime, so on the Windows box this must be compiled into an AssetBundle with the EXACT editor
// Unity 2021.3.45f2 (see src/tabg_plugin/README.md "Building the shader bundle"). The plugin loads
// the bundle "portalpass.bundle" from next to the DLL and finds Shader "Hidden/TabgPortal/PortalPass".
// If the bundle is absent the composite is skipped (logged) - the rest of the plugin still runs.
//
// What it does (CommandBuffer blit at CameraEvent.BeforeImageEffects):
//   1. Sample the guest colour (_PortalColor) and guest depth (_PortalDepth) for this screen pixel,
//      after reprojecting from the pose the guest RENDERED with to the current TABG camera (the
//      reference's latency-hiding trick). Reprojection uses _PortalInvVP (guest inverse view-proj,
//      from the frame-channel pose snapshot) and the current camera via unity_CameraInvProjection /
//      UNITY_MATRIX_VP supplied as _CurrentVP.
//   2. Linearise both the guest depth and TABG's _CameraDepthTexture. TABG runs D3D11 reversed-Z
//      (UNITY_REVERSED_Z): _CameraDepthTexture is 1=near, 0=far. Linearise with _ZBufferParams
//      (Linear01Depth). The guest depth convention is passed in _PortalDepthConvention and the
//      guest near/far in _PortalNearFar (framechannel.md "Depth convention flag").
//   3. Occlude: draw the overlay only where the reprojected guest eye-depth is NEARER than TABG's
//      scene eye-depth at that pixel (so TABG geometry in front of the chamber hides the overlay).

Shader "Hidden/TabgPortal/PortalPass"
{
    Properties
    {
        _PortalColor ("Guest Colour", 2D) = "black" {}
        _PortalDepth ("Guest Depth", 2D) = "black" {}
    }
    SubShader
    {
        // Overlay, no depth write/test of its own - we do manual depth comparison in the fragment.
        Cull Off ZWrite Off ZTest Always
        Blend SrcAlpha OneMinusSrcAlpha

        Pass
        {
            CGPROGRAM
            #pragma vertex vert
            #pragma fragment frag
            #include "UnityCG.cginc"

            sampler2D _PortalColor;
            sampler2D _PortalDepth;
            sampler2D _CameraDepthTexture;     // TABG scene depth (reversed-Z on D3D11)

            float4x4 _PortalInvVP;             // guest inverse view-projection (rendered pose)
            float4x4 _CurrentVP;               // current TABG camera view-projection
            float2   _PortalNearFar;           // guest near/far for linearising guest depth
            int      _PortalDepthConvention;   // framechannel.md values (0..4)
            int      _PortalRowsTopDown;        // 1 if guest rows are top-down

            struct v2f { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };

            v2f vert (appdata_img v)
            {
                v2f o;
                o.pos = UnityObjectToClipPos(v.vertex);
                o.uv = v.texcoord;
                return o;
            }

            // Linearise the guest depth sample to eye-space metres per its convention.
            float GuestEyeDepth(float d)
            {
                float n = _PortalNearFar.x;
                float f = _PortalNearFar.y;
                if (_PortalDepthConvention == 4)      // EYE_METRES
                    return d;
                if (_PortalDepthConvention == 1)      // REVERSED_Z_01 (1=near,0=far)
                    d = 1.0 - d;
                else if (_PortalDepthConvention == 3) // GL_NDC [-1,1]
                    d = d * 0.5 + 0.5;
                // now d is standard [0,1], 0=near. Convert to eye metres.
                if (_PortalDepthConvention == 0)      // LINEAR01 already
                    return lerp(n, f, d);
                return (n * f) / (f - d * (f - n));
            }

            fixed4 frag (v2f i) : SV_Target
            {
                float2 uv = i.uv;
                float2 guestUv = uv;
                if (_PortalRowsTopDown == 1) guestUv.y = 1.0 - guestUv.y;

                // --- reproject: reconstruct the guest world pos for this guest pixel, project to now ---
                float gd = tex2D(_PortalDepth, guestUv).r;
                float gEye = GuestEyeDepth(gd);

                // guest clip -> world via inverse VP (use the raw guest depth as NDC z)
                float ndcZ = (_PortalDepthConvention == 1) ? gd : (gd * 2.0 - 1.0);
                float4 gClip = float4(guestUv * 2.0 - 1.0, ndcZ, 1.0);
                float4 gWorld = mul(_PortalInvVP, gClip);
                gWorld /= gWorld.w;

                // world -> current clip
                float4 cClip = mul(_CurrentVP, gWorld);
                float2 cUv = (cClip.xy / cClip.w) * 0.5 + 0.5;

                // overlay colour sampled at the guest pixel (colour travels with the pixel)
                fixed4 col = tex2D(_PortalColor, guestUv);

                // --- occlusion test against TABG scene depth at the reprojected screen pos ---
                float sceneRaw = SAMPLE_DEPTH_TEXTURE(_CameraDepthTexture, cUv);
                float sceneEye = LinearEyeDepth(sceneRaw); // uses _ZBufferParams (TABG reversed-Z)

                // reprojected guest eye depth in TABG camera space (approx: current clip w)
                float reprojEye = cClip.w;
                if (reprojEye <= 0.0) return fixed4(0,0,0,0);

                // hide overlay where TABG geometry is in front
                float occl = step(reprojEye, sceneEye + 0.02);
                col.a *= occl;

                // drop pixels that reproject off-screen
                if (cUv.x < 0 || cUv.x > 1 || cUv.y < 0 || cUv.y > 1) col.a = 0;
                return col;
            }
            ENDCG
        }
    }
    Fallback Off
}
