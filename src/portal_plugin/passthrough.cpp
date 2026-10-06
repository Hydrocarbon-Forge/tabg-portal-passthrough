// passthrough.cpp — implementation of the Portal guest server plugin. See
// passthrough.h for scope and the recon citations; camera.md for the
// camera-drive decision; README.md for the Windows build.
//
// Compiles on Linux (g++ -fsyntax-only) against sdk_stubs/ for a syntax check;
// the real behaviour only runs inside Portal on Windows. Everything that would
// touch the GPU is written against the SDK surface and guarded so it builds here.
#include "passthrough.h"
#include "sdk_stubs/d3d9_stub.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

namespace portal_passthrough {

// ---------------------------------------------------------------------------
// Tiny flat-JSON readers (no allocations, no exceptions) — same philosophy as
// the reference gta/src/script.cpp json_* helpers.
// ---------------------------------------------------------------------------
static const char *json_find(const char *m, const char *key) {
    char k[64];
    snprintf(k, sizeof(k), "\"%s\"", key);
    const char *at = strstr(m, k);
    if (!at) return 0;
    const char *p = at + strlen(k);
    while (*p == ' ' || *p == '\t') ++p;
    if (*p != ':') return 0;
    ++p;
    while (*p == ' ' || *p == '\t') ++p;
    return p;
}

static double json_num(const char *m, const char *key, double fallback) {
    const char *p = json_find(m, key);
    return p ? atof(p) : fallback;
}

static long long json_ll(const char *m, const char *key, long long fallback) {
    const char *p = json_find(m, key);
    return p ? atoll(p) : fallback;
}

// Read an N-float array that begins at the value of `key` (expects '[a,b,...]').
static int json_array(const char *m, const char *key, float *out, int n) {
    const char *p = json_find(m, key);
    if (!p || *p != '[') return 0;
    ++p;
    for (int i = 0; i < n; ++i) {
        while (*p == ' ' || *p == ',') ++p;
        if (*p == ']' || *p == 0) return i;
        out[i] = (float)atof(p);
        while (*p && *p != ',' && *p != ']') ++p;
    }
    return n;
}

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------
Config::Config() {
    strncpy(ws_host, "127.0.0.1", sizeof(ws_host));
    ws_host[sizeof(ws_host) - 1] = 0;
    ws_port = 25600;
    width = 1920;
    height = 1080;
    strncpy(channel_name, "TABGPortalFrame", sizeof(channel_name));
    channel_name[sizeof(channel_name) - 1] = 0;
    depth_method = DEPTH_RENDERPASS;
    near_z = 7.0f;      // Source CViewSetup defaults are ~3..7 near, far large
    far_z = 28000.0f;
}

void Config::LoadFile(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == ';') continue;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *key = line, *val = eq + 1;
        // trim trailing whitespace/newline on value
        char *end = val + strlen(val);
        while (end > val && (end[-1] == '\n' || end[-1] == '\r' ||
                             end[-1] == ' ' || end[-1] == '\t')) *--end = 0;
        if (strcmp(key, "ws_host") == 0) {
            strncpy(ws_host, val, sizeof(ws_host)); ws_host[sizeof(ws_host) - 1] = 0;
        } else if (strcmp(key, "ws_port") == 0) {
            ws_port = atoi(val);
        } else if (strcmp(key, "width") == 0) {
            width = atoi(val);
        } else if (strcmp(key, "height") == 0) {
            height = atoi(val);
        } else if (strcmp(key, "channel_name") == 0) {
            strncpy(channel_name, val, sizeof(channel_name)); channel_name[sizeof(channel_name) - 1] = 0;
        } else if (strcmp(key, "depth_method") == 0) {
            depth_method = (strcmp(val, "intz") == 0) ? DEPTH_INTZ : DEPTH_RENDERPASS;
        } else if (strcmp(key, "near_z") == 0) {
            near_z = (float)atof(val);
        } else if (strcmp(key, "far_z") == 0) {
            far_z = (float)atof(val);
        }
    }
    fclose(f);
}

// ---------------------------------------------------------------------------
// Coordinate mapping, host (Unity) -> Source (recon/portal-notes.md §4).
//   unity = (-src.y, src.z, src.x) * scale   =>   src = (uz, -ux, uy) / scale
// `scale` = 0.0254 m per Source unit (env.md UNIT_SCALE). cam.p is host metres.
// ---------------------------------------------------------------------------
static const float kMetresPerUnit = 0.0254f;
static const float kRad2Deg = 57.2957795f;

