// framechannel.h — C/C++ writer+reader for the passthrough shared-memory frame
// channel (slice 3). Byte layout is identical to src/framechannel/framechannel.py
// and documented in src/framechannel/framechannel.md.
//
// Target: the 32-bit Source server plugin (guest) on Windows is the WRITER; the
// host (and tests) read. The header is header-only and:
//   * x86- and x64-safe: all on-wire fields are fixed-width and the structs are
//     #pragma pack(1), so offsets match the Python struct regardless of arch;
//   * exception-free: every entry point returns a bool/int, never throws, so
//     nothing unwinds across the plugin boundary;
//   * CRT-safe: no global constructors, no STL containers, no static init — safe
//     to compile into a static lib and load into Source's CRT.
//
// A POSIX backend (shm as a /dev/shm file, matching framechannel.py) is provided
// under #ifndef _WIN32 purely so the layout/logic can be built and cross-checked
// against the Python side on Linux. The shipped target is the Win32 path.

#ifndef TABG_PORTAL_FRAMECHANNEL_H
#define TABG_PORTAL_FRAMECHANNEL_H

#include <stdint.h>
#include <string.h>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <fcntl.h>
#  include <sys/mman.h>
#  include <sys/stat.h>
#  include <unistd.h>
#  include <stdio.h>
#  include <time.h>
#endif

namespace tabg_portal {

// ---- constants (must match framechannel.py) --------------------------------
static const int32_t FC_MAGIC        = 0x31435054; // "TPC1"
static const int32_t FC_VERSION      = 1;
static const int32_t FC_HEADER_BYTES = 4096;
static const int32_t FC_DESC_OFF     = 256;
static const int32_t FC_DESC_STRIDE  = 128;
static const int32_t FC_ALIGN        = 64;

// header flags
static const int32_t FC_ROWS_TOP_DOWN = 1;

// depth conventions
enum FcDepthConvention {
    FC_LINEAR01      = 0,
    FC_REVERSED_Z_01 = 1,
    FC_ZBUF_01       = 2,
    FC_GL_NDC        = 3,
    FC_EYE_METRES    = 4
};

#pragma pack(push, 1)
struct FcHeader {
    int32_t magic;             // 0
    int32_t version;           // 4
    int32_t header_size;       // 8
    int32_t slot_count;        // 12
    int64_t slot_stride;       // 16
    int32_t max_width;         // 24
    int32_t max_height;        // 28
    int32_t depth_convention;  // 32
    int64_t published;         // 36
    int32_t latest_slot;       // 44
    int32_t flags;             // 48
};                             // 52 bytes

struct FcSlotDesc {
    int64_t seq;               // 0  seqlock (odd = write in progress)
    int64_t frame_index;       // 8
    int64_t timestamp_us;      // 16
    int32_t width;             // 24
    int32_t height;            // 28
    float   pos[3];            // 32
    float   rot[4];            // 44 (x,y,z,w)
    float   fov;               // 60
    int32_t depth_convention;  // 64
    int32_t payload_size;      // 68
    int64_t capture_ns;        // 72
    int64_t publish_ns;        // 80
};                             // 88 bytes
#pragma pack(pop)

// ---- helpers ---------------------------------------------------------------
static inline int64_t fc_align(int64_t n, int64_t a) {
    return (n + a - 1) / a * a;
}
static inline int64_t fc_slot_stride(int32_t max_w, int32_t max_h) {
    return fc_align((int64_t)max_w * max_h * (4 + 4), FC_ALIGN);
}
static inline void fc_barrier() {
#ifdef _WIN32
    MemoryBarrier();
#else
    __sync_synchronize();
#endif
}
static inline int64_t fc_perf_ns() {
#ifdef _WIN32
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (int64_t)((double)c.QuadPart / (double)f.QuadPart * 1e9);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
#endif
}

// A copied-out view of one frame, owned by the caller's buffers.
struct FcFrameView {
    FcSlotDesc desc;
    uint8_t* rgba;   // caller-provided buffer, width*height*4
    float*   depth;  // caller-provided buffer, width*height
};

// ---------------------------------------------------------------------------
// Writer (single writer). Owns the mapping lifetime.
// ---------------------------------------------------------------------------
class FrameChannelWriter {
public:
    FrameChannelWriter()
        : base_(0), size_(0), slot_count_(0), slot_stride_(0),
          max_w_(0), max_h_(0), depth_conv_(FC_REVERSED_Z_01),
          flags_(FC_ROWS_TOP_DOWN), published_(0)
#ifdef _WIN32
        , map_(NULL)
#else
        , fd_(-1)
#endif
    {
        name_[0] = 0;
    }
    ~FrameChannelWriter() { close(); }

