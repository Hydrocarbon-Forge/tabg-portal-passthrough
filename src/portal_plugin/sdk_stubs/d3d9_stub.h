// d3d9_stub.h — on Windows, pull in the real <d3d9.h>; on this Linux box, define
// just enough of the D3D9 surface types that the INTZ depth-capture fallback
// (depth_method=intz) references, so passthrough.cpp syntax-checks with g++.
//
// The INTZ path is the recon §3 option 3 fallback: grab the backbuffer colour
// with GetRenderTargetData into a D3DPOOL_SYSTEMMEM surface, and read depth from
// an INTZ depth texture. All of it compiles here but only runs on the real game.
#ifndef PP_SDK_D3D9_STUB_H
#define PP_SDK_D3D9_STUB_H

#ifdef _WIN32
#  include <d3d9.h>
#else
// ---- Linux syntax-check shims (NOT a D3D implementation) -------------------
#include <stdint.h>

typedef long HRESULT;
typedef unsigned int UINT;
typedef unsigned long DWORD;
typedef int BOOL;
typedef void *HWND;

#ifndef S_OK
#  define S_OK ((HRESULT)0)
#endif
#ifndef D3D_OK
#  define D3D_OK ((HRESULT)0)
#endif
#define SUCCEEDED(hr) ((HRESULT)(hr) >= 0)
#define FAILED(hr)    ((HRESULT)(hr) < 0)

// FourCC for the NVIDIA INTZ readable-depth format (recon §3: build supports NV_INTZ).
#ifndef MAKEFOURCC
#  define MAKEFOURCC(a, b, c, d) \
      ((DWORD)(uint8_t)(a) | ((DWORD)(uint8_t)(b) << 8) | \
       ((DWORD)(uint8_t)(c) << 16) | ((DWORD)(uint8_t)(d) << 24))
#endif

typedef enum _D3DFORMAT {
    D3DFMT_UNKNOWN = 0,
    D3DFMT_A8R8G8B8 = 21,
    D3DFMT_X8R8G8B8 = 22,
    D3DFMT_INTZ = 0 // placeholder; real value is MAKEFOURCC('I','N','T','Z')
} D3DFORMAT;

typedef enum _D3DPOOL {
    D3DPOOL_DEFAULT = 0,
    D3DPOOL_MANAGED = 1,
    D3DPOOL_SYSTEMMEM = 2,
    D3DPOOL_SCRATCH = 3
} D3DPOOL;

typedef enum _D3DRESOURCETYPE { D3DRTYPE_SURFACE = 1, D3DRTYPE_TEXTURE = 3 } D3DRESOURCETYPE;

typedef struct _D3DLOCKED_RECT {
    int Pitch;
    void *pBits;
} D3DLOCKED_RECT;

typedef struct _RECT {
    long left, top, right, bottom;
} RECT;

typedef struct _D3DSURFACE_DESC {
    D3DFORMAT Format;
    D3DRESOURCETYPE Type;
    DWORD Usage;
    D3DPOOL Pool;
    UINT Width;
    UINT Height;
} D3DSURFACE_DESC;

#define D3DLOCK_READONLY 0x00000010L
#define D3DUSAGE_DEPTHSTENCIL 0x00000002L

struct IDirect3DSurface9 {
    virtual HRESULT LockRect(D3DLOCKED_RECT *pLockedRect, const RECT *pRect, DWORD Flags) = 0;
    virtual HRESULT UnlockRect() = 0;
    virtual HRESULT GetDesc(D3DSURFACE_DESC *pDesc) = 0;
    virtual unsigned long Release() = 0;
};

struct IDirect3DTexture9 {
    virtual HRESULT GetSurfaceLevel(UINT Level, IDirect3DSurface9 **ppSurfaceLevel) = 0;
    virtual unsigned long Release() = 0;
};

struct IDirect3DDevice9 {
    virtual HRESULT GetRenderTarget(DWORD RenderTargetIndex, IDirect3DSurface9 **ppRenderTarget) = 0;
    virtual HRESULT CreateOffscreenPlainSurface(UINT Width, UINT Height, D3DFORMAT Format,
                                                D3DPOOL Pool, IDirect3DSurface9 **ppSurface,
                                                void *pSharedHandle) = 0;
    virtual HRESULT GetRenderTargetData(IDirect3DSurface9 *pRenderTarget,
                                        IDirect3DSurface9 *pDestSurface) = 0;
    virtual HRESULT CreateTexture(UINT Width, UINT Height, UINT Levels, DWORD Usage,
                                  D3DFORMAT Format, D3DPOOL Pool,
                                  IDirect3DTexture9 **ppTexture, void *pSharedHandle) = 0;
    virtual unsigned long Release() = 0;
};

#endif // _WIN32
#endif // PP_SDK_D3D9_STUB_H