static void basis_to_quat(const float fwd[3], const float right[3],
                          const float up[3], float q[4]) {
    // Columns right=X, up=Y, fwd=Z (Unity left-handed). Matrix-to-quaternion.
    float m00 = right[0], m01 = up[0], m02 = fwd[0];
    float m10 = right[1], m11 = up[1], m12 = fwd[1];
    float m20 = right[2], m21 = up[2], m22 = fwd[2];
    float tr = m00 + m11 + m22;
    if (tr > 0.0f) {
        float s = sqrtf(tr + 1.0f) * 2.0f;
        q[3] = 0.25f * s;
        q[0] = (m21 - m12) / s;
        q[1] = (m02 - m20) / s;
        q[2] = (m10 - m01) / s;
    } else if (m00 > m11 && m00 > m22) {
        float s = sqrtf(1.0f + m00 - m11 - m22) * 2.0f;
        q[3] = (m21 - m12) / s; q[0] = 0.25f * s;
        q[1] = (m01 + m10) / s; q[2] = (m02 + m20) / s;
    } else if (m11 > m22) {
        float s = sqrtf(1.0f + m11 - m00 - m22) * 2.0f;
        q[3] = (m02 - m20) / s; q[0] = (m01 + m10) / s;
        q[1] = 0.25f * s;       q[2] = (m12 + m21) / s;
    } else {
        float s = sqrtf(1.0f + m22 - m00 - m11) * 2.0f;
        q[3] = (m10 - m01) / s; q[0] = (m02 + m20) / s;
        q[1] = (m12 + m21) / s; q[2] = 0.25f * s;
    }
}

// ---------------------------------------------------------------------------
// Plugin
// ---------------------------------------------------------------------------
PortalPassthroughPlugin::PortalPassthroughPlugin()
    : m_generation(-1), m_loaded(false), m_skyOff(false),
      m_engine(0), m_client(0), m_renderView(0), m_matsys(0),
      m_rgba(0), m_depth(0), m_cap_w(0), m_cap_h(0),
      m_depthRT(0), m_depthMat(0) {
    m_pending.valid = false;
}

PortalPassthroughPlugin::~PortalPassthroughPlugin() {
    if (m_rgba) { free(m_rgba); m_rgba = 0; }
    if (m_depth) { free(m_depth); m_depth = 0; }
}

const char *PortalPassthroughPlugin::GetPluginDescription() {
    return "TABGxPortal passthrough guest v1 (server plugin)";
}

bool PortalPassthroughPlugin::Load(CreateInterfaceFn interfaceFactory,
                                   CreateInterfaceFn gameServerFactory) {
    (void)gameServerFactory;
    m_cfg.LoadFile("portal_passthrough.cfg");

    // Query the engine-side interfaces (recon §2 version strings). Log any NULL:
    // the 2024 rebuild may have shifted a vtable (recon §2) — fail soft.
    if (interfaceFactory) {
        m_engine     = (IVEngineClient *)interfaceFactory(VENGINE_CLIENT_INTERFACE_VERSION, 0);
        m_renderView = (IVRenderView   *)interfaceFactory(VENGINE_RENDERVIEW_INTERFACE_VERSION, 0);
        m_matsys     = (IMaterialSystem*)interfaceFactory(MATERIAL_SYSTEM_INTERFACE_VERSION, 0);
    }
    // VClient017 lives in client.dll; its own CreateInterface is resolved from
    // the loaded module (recon §2). On Linux this returns 0 and we carry on.
    {
        CreateInterfaceFn clientFactory = 0;
#ifdef _WIN32
        HMODULE hClient = GetModuleHandleA("client.dll");
        if (!hClient) hClient = GetModuleHandleA("client");
        if (hClient)
            clientFactory = (CreateInterfaceFn)GetProcAddress(hClient, "CreateInterface");
#endif
        if (clientFactory)
            m_client = (IBaseClientDLL *)clientFactory(VCLIENT_INTERFACE_VERSION, 0);
    }

    if (!m_engine)  printf("[passthrough] WARN: %s is NULL\n", VENGINE_CLIENT_INTERFACE_VERSION);
    if (!m_matsys)  printf("[passthrough] WARN: %s is NULL\n", MATERIAL_SYSTEM_INTERFACE_VERSION);

    // Size the frame channel to the configured capacity, reversed-Z default
    // (task + framechannel.md: FC_REVERSED_Z_01, mapping name TABGPortalFrame).
    if (!m_fc.create(m_cfg.channel_name, m_cfg.width, m_cfg.height, 3,
                     tabg_portal::FC_REVERSED_Z_01, /*rows_top_down=*/true)) {
        printf("[passthrough] ERROR: could not create frame channel '%s'\n", m_cfg.channel_name);
    }
    EnsureBuffers(m_cfg.width, m_cfg.height);

    m_ws.start(m_cfg.ws_host, m_cfg.ws_port);
    m_loaded = true;
    printf("[passthrough] loaded; link -> ws://%s:%d guest, depth_method=%s\n",
           m_cfg.ws_host, m_cfg.ws_port,
           m_cfg.depth_method == Config::DEPTH_INTZ ? "intz" : "renderpass");
    return true;
}