    // Create (or replace) the mapping sized for max_w x max_h. Returns false on
    // failure; never throws.
    bool create(const char* name, int32_t max_w, int32_t max_h,
                int32_t slot_count = 3,
                int32_t depth_convention = FC_REVERSED_Z_01,
                bool rows_top_down = true) {
        close();
        copy_name(name);
        max_w_ = max_w; max_h_ = max_h;
        slot_count_ = slot_count;
        slot_stride_ = fc_slot_stride(max_w, max_h);
        depth_conv_ = depth_convention;
        flags_ = rows_top_down ? FC_ROWS_TOP_DOWN : 0;
        size_ = (int64_t)FC_HEADER_BYTES + slot_stride_ * slot_count_;
        if (!map_create()) { close(); return false; }

        memset(base_, 0, (size_t)FC_HEADER_BYTES);
        FcHeader* h = header();
        h->magic = FC_MAGIC;
        h->version = FC_VERSION;
        h->header_size = FC_HEADER_BYTES;
        h->slot_count = slot_count_;
        h->slot_stride = slot_stride_;
        h->max_width = max_w_;
        h->max_height = max_h_;
        h->depth_convention = depth_conv_;
        h->published = 0;
        h->latest_slot = -1;
        h->flags = flags_;
        for (int32_t i = 0; i < slot_count_; ++i) desc(i)->seq = 0;
        fc_barrier();
        return true;
    }

    // Publish one frame. rgba is width*height*4 bytes, depth is width*height
    // floats, both in the header's row order. Returns the slot used, or -1.
    int write(const uint8_t* rgba, const float* depth,
              int32_t width, int32_t height, int64_t frame_index,
              const float pos[3], const float rot[4], float fov,
              int64_t timestamp_us, int64_t capture_ns = 0) {
        if (!base_ || width <= 0 || height <= 0 ||
            width > max_w_ || height > max_h_) return -1;

        int32_t slot = (int32_t)(published_ % slot_count_);
        FcSlotDesc* d = desc(slot);

        // seqlock: odd => write in progress
        int64_t seq = d->seq + 1;
        d->seq = seq;
        fc_barrier();

        d->frame_index = frame_index;
        d->timestamp_us = timestamp_us;
        d->width = width;
        d->height = height;
        d->pos[0] = pos[0]; d->pos[1] = pos[1]; d->pos[2] = pos[2];
        d->rot[0] = rot[0]; d->rot[1] = rot[1]; d->rot[2] = rot[2]; d->rot[3] = rot[3];
        d->fov = fov;
        d->depth_convention = depth_conv_;
        int32_t rgba_bytes = width * height * 4;
        int32_t depth_bytes = width * height * 4;
        d->payload_size = rgba_bytes + depth_bytes;
        d->capture_ns = capture_ns;
        d->publish_ns = fc_perf_ns();

        uint8_t* p = payload(slot);
        memcpy(p, rgba, (size_t)rgba_bytes);
        memcpy(p + rgba_bytes, depth, (size_t)depth_bytes);

        fc_barrier();
        d->seq = seq + 1;  // even => complete
        fc_barrier();

        published_ += 1;
        FcHeader* h = header();
        h->published = published_;
        h->latest_slot = slot;
        fc_barrier();
        return slot;
    }

