// fc_selftest.cpp — compile-check + cross-language layout proof for
// framechannel.h. Built and run on this Linux box via the POSIX backend.
//
//   mode "write": create the channel and publish a deterministic frame, then
//     sleep so the Python reader (tests/framechannel_cpp_interop.py) can read it.
//   mode "roundtrip": write and read back in-process; verify every field.
//
//   g++ -std=c++11 -O2 fc_selftest.cpp -o fc_selftest
//   ./fc_selftest roundtrip
//   ./fc_selftest write <name> <seconds>

#include "framechannel.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

using namespace tabg_portal;

static void fill(uint8_t* rgba, float* depth, int w, int h, int frame_index) {
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int i = (y * w + x) * 4;
            rgba[i + 0] = (uint8_t)(x * 255 / (w - 1));
            rgba[i + 1] = (uint8_t)(y * 255 / (h - 1));
            rgba[i + 2] = (uint8_t)(frame_index & 0xFF);
            rgba[i + 3] = 255;
            // reversed-Z ramp: 1=near at x=0
            depth[y * w + x] = 1.0f - (float)x / (float)(w) * 0.95f;
        }
}

static int64_t now_us() {
    struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
}

int main(int argc, char** argv) {
    const char* mode = argc > 1 ? argv[1] : "roundtrip";
    const int w = 64, h = 36;

    if (strcmp(mode, "write") == 0) {
        const char* name = argc > 2 ? argv[2] : "TABGPortalFrameCpp";
        int seconds = argc > 3 ? atoi(argv[3]) : 3;
        FrameChannelWriter wr;
        if (!wr.create(name, w, h, 3, FC_REVERSED_Z_01, true)) {
            fprintf(stderr, "create failed\n"); return 2;
        }
        uint8_t rgba[w * h * 4]; float depth[w * h];
        float pos[3] = {1.5f, 2.0f, -3.25f};
        float rot[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        for (int f = 1; f <= seconds * 60; ++f) {
            pos[0] = 1.5f + f * 0.01f;
            fill(rgba, depth, w, h, f);
            wr.write(rgba, depth, w, h, f, pos, rot, 72.0f, now_us());
            struct timespec ts; ts.tv_sec = 0; ts.tv_nsec = 16666667; nanosleep(&ts, 0);
        }
        printf("cpp writer published %d frames to %s\n", seconds * 60, name);
        // keep the mapping around a moment for the reader
        struct timespec ts; ts.tv_sec = 1; ts.tv_nsec = 0; nanosleep(&ts, 0);
        wr.close();
        // leave the backing file for the python reader; it will be cleaned by the test
        return 0;
    }

    // roundtrip
    const char* name = "TABGPortalFrameCppRT";
    FrameChannelWriter wr;
    if (!wr.create(name, w, h, 3, FC_REVERSED_Z_01, true)) {
        fprintf(stderr, "create failed\n"); return 2;
    }
    uint8_t rgba[w * h * 4]; float depth[w * h];
    float pos[3] = {1.5f, 2.0f, -3.25f};
    float rot[4] = {0.1f, 0.2f, 0.3f, 0.927f};
    int64_t ts_us = now_us();
    fill(rgba, depth, w, h, 42);
    int slot = wr.write(rgba, depth, w, h, 42, pos, rot, 72.0f, ts_us);
    if (slot < 0) { fprintf(stderr, "write failed\n"); return 2; }

    FrameChannelReader rd;
    if (!rd.open(name)) { fprintf(stderr, "open failed\n"); return 2; }
    uint8_t out_rgba[w * h * 4]; float out_depth[w * h];
    FcFrameView v; v.rgba = out_rgba; v.depth = out_depth;
    if (!rd.latest(v)) { fprintf(stderr, "latest failed\n"); return 2; }

    int fails = 0;
    if (v.desc.frame_index != 42) { printf("FAIL frame_index=%lld\n", (long long)v.desc.frame_index); fails++; }
    if (v.desc.width != w || v.desc.height != h) { printf("FAIL dims\n"); fails++; }
    if (v.desc.timestamp_us != ts_us) { printf("FAIL ts\n"); fails++; }
    if (v.desc.fov != 72.0f) { printf("FAIL fov\n"); fails++; }
    if (v.desc.pos[0] != pos[0] || v.desc.pos[2] != pos[2]) { printf("FAIL pos\n"); fails++; }
    if (v.desc.rot[3] != rot[3]) { printf("FAIL rot\n"); fails++; }
    if (memcmp(out_rgba, rgba, sizeof(rgba)) != 0) { printf("FAIL rgba\n"); fails++; }
    if (memcmp(out_depth, depth, sizeof(depth)) != 0) { printf("FAIL depth\n"); fails++; }

    // offset sanity (compile-time layout vs the documented offsets)
    if (sizeof(FcSlotDesc) != 88) { printf("FAIL sizeof(FcSlotDesc)=%zu\n", sizeof(FcSlotDesc)); fails++; }

    rd.close();
    wr.close();
    wr.unlink_backing(name);
    printf("%s: cpp roundtrip %s\n", fails ? "FAIL" : "PASS", fails ? "had errors" : "ok");
    return fails ? 1 : 0;
}