void PortalPassthroughPlugin::Unload() {
    m_ws.stop();
    m_fc.close();
    m_loaded = false;
}

void PortalPassthroughPlugin::LevelInit(char const *pMapName) {
    (void)pMapName;
    m_skyOff = false;
    EnsureDepthTargets(m_cfg.width, m_cfg.height);
}

void PortalPassthroughPlugin::LevelShutdown() {
    m_skyOff = false;
}

void PortalPassthroughPlugin::EnsureBuffers(int w, int h) {
    if (w == m_cap_w && h == m_cap_h && m_rgba && m_depth) return;
    if (m_rgba) free(m_rgba);
    if (m_depth) free(m_depth);
    m_cap_w = w; m_cap_h = h;
    m_rgba = (uint8_t *)malloc((size_t)w * h * 4);
    m_depth = (float *)malloc((size_t)w * h * sizeof(float));
}

void PortalPassthroughPlugin::EnsureDepthTargets(int w, int h) {
    if (!m_matsys) return;
    if (!m_depthRT) {
        // R32F render target for the depth second-pass (recon §3 option 1).
        m_depthRT = m_matsys->CreateNamedRenderTargetTextureEx2(
            "_rt_PassthroughDepth", w, h, RT_SIZE_NO_CHANGE, IMAGE_FORMAT_R32F,
            /*depth=*/0, /*textureFlags=*/0,
            CREATERENDERTARGETFLAGS_UNFILTERABLE_OK);
    }
    if (!m_depthMat) {
        // Stock depth-write material; writes scaled scene depth (recon §3,
        // materials/engine/depthwrite.vmt present in hl2_misc VPK).
        m_depthMat = m_matsys->FindMaterial("engine/depthwrite", "Other", false, 0);
    }
}

void PortalPassthroughPlugin::SendHello() {
    char hello[256];
    snprintf(hello, sizeof(hello),
             "{\"t\":\"hello\",\"role\":\"guest\",\"ver\":1,"
             "\"name\":\"portal-plugin\",\"caps\":[\"cam\",\"event\",\"depth\"]}");
    m_ws.send(hello);
}

void PortalPassthroughPlugin::GameFrame(bool simulating) {
    if (!m_loaded) return;
    (void)simulating;

    // Resend our hello after every (re)connect, like the reference loop.
    if (m_ws.connected() && m_ws.generation() != m_generation) {
        m_generation = m_ws.generation();
        SendHello();
    }

    std::string msg;
    while (m_ws.poll(msg))
        HandleMessage(msg.c_str());
}

