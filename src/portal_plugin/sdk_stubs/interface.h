// interface.h — minimal shim of Source SDK 2013 public/interface.h.
//
// This is NOT the real SDK. It declares only the factory typedef and the
// interface version strings that passthrough.cpp needs, so the plugin can be
// syntax-checked with g++ on a box that has no Source SDK. On the Windows build
// box, point the compiler at ValveSoftware/source-sdk-2013 public headers
// instead of this sdk_stubs dir (the version strings below match that SDK and
// the 2024 Portal rebuild per recon/portal-notes.md §2).
#ifndef PP_SDK_INTERFACE_H
#define PP_SDK_INTERFACE_H

// The engine hands every module this factory to fetch interfaces by name.
typedef void *(*CreateInterfaceFn)(const char *pName, int *pReturnCode);

// Interface version strings (recon/portal-notes.md §2, env.md). Verified against
// the client.dll/engine.dll strings in the 2024 rel_singleplayer Portal build.
#define INTERFACEVERSION_ISERVERPLUGINCALLBACKS "ISERVERPLUGINCALLBACKS003"
#define VENGINE_CLIENT_INTERFACE_VERSION        "VEngineClient014"
#define VCLIENT_INTERFACE_VERSION               "VClient017"
#define VENGINE_RENDERVIEW_INTERFACE_VERSION    "VEngineRenderView014"
#define MATERIAL_SYSTEM_INTERFACE_VERSION       "VMaterialSystem080"

#endif // PP_SDK_INTERFACE_H
