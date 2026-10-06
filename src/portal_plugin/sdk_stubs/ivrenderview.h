// ivrenderview.h — minimal shim of the IVRenderView (VEngineRenderView014)
// interface. NOT the real SDK.
//
// The plugin queries this interface (required by the task) and null-checks it;
// the depth second-pass (depth_method=renderpass) is driven through the
// material-system render context (see imaterialsystem.h) rather than this
// interface directly, because the full CViewSetup/IViewRender re-entry lives in
// client.dll and a production build would signature-hook it (recon §3, §2).
// A couple of representative methods are shimmed so the query site compiles.
#ifndef PP_SDK_IVRENDERVIEW_H
#define PP_SDK_IVRENDERVIEW_H

#include "sdk_types.h"

class IVRenderView {
public:
    // Global draw-translucency blend factor; harmless to read, used here only to
    // confirm the vtable resolved (recon §2 says verify interfaces on first load).
    virtual void SetBlend(float blend) = 0;
    virtual float GetBlend() = 0;
    virtual void SceneBegin() = 0;
    virtual void SceneEnd() = 0;
};

#endif // PP_SDK_IVRENDERVIEW_H