void PortalPassthroughPlugin::HandleMessage(const char *json) {
    const char *t = json_find(json, "t");
    if (!t || *t != '"') return;
    // "cam" is the only host->guest message that drives a render this slice;
    // "cmd" is forwarded straight to the console; others are ignored for now.
    if (strncmp(t, "\"cam\"", 5) == 0) {
        HostPose pose;
        pose.valid = true;
        float p[3] = {0, 0, 0};
        json_array(json, "p", p, 3);
        pose.pos[0] = p[0]; pose.pos[1] = p[1]; pose.pos[2] = p[2];
        pose.fov = (float)json_num(json, "fov", 60.0);
        pose.frame_index = json_ll(json, "f", 0);
        pose.ts_us = json_ll(json, "ts", 0);

        // Orientation: prefer an explicit basis, else a quaternion.
        float fwd[3] = {0, 0, 1}, right[3] = {1, 0, 0}, up[3] = {0, 1, 0};
        if (json_find(json, "fwd")) {
            json_array(json, "fwd", fwd, 3);
            json_array(json, "right", right, 3);
            json_array(json, "up", up, 3);
            basis_to_quat(fwd, right, up, pose.rot);
        } else if (json_find(json, "q")) {
            json_array(json, "q", pose.rot, 4);
            // derive forward from quaternion (host convention, Z-forward)
            float x = pose.rot[0], y = pose.rot[1], z = pose.rot[2], w = pose.rot[3];
            fwd[0] = 2 * (x * z + w * y);
            fwd[1] = 2 * (y * z - w * x);
            fwd[2] = 1 - 2 * (x * x + y * y);
        } else {
            pose.rot[0] = pose.rot[1] = pose.rot[2] = 0; pose.rot[3] = 1;
        }

        // Host -> Source position and angles (recon §4).
        pose.src_origin[0] = p[2] / kMetresPerUnit;
        pose.src_origin[1] = -p[0] / kMetresPerUnit;
        pose.src_origin[2] = p[1] / kMetresPerUnit;
        float sfx = fwd[2], sfy = -fwd[0], sfz = fwd[1];
        float yaw = atan2f(sfy, sfx) * kRad2Deg;
        float fz = sfz; if (fz > 1) fz = 1; if (fz < -1) fz = -1;
        float pitch = -asinf(fz) * kRad2Deg;
        pose.src_ang[0] = pitch; pose.src_ang[1] = yaw; pose.src_ang[2] = 0;

        DriveCamera(pose);
        m_pending = pose;
        ExportFrame(pose);

        // Echo immediately so the host can measure round-trip (protocol.md).
        char echo[96];
        snprintf(echo, sizeof(echo), "{\"t\":\"echo\",\"f\":%lld,\"ts\":%lld}",
                 (long long)pose.frame_index, (long long)pose.ts_us);
        m_ws.send(echo);
    } else if (strncmp(t, "\"cmd\"", 5) == 0) {
        const char *c = json_find(json, "c");
        if (c && *c == '"' && m_engine) {
            char cmd[256];
            const char *e = strchr(c + 1, '"');
            size_t n = e ? (size_t)(e - (c + 1)) : 0;
            if (n >= sizeof(cmd)) n = sizeof(cmd) - 1;
            memcpy(cmd, c + 1, n); cmd[n] = 0;
            m_engine->ClientCmd_Unrestricted(cmd);
        }
    }
}

// Camera drive — route B (recon §5, see camera.md): teleport the view with
// setpos/setang via ClientCmd_Unrestricted and lock the view angles with
// SetViewAngles. Skybox off per task (r_drawskybox 0), issued once per level.
void PortalPassthroughPlugin::DriveCamera(const HostPose &pose) {
    if (!m_engine) return;
    if (!m_engine->IsInGame()) return;

    if (!m_skyOff) {
        m_engine->ClientCmd_Unrestricted("r_drawskybox 0");
        m_engine->ClientCmd_Unrestricted("sv_cheats 1");  // setpos needs cheats
        m_engine->ClientCmd_Unrestricted("noclip");       // free the view to teleport
        m_skyOff = true;
    }

    char cmd[160];
    snprintf(cmd, sizeof(cmd), "setpos %.4f %.4f %.4f",
             pose.src_origin[0], pose.src_origin[1], pose.src_origin[2]);
    m_engine->ClientCmd_Unrestricted(cmd);
    snprintf(cmd, sizeof(cmd), "setang %.4f %.4f %.4f",
             pose.src_ang[0], pose.src_ang[1], pose.src_ang[2]);
    m_engine->ClientCmd_Unrestricted(cmd);

    QAngle va(pose.src_ang[0], pose.src_ang[1], pose.src_ang[2]);
    m_engine->SetViewAngles(va);
}

bool PortalPassthroughPlugin::CaptureColor(int w, int h) {
    if (!m_matsys || !m_rgba) return false;
    IMatRenderContext *rc = m_matsys->GetRenderContext();
    if (!rc) return false;
    // Read the primary target (the just-rendered backbuffer) as RGBA8.
    rc->ReadPixels(0, 0, w, h, m_rgba, IMAGE_FORMAT_RGBA8888);
    return true;
}

