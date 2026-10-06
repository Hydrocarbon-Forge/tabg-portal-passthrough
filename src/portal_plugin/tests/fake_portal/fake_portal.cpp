// fake_portal.cpp — test target for the Source server plugin (slice C).
// Links passthrough.o + ws.o against a synthetic engine surface, simulating
// Source engine callbacks and rendering a deterministic synthetic scene.
//
// Proves the camera-drive + export path end to end without the real game:
//   host / pose_orbit.py -> ws link -> passthrough.cpp receives "cam" ->
//   DriveCamera (setpos/setang) -> synthetic scene render -> ExportFrame ->
//   writes frame channel -> reader validates pose + pixels.

#include "sdk_stubs/interface.h"
#include "sdk_stubs/sdk_types.h"
#include "sdk_stubs/eiface.h"
#include "sdk_stubs/cdll_int.h"
#include "sdk_stubs/ivrenderview.h"
#include "sdk_stubs/imaterialsystem.h"
#include "passthrough.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <chrono>
#include <thread>
#include <csignal>
#include <atomic>
#include <vector>

extern "C" {
void *CreateInterface(const char *pName, int *pReturnCode);
}

namespace {

static std::atomic<bool> g_stop{false};

void SignalHandler(int signum) {
    (void)signum;
    g_stop = true;
}

class FakeEngineClient : public IVEngineClient {
public:
    int screen_w = 1920;
    int screen_h = 1080;
    QAngle current_va;
    float cam_pos[3] = {0, 0, 0};
    float cam_ang[3] = {0, 0, 0};
    int setpos_count = 0;
    int setang_count = 0;

    virtual void GetScreenSize(int &width, int &height) override {
        width = screen_w;
        height = screen_h;
    }
    virtual void ClientCmd(const char *szCmdString) override {
        ClientCmd_Unrestricted(szCmdString);
    }
    virtual void ClientCmd_Unrestricted(const char *szCmdString) override {
        if (!szCmdString) return;
        if (strncmp(szCmdString, "setpos ", 7) == 0) {
            sscanf(szCmdString + 7, "%f %f %f", &cam_pos[0], &cam_pos[1], &cam_pos[2]);
            setpos_count++;
        } else if (strncmp(szCmdString, "setang ", 7) == 0) {
            sscanf(szCmdString + 7, "%f %f %f", &cam_ang[0], &cam_ang[1], &cam_ang[2]);
            setang_count++;
        }
    }
    virtual void GetViewAngles(QAngle &va) override {
        va = current_va;
    }
    virtual void SetViewAngles(QAngle &va) override {
        current_va = va;
    }
    virtual bool IsInGame() override { return true; }
    virtual bool IsConnected() override { return true; }
    virtual int GetMaxClients() override { return 1; }
    virtual float Time() override { return 1.0f; }
    virtual int GetLocalPlayer() override { return 1; }
};

class FakeRenderView : public IVRenderView {
public:
    float blend = 1.0f;
    virtual void SetBlend(float b) override { blend = b; }
    virtual float GetBlend() override { return blend; }
    virtual void SceneBegin() override {}
    virtual void SceneEnd() override {}
};

class FakeMaterial : public IMaterial {
public:
    virtual const char *GetName() const override { return "engine/depthwrite"; }
    virtual bool IsErrorMaterial() const override { return false; }
};

class FakeTexture : public ITexture {
public:
    virtual const char *GetName() const override { return "_rt_PassthroughDepth"; }
    virtual int GetActualWidth() const override { return 1920; }
    virtual int GetActualHeight() const override { return 1080; }
};

class FakeMatRenderContext : public IMatRenderContext {
public:
    FakeEngineClient *engine = nullptr;
    int current_frame = 0;
    std::vector<uint8_t> ramp_x;
    std::vector<uint8_t> ramp_y;
    std::vector<float> depth_ramp;

    void InitTables(int w, int h) {
        if ((int)ramp_x.size() == w && (int)ramp_y.size() == h) return;
        ramp_x.resize(w);
        for (int x = 0; x < w; ++x) ramp_x[x] = (uint8_t)(x * 255 / (w > 1 ? w - 1 : 1));
        ramp_y.resize(h);
        for (int y = 0; y < h; ++y) ramp_y[y] = (uint8_t)(y * 255 / (h > 1 ? h - 1 : 1));
        depth_ramp.resize(w);
        for (int x = 0; x < w; ++x) depth_ramp[x] = 1.0f - (float)x / (float)(w > 1 ? w : 1) * 0.95f;
    }

    virtual void BeginRender() override {}
    virtual void EndRender() override {}
    virtual void Flush(bool) override {}
    virtual void GetRenderTargetDimensions(int &w, int &h) const override { w = 1920; h = 1080; }
    virtual void PushRenderTargetAndViewport(ITexture *) override {}
    virtual void PopRenderTargetAndViewport() override {}
    virtual void Bind(IMaterial *, void *) override {}
    virtual void ClearColor4ub(unsigned char, unsigned char, unsigned char, unsigned char) override {}
    virtual void ClearBuffers(bool, bool, bool) override {}
    virtual void DrawScreenSpaceRectangle(IMaterial *, int, int, int, int, float, float, float, float, int, int, void *, int, int) override {}

