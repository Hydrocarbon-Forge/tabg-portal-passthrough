// passthrough.h — TABG×Portal passthrough GUEST: a Source 2013 server plugin
// (ISERVERPLUGINCALLBACKS003) that connects to the passthrough hub as role
// "guest", drives the Portal camera to the host's pose, renders with the skybox
// off, and exports world colour + depth to the shared-memory frame channel.
//
// Scope / evidence:
//   * Server plugin, NOT a replaced client.dll — recon/portal-notes.md §2.
//   * Interfaces queried on Load: VEngineClient014, VClient017,
//     VEngineRenderView014, VMaterialSystem080 — recon §2 / env.md.
//   * Camera drive route: ClientCmd_Unrestricted setpos/setang + SetViewAngles
//     (recon §5). Rationale + rejected alternatives in camera.md.
//   * Depth export: two routes behind cfg.depth_method — "renderpass" (second
//     depth pass via the material-system render context, recon §3 option 1,
//     preferred) and "intz" (D3D9 INTZ readback, recon §3 option 3, fallback).
//
// This header + passthrough.cpp compile on Linux against sdk_stubs/ (syntax
// check only, no game). On Windows build against source-sdk-2013 public headers.
#ifndef PP_PASSTHROUGH_H
#define PP_PASSTHROUGH_H

#include "sdk_stubs/interface.h"
#include "sdk_stubs/sdk_types.h"
#include "sdk_stubs/eiface.h"
#include "sdk_stubs/cdll_int.h"
#include "sdk_stubs/ivrenderview.h"
#include "sdk_stubs/imaterialsystem.h"

#include "ws.h"
#include "framechannel.h"  // src/framechannel/cpp/framechannel.h (added via -I)

#include <stdint.h>

namespace portal_passthrough {

// Runtime config, read from portal_passthrough.cfg next to the game exe
// (key=value lines). All fields have safe defaults.
struct Config {
    enum DepthMethod { DEPTH_RENDERPASS = 0, DEPTH_INTZ = 1 };
    char ws_host[64];
    int  ws_port;
    int  width;
    int  height;
    char channel_name[64];
    DepthMethod depth_method;
    float near_z;   // Source CViewSetup near plane (units) for depth linearise
    float far_z;
    Config();
    void LoadFile(const char *path);
};

// The pose the guest renders a frame with, stored in the host's convention so
// the Unity host can reproject (see framechannel.md slot descriptor).
struct HostPose {
    float pos[3];      // host-convention camera position (snapshot for reprojection)
    float rot[4];      // quaternion x,y,z,w (host convention)
    float fov;
    float src_origin[3]; // Source world origin the camera is driven to (units)
    float src_ang[3];    // Source QAngle pitch,yaw,roll the camera is driven to
    int64_t frame_index;
    int64_t ts_us;     // host cam timestamp, echoed back for latency
    bool valid;
};

class PortalPassthroughPlugin : public IServerPluginCallbacks {
public:
    PortalPassthroughPlugin();
    virtual ~PortalPassthroughPlugin();

    // --- IServerPluginCallbacks (003) ---
    virtual bool Load(CreateInterfaceFn interfaceFactory,
                      CreateInterfaceFn gameServerFactory);
    virtual void Unload();
    virtual void Pause() {}
    virtual void UnPause() {}
    virtual const char *GetPluginDescription();
    virtual void LevelInit(char const *pMapName);
    virtual void ServerActivate(edict_t *, int, int) {}
    virtual void GameFrame(bool simulating);
    virtual void LevelShutdown();
    virtual void ClientActive(edict_t *) {}
    virtual void ClientDisconnect(edict_t *) {}
    virtual void ClientPutInServer(edict_t *, char const *) {}
    virtual void SetCommandClient(int) {}
    virtual void ClientSettingsChanged(edict_t *) {}
    virtual PLUGIN_RESULT ClientConnect(bool *, edict_t *, const char *,
                                        const char *, char *, int) { return PLUGIN_CONTINUE; }
    virtual PLUGIN_RESULT ClientCommand(edict_t *, const CCommand &) { return PLUGIN_CONTINUE; }
    virtual PLUGIN_RESULT NetworkIDValidated(const char *, const char *) { return PLUGIN_CONTINUE; }
    virtual void OnQueryCvarValueFinished(QueryCvarCookie_t, edict_t *,
                                          EQueryCvarValueStatus, const char *,
                                          const char *) {}
    virtual void OnEdictAllocated(edict_t *) {}
    virtual void OnEdictFreed(const edict_t *) {}

private:
    void SendHello();
    void HandleMessage(const char *json);
    void DriveCamera(const HostPose &pose);         // camera.md route B
    bool ExportFrame(const HostPose &pose);         // colour + depth -> channel
    bool CaptureColor(int w, int h);
    bool CaptureDepthRenderPass(int w, int h);      // recon §3 option 1
    bool CaptureDepthINTZ(int w, int h);            // recon §3 option 3
    void EnsureBuffers(int w, int h);
    void EnsureDepthTargets(int w, int h);

    Config m_cfg;
    WsClient m_ws;
    int m_generation;
    bool m_loaded;
    bool m_skyOff;

    HostPose m_pending;   // latest cam not yet exported

    // Engine interfaces (owned by the engine; we only hold pointers).
    IVEngineClient   *m_engine;
    IBaseClientDLL   *m_client;
    IVRenderView     *m_renderView;
    IMaterialSystem  *m_matsys;

    // Export scratch (sized to cfg.width*height).
    tabg_portal::FrameChannelWriter m_fc;
    uint8_t *m_rgba;
    float   *m_depth;
    int      m_cap_w, m_cap_h;

    // renderpass depth target + material.
    ITexture *m_depthRT;
    IMaterial *m_depthMat;
};

} // namespace portal_passthrough

#endif // PP_PASSTHROUGH_H