// Depth export route A (preferred): second depth pass into our R32F RT with the
// depth-write material, then read it back (recon §3 option 1).
bool PortalPassthroughPlugin::CaptureDepthRenderPass(int w, int h) {
    if (!m_matsys || !m_depth) return false;
    IMatRenderContext *rc = m_matsys->GetRenderContext();
    if (!rc || !m_depthRT || !m_depthMat) return false;
    rc->PushRenderTargetAndViewport(m_depthRT);
    rc->ClearColor4ub(0, 0, 0, 0);
    rc->ClearBuffers(true, false);
    rc->Bind(m_depthMat, 0);
    rc->DrawScreenSpaceRectangle(m_depthMat, 0, 0, w, h,
                                 0, 0, (float)w, (float)h, w, h, 0, 1, 1);
    rc->ReadPixels(0, 0, w, h, (unsigned char *)m_depth, IMAGE_FORMAT_R32F);
    rc->PopRenderTargetAndViewport();
    return true;
}

// Depth export route B (fallback): D3D9 INTZ readback (recon §3 option 3). All
// D3D9 calls compile against sdk_stubs/d3d9_stub.h here; they only run on the
// real game's D3D9 device.
bool PortalPassthroughPlugin::CaptureDepthINTZ(int w, int h) {
    if (!m_matsys || !m_depth) return false;
    IDirect3DDevice9 *dev = (IDirect3DDevice9 *)m_matsys->GetD3DDevice();
    if (!dev) return false;

    // An INTZ depth texture would be bound in place of the engine's depth
    // surface at device-reset time (invasive — see recon §3). Here we lock its
    // top surface and copy the R32F-equivalent depth out.
    IDirect3DTexture9 *intz = 0;
    const D3DFORMAT INTZ = (D3DFORMAT)MAKEFOURCC('I', 'N', 'T', 'Z');
    if (FAILED(dev->CreateTexture((UINT)w, (UINT)h, 1, D3DUSAGE_DEPTHSTENCIL,
                                  INTZ, D3DPOOL_DEFAULT, &intz, 0)) || !intz)
        return false;
    IDirect3DSurface9 *surf = 0;
    if (FAILED(intz->GetSurfaceLevel(0, &surf)) || !surf) {
        intz->Release();
        return false;
    }
    D3DLOCKED_RECT lr;
    bool ok = false;
    if (SUCCEEDED(surf->LockRect(&lr, 0, D3DLOCK_READONLY))) {
        const uint8_t *src = (const uint8_t *)lr.pBits;
        for (int y = 0; y < h; ++y) {
            const float *row = (const float *)(src + (size_t)y * lr.Pitch);
            memcpy(m_depth + (size_t)y * w, row, (size_t)w * sizeof(float));
        }
        surf->UnlockRect();
        ok = true;
    }
    surf->Release();
    intz->Release();
    return ok;
}

bool PortalPassthroughPlugin::ExportFrame(const HostPose &pose) {
    const int w = m_cfg.width, h = m_cfg.height;
    EnsureBuffers(w, h);
    bool cok = CaptureColor(w, h);
    bool dok = (m_cfg.depth_method == Config::DEPTH_INTZ)
                   ? CaptureDepthINTZ(w, h)
                   : CaptureDepthRenderPass(w, h);
    if (!cok || !dok) {
        // On this box (no GPU) these return false; the shipped plugin publishes
        // only complete frames. Still return cleanly so the loop keeps running.
        return false;
    }
    int64_t ts = (int64_t)time(0) * 1000000LL; // µs; QPC on the real build
    int slot = m_fc.write(m_rgba, m_depth, w, h, pose.frame_index,
                          pose.pos, pose.rot, pose.fov, ts);
    return slot >= 0;
}

} // namespace portal_passthrough

// ---------------------------------------------------------------------------
// Plugin export. Source's engine calls CreateInterface(ISERVERPLUGINCALLBACKS003)
// on the dll named by addons/passthrough.vdf. One global instance, as the stock
// serverplugin_empty does (recon §2).
// ---------------------------------------------------------------------------
static portal_passthrough::PortalPassthroughPlugin g_portalPlugin;

extern "C"
#ifdef _WIN32
__declspec(dllexport)
#else
__attribute__((visibility("default")))
#endif
void *CreateInterface(const char *pName, int *pReturnCode) {
    if (pName && strcmp(pName, INTERFACEVERSION_ISERVERPLUGINCALLBACKS) == 0) {
        if (pReturnCode) *pReturnCode = 0; // IFACE_OK
        return (IServerPluginCallbacks *)&g_portalPlugin;
    }
    if (pReturnCode) *pReturnCode = 1;     // IFACE_FAILED
    return 0;
}
