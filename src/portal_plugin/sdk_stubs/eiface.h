// eiface.h — minimal shim of Source SDK 2013 public/eiface.h: just the
// IServerPluginCallbacks (ISERVERPLUGINCALLBACKS003) abstract base the plugin
// implements, plus the enums its method signatures use. NOT the real SDK.
//
// The method set and order below is the 003 vtable from source-sdk-2013
// (public/eiface.h). On the Windows build box use the real header so the vtable
// matches the engine exactly (recon §2 warns the 2024 rebuild may have appended
// methods — call only these documented ones and log any NULL interface).
#ifndef PP_SDK_EIFACE_H
#define PP_SDK_EIFACE_H

#include "interface.h"
#include "sdk_types.h"

typedef int QueryCvarCookie_t;

enum EQueryCvarValueStatus {
    eQueryCvarValueStatus_ValueIntact = 0,
    eQueryCvarValueStatus_CvarNotFound = 1,
    eQueryCvarValueStatus_NotACvar = 2,
    eQueryCvarValueStatus_CvarProtected = 3
};

typedef enum {
    PLUGIN_CONTINUE = 0,  // keep going, let the game handle it too
    PLUGIN_OVERRIDE,      // run the game code, but use our return value
    PLUGIN_STOP           // don't run the game code at all
} PLUGIN_RESULT;

// ISERVERPLUGINCALLBACKS003
class IServerPluginCallbacks {
public:
    virtual bool Load(CreateInterfaceFn interfaceFactory,
                      CreateInterfaceFn gameServerFactory) = 0;
    virtual void Unload() = 0;
    virtual void Pause() = 0;
    virtual void UnPause() = 0;
    virtual const char *GetPluginDescription() = 0;
    virtual void LevelInit(char const *pMapName) = 0;
    virtual void ServerActivate(edict_t *pEdictList, int edictCount, int clientMax) = 0;
    virtual void GameFrame(bool simulating) = 0;
    virtual void LevelShutdown() = 0;
    virtual void ClientActive(edict_t *pEntity) = 0;
    virtual void ClientDisconnect(edict_t *pEntity) = 0;
    virtual void ClientPutInServer(edict_t *pEntity, char const *playername) = 0;
    virtual void SetCommandClient(int index) = 0;
    virtual void ClientSettingsChanged(edict_t *pEdict) = 0;
    virtual PLUGIN_RESULT ClientConnect(bool *bAllowConnect, edict_t *pEntity,
                                        const char *pszName, const char *pszAddress,
                                        char *reject, int maxrejectlen) = 0;
    virtual PLUGIN_RESULT ClientCommand(edict_t *pEntity, const CCommand &args) = 0;
    virtual PLUGIN_RESULT NetworkIDValidated(const char *pszUserName,
                                             const char *pszNetworkID) = 0;
    virtual void OnQueryCvarValueFinished(QueryCvarCookie_t iCookie,
                                          edict_t *pPlayerEntity,
                                          EQueryCvarValueStatus eStatus,
                                          const char *pCvarName,
                                          const char *pCvarValue) = 0;
    virtual void OnEdictAllocated(edict_t *edict) = 0;
    virtual void OnEdictFreed(const edict_t *edict) = 0;
};

#endif // PP_SDK_EIFACE_H
