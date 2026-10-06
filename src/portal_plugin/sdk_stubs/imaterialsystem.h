// imaterialsystem.h — minimal shim of IMaterialSystem (VMaterialSystem080) and
// the handful of IMatRenderContext / ITexture / IMaterial methods the colour +
// depth export paths call. NOT the real SDK.
//
// depth_method=renderpass uses this surface: create an R32F render target, bind
// a depth-to-colour material (stock engine/depthwrite.vmt — recon §3), draw a
// screen-space pass, then ReadPixels the RT back to system memory. Colour is
// read back from the primary target the same way. On Windows build against the
// real materialsystem/imaterialsystem.h; the signatures below are deliberately
// conservative and documented so the swap is mechanical.
#ifndef PP_SDK_IMATERIALSYSTEM_H
#define PP_SDK_IMATERIALSYSTEM_H

#include "sdk_types.h"

// Render-target size modes (materialsystem/imaterialsystem.h RenderTargetSizeMode_t).
enum RenderTargetSizeMode_t {
    RT_SIZE_NO_CHANGE = 0,
    RT_SIZE_DEFAULT = 1,
    RT_SIZE_PICMIP = 2,
    RT_SIZE_HDR = 3,
    RT_SIZE_FULL_FRAME_BUFFER = 4,
    RT_SIZE_OFFSCREEN = 5,
    RT_SIZE_FULL_FRAME_BUFFER_ROUNDED_UP = 6
};

// Render-target creation flags subset (materialsystem/materialsystem.h).
enum CreateRenderTargetFlags {
    CREATERENDERTARGETFLAGS_HDR = 0x00000001,
    CREATERENDERTARGETFLAGS_AUTOMIPMAP = 0x00000002,
    CREATERENDERTARGETFLAGS_UNFILTERABLE_OK = 0x00000004
};

class IMaterial {
public:
    virtual const char *GetName() const = 0;
    virtual bool IsErrorMaterial() const = 0;
};

class ITexture {
public:
    virtual const char *GetName() const = 0;
    virtual int GetActualWidth() const = 0;
    virtual int GetActualHeight() const = 0;
};

class IMatRenderContext {
public:
    virtual void BeginRender() = 0;
    virtual void EndRender() = 0;
    virtual void Flush(bool flushHardware = false) = 0;
    virtual void GetRenderTargetDimensions(int &width, int &height) const = 0;
    // Push/pop a render target + full viewport (RT = NULL means the back buffer).
    virtual void PushRenderTargetAndViewport(ITexture *pTexture) = 0;
    virtual void PopRenderTargetAndViewport() = 0;
    virtual void Bind(IMaterial *material, void *proxyData = 0) = 0;
    virtual void ClearColor4ub(unsigned char r, unsigned char g,
                               unsigned char b, unsigned char a) = 0;
    virtual void ClearBuffers(bool clearColor, bool clearDepth,
                              bool clearStencil = false) = 0;
    // Full-screen quad with the bound material (used to splat depth into the RT).
    virtual void DrawScreenSpaceRectangle(IMaterial *material,
                                          int destx, int desty, int width, int height,
                                          float srcTextureX0, float srcTextureY0,
                                          float srcTextureX1, float srcTextureY1,
                                          int srcTextureWidth, int srcTextureHeight,
                                          void *clientRenderable = 0,
                                          int xDice = 1, int yDice = 1) = 0;
    // Copy the current (or given) RT into system memory in dstFormat.
    virtual void ReadPixels(int x, int y, int width, int height,
                            unsigned char *data, ImageFormat dstFormat) = 0;
};

class IMaterialSystem {
public:
    virtual IMatRenderContext *GetRenderContext() = 0;
    virtual IMaterial *FindMaterial(const char *materialName, const char *textureGroupName,
                                    bool complain = true,
                                    const char *complainPrefix = 0) = 0;
    virtual ITexture *CreateNamedRenderTargetTextureEx2(
        const char *pRTName, int w, int h, RenderTargetSizeMode_t sizeMode,
        ImageFormat format, int /*MaterialRenderTargetDepth_t*/ depth,
        unsigned int textureFlags, unsigned int renderTargetFlags) = 0;
    // D3D9 escape hatch used by depth_method=intz; see d3d9_capture.h.
    virtual void *GetD3DDevice() = 0;
};

#endif // PP_SDK_IMATERIALSYSTEM_H