    void close() {
        map_close();
        base_ = 0; size_ = 0;
    }

    bool valid() const { return base_ != 0; }

    // Remove the POSIX backing file (owner cleanup). No-op on Windows, where a
    // named section vanishes when the last handle closes.
    static void unlink_backing(const char* name) {
#ifndef _WIN32
        char path[300];
        snprintf(path, sizeof(path), "/dev/shm/%s", name);
        ::unlink(path);
#else
        (void)name;
#endif
    }

private:
    FcHeader* header() { return (FcHeader*)base_; }
    FcSlotDesc* desc(int32_t i) {
        return (FcSlotDesc*)(base_ + FC_DESC_OFF + (int64_t)i * FC_DESC_STRIDE);
    }
    uint8_t* payload(int32_t i) {
        return base_ + FC_HEADER_BYTES + (int64_t)i * slot_stride_;
    }
    void copy_name(const char* n) {
        size_t i = 0;
        for (; n && n[i] && i < sizeof(name_) - 1; ++i) name_[i] = n[i];
        name_[i] = 0;
    }

    char    name_[256];
    uint8_t* base_;
    int64_t size_;
    int32_t slot_count_;
    int64_t slot_stride_;
    int32_t max_w_, max_h_;
    int32_t depth_conv_;
    int32_t flags_;
    int64_t published_;

#ifdef _WIN32
    HANDLE map_;
    bool map_create() {
        // Session-local name. Use "Global\\" prefix if a service needs it.
        char full[300];
        _snprintf(full, sizeof(full), "Local\\%s", name_);
        full[sizeof(full) - 1] = 0;
        map_ = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
                                  (DWORD)(size_ >> 32), (DWORD)(size_ & 0xffffffff),
                                  full);
        if (!map_) return false;
        base_ = (uint8_t*)MapViewOfFile(map_, FILE_MAP_ALL_ACCESS, 0, 0, (SIZE_T)size_);
        return base_ != 0;
    }
    void map_close() {
        if (base_) { UnmapViewOfFile(base_); base_ = 0; }
        if (map_) { CloseHandle(map_); map_ = NULL; }
    }
#else
    int fd_;
    bool map_create() {
        char path[300];
        snprintf(path, sizeof(path), "/dev/shm/%s", name_);
        fd_ = ::open(path, O_CREAT | O_RDWR, 0600);
        if (fd_ < 0) return false;
        if (ftruncate(fd_, (off_t)size_) != 0) return false;
        void* m = mmap(0, (size_t)size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
        if (m == MAP_FAILED) return false;
        base_ = (uint8_t*)m;
        return true;
    }
    void map_close() {
        if (base_) { munmap(base_, (size_t)size_); base_ = 0; }
        if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
    }
#endif
};

// ---------------------------------------------------------------------------
// Reader. Attaches to an existing mapping; never unlinks.
// ---------------------------------------------------------------------------
class FrameChannelReader {
public:
    FrameChannelReader()
        : base_(0), size_(0), slot_count_(0), slot_stride_(0),
          max_w_(0), max_h_(0), flags_(0)
#ifdef _WIN32
        , map_(NULL)
#else
        , fd_(-1)
#endif
    { name_[0] = 0; }
    ~FrameChannelReader() { close(); }

    bool open(const char* name) {
        close();
        copy_name(name);
        if (!map_open()) { close(); return false; }
        FcHeader* h = (FcHeader*)base_;
        if (h->magic != FC_MAGIC) { close(); return false; }
        slot_count_ = h->slot_count;
        slot_stride_ = h->slot_stride;
        max_w_ = h->max_width;
        max_h_ = h->max_height;
        flags_ = h->flags;
        return true;
    }

    int64_t published() const { return base_ ? ((FcHeader*)base_)->published : 0; }
    int32_t latest_slot() const { return base_ ? ((FcHeader*)base_)->latest_slot : -1; }

