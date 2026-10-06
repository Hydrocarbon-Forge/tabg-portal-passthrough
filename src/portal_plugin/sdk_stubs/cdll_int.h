// cdll_int.h — minimal shim of Source SDK 2013 public/cdll_int.h: the
// IVEngineClient (VEngineClient014) surface the plugin uses to drive the camera,
// plus the IBaseClientDLL (VClient017) handle we query to prove the client-side
// factory resolves. NOT the real SDK.
//
// Only the methods passthrough.cpp calls are declared. On Windows build against
// the real header (recon §2: VEngineClient014 @client.dll 0x3b6468,
// VClient017 @0x3b6420 confirmed present).
#ifndef PP_SDK_CDLL_INT_H
#define PP_SDK_CDLL_INT_H

#include "sdk_types.h"

class IVEngineClient {
public:
    // Screen dimensions of the (hidden) render window.
    virtual void GetScreenSize(int &width, int &height) = 0;
    // Queue a console command as if typed. ClientCmd is filtered (no cheats in
    // MP); ClientCmd_Unrestricted bypasses the filter — needed for setpos/setang
    // (recon §5). Portal is singleplayer_only so even ClientCmd would work, but
    // we use the unrestricted form to be explicit.
    virtual void ClientCmd(const char *szCmdString) = 0;
    virtual void ClientCmd_Unrestricted(const char *szCmdString) = 0;
    // View angle control (recon §5 camera-drive route B).
    virtual void GetViewAngles(QAngle &va) = 0;
    virtual void SetViewAngles(QAngle &va) = 0;
    // State guards so we only drive the camera when a map is live.
    virtual bool IsInGame() = 0;
    virtual bool IsConnected() = 0;
    virtual int GetMaxClients() = 0;
    virtual float Time() = 0;
    // Local player index (1-based) for setpos targeting.
    virtual int GetLocalPlayer() = 0;
};

// VClient017 — we only null-check it on load (recon §2 lists it as queryable
// from client.dll's own CreateInterface).
class IBaseClientDLL {
public:
    virtual int Init(CreateInterfaceFn appSystemFactory,
                     CreateInterfaceFn physicsFactory, void *pGlobals) = 0;
    virtual void Shutdown() = 0;
};

#endif // PP_SDK_CDLL_INT_H