    virtual void ReadPixels(int, int, int width, int height, unsigned char *data, ImageFormat dstFormat) override {
        if (!data || width <= 0 || height <= 0) return;
        InitTables(width, height);

        if (dstFormat == IMAGE_FORMAT_RGBA8888) {
            current_frame++;
            uint8_t b = (uint8_t)(current_frame & 0xFF);
            for (int y = 0; y < height; ++y) {
                uint8_t gy = ramp_y[y];
                uint8_t *row = data + (size_t)y * width * 4;
                for (int x = 0; x < width; ++x) {
                    row[x * 4 + 0] = ramp_x[x];
                    row[x * 4 + 1] = gy;
                    row[x * 4 + 2] = b;
                    row[x * 4 + 3] = 255;
                }
            }
        } else if (dstFormat == IMAGE_FORMAT_R32F) {
            float *depth = (float *)data;
            const float *dramp = depth_ramp.data();
            for (int y = 0; y < height; ++y) {
                memcpy(depth + (size_t)y * width, dramp, (size_t)width * sizeof(float));
            }
        }
    }
};

class FakeMaterialSystem : public IMaterialSystem {
public:
    FakeMatRenderContext renderContext;
    FakeMaterial depthMat;
    FakeTexture depthRT;

    virtual IMatRenderContext *GetRenderContext() override {
        return &renderContext;
    }
    virtual IMaterial *FindMaterial(const char *, const char *, bool, const char *) override {
        return &depthMat;
    }
    virtual ITexture *CreateNamedRenderTargetTextureEx2(
        const char *, int, int, RenderTargetSizeMode_t, ImageFormat, int, unsigned int, unsigned int) override {
        return &depthRT;
    }
    virtual void *GetD3DDevice() override { return nullptr; }
};

static FakeEngineClient g_engine;
static FakeRenderView g_renderView;
static FakeMaterialSystem g_matsys;

static void *FakeInterfaceFactory(const char *pName, int *pReturnCode) {
    if (pReturnCode) *pReturnCode = 0;
    if (strcmp(pName, VENGINE_CLIENT_INTERFACE_VERSION) == 0)
        return (IVEngineClient *)&g_engine;
    if (strcmp(pName, VENGINE_RENDERVIEW_INTERFACE_VERSION) == 0)
        return (IVRenderView *)&g_renderView;
    if (strcmp(pName, MATERIAL_SYSTEM_INTERFACE_VERSION) == 0)
        return (IMaterialSystem *)&g_matsys;
    if (pReturnCode) *pReturnCode = 1;
    return nullptr;
}

} // namespace

int main(int argc, char **argv) {
    float max_seconds = 0.0f;
    int max_frames = 0;
    bool verbose = false;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) {
            max_seconds = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            max_frames = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--verbose") == 0) {
            verbose = true;
        }
    }

    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    g_matsys.renderContext.engine = &g_engine;

    int rc = 0;
    IServerPluginCallbacks *plugin = (IServerPluginCallbacks *)CreateInterface(
        INTERFACEVERSION_ISERVERPLUGINCALLBACKS, &rc);
    if (!plugin) {
        fprintf(stderr, "[fake_portal] ERROR: CreateInterface failed (rc=%d)\n", rc);
        return 1;
    }

    if (!plugin->Load(FakeInterfaceFactory, nullptr)) {
        fprintf(stderr, "[fake_portal] ERROR: plugin->Load failed\n");
        return 1;
    }

    plugin->LevelInit("fake_portal_chamber");
    printf("[fake_portal] running (PID=%d, max_sec=%.1f, max_frames=%d)...\n",
           getpid(), max_seconds, max_frames);
    fflush(stdout);

    auto start_time = std::chrono::steady_clock::now();
    int last_reported_frame = 0;

    while (!g_stop) {
        plugin->GameFrame(true);

        auto now = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - start_time).count();

        if (verbose && g_matsys.renderContext.current_frame != last_reported_frame) {
            last_reported_frame = g_matsys.renderContext.current_frame;
            if (last_reported_frame % 30 == 0) {
                printf("[fake_portal] frames=%d setpos=%d cam=(%.2f, %.2f, %.2f)\n",
                       last_reported_frame, g_engine.setpos_count,
                       g_engine.cam_pos[0], g_engine.cam_pos[1], g_engine.cam_pos[2]);
                fflush(stdout);
            }
        }

        if (max_seconds > 0.0f && elapsed >= max_seconds) break;
        if (max_frames > 0 && g_matsys.renderContext.current_frame >= max_frames) break;

        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    printf("[fake_portal] shutting down: exported_frames=%d setpos=%d setang=%d\n",
           g_matsys.renderContext.current_frame, g_engine.setpos_count, g_engine.setang_count);
    fflush(stdout);

    plugin->LevelShutdown();
    plugin->Unload();

    return 0;
}