    // Copy the latest completed frame into caller buffers. out.rgba must hold
    // >= max_w*max_h*4 bytes and out.depth >= max_w*max_h floats. Returns true
    // on a clean (non-torn) read, false if none / torn after max_retries.
    bool latest(FcFrameView& out, int max_retries = 8) {
        if (!base_) return false;
        for (int attempt = 0; attempt < max_retries; ++attempt) {
            int32_t slot = latest_slot();
            if (slot < 0) return false;
            FcSlotDesc* d = (FcSlotDesc*)(base_ + FC_DESC_OFF +
                                          (int64_t)slot * FC_DESC_STRIDE);
            int64_t seq0 = d->seq;
            if (seq0 & 1) continue;                 // write in progress
            fc_barrier();
            FcSlotDesc snap = *d;
            if (snap.width <= 0 || snap.height <= 0 ||
                snap.width > max_w_ || snap.height > max_h_) continue;
            int32_t rgba_bytes = snap.width * snap.height * 4;
            int32_t depth_bytes = snap.width * snap.height * 4;
            uint8_t* p = base_ + FC_HEADER_BYTES + (int64_t)slot * slot_stride_;
            memcpy(out.rgba, p, (size_t)rgba_bytes);
            memcpy(out.depth, p + rgba_bytes, (size_t)depth_bytes);
            fc_barrier();
            if (d->seq != seq0) continue;           // torn read, retry
            out.desc = snap;
            return true;
        }
        return false;
    }

    bool rows_top_down() const { return (flags_ & FC_ROWS_TOP_DOWN) != 0; }
    int32_t max_width() const { return max_w_; }
    int32_t max_height() const { return max_h_; }

    void close() { map_close(); base_ = 0; size_ = 0; }
    bool valid() const { return base_ != 0; }

private:
    void copy_name(const char* n) {
        size_t i = 0;
        for (; n && n[i] && i < sizeof(name_) - 1; ++i) name_[i] = n[i];
        name_[i] = 0;
    }
    char     name_[256];
    uint8_t* base_;
    int64_t  size_;
    int32_t  slot_count_;
    int64_t  slot_stride_;
    int32_t  max_w_, max_h_, flags_;

#ifdef _WIN32
    HANDLE map_;
    bool map_open() {
        char full[300];
        _snprintf(full, sizeof(full), "Local\\%s", name_);
        full[sizeof(full) - 1] = 0;
        // Map the header first to learn the full size, then remap.
        map_ = OpenFileMappingA(FILE_MAP_READ, FALSE, full);
        if (!map_) return false;
        uint8_t* probe = (uint8_t*)MapViewOfFile(map_, FILE_MAP_READ, 0, 0,
                                                 (SIZE_T)FC_HEADER_BYTES);
        if (!probe) return false;
        FcHeader* h = (FcHeader*)probe;
        int64_t full_size = (int64_t)FC_HEADER_BYTES +
                            h->slot_stride * h->slot_count;
        UnmapViewOfFile(probe);
        base_ = (uint8_t*)MapViewOfFile(map_, FILE_MAP_READ, 0, 0, (SIZE_T)full_size);
        size_ = full_size;
        return base_ != 0;
    }
    void map_close() {
        if (base_) { UnmapViewOfFile(base_); base_ = 0; }
        if (map_) { CloseHandle(map_); map_ = NULL; }
    }
#else
    int fd_;
    bool map_open() {
        char path[300];
        snprintf(path, sizeof(path), "/dev/shm/%s", name_);
        fd_ = ::open(path, O_RDWR);
        if (fd_ < 0) return false;
        struct stat st;
        if (fstat(fd_, &st) != 0 || st.st_size < FC_HEADER_BYTES) return false;
        size_ = (int64_t)st.st_size;
        void* m = mmap(0, (size_t)size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
        if (m == MAP_FAILED) { base_ = 0; return false; }
        base_ = (uint8_t*)m;
        return true;
    }
    void map_close() {
        if (base_) { munmap(base_, (size_t)size_); base_ = 0; }
        if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
    }
#endif
};

} // namespace tabg_portal

#endif // TABG_PORTAL_FRAMECHANNEL_H
