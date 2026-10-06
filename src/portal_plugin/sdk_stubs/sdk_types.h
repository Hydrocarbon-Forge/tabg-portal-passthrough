// sdk_types.h — minimal shim of the Source SDK math / engine value types that
// passthrough.cpp passes across interface calls. NOT the real SDK; see
// interface.h for the swap-on-Windows note.
#ifndef PP_SDK_TYPES_H
#define PP_SDK_TYPES_H

#include <math.h>

// Source world-space vector (units = inches; recon §4). x fwd, y left, z up.
class Vector {
public:
    float x, y, z;
    Vector() : x(0), y(0), z(0) {}
    Vector(float X, float Y, float Z) : x(X), y(Y), z(Z) {}
    void Init(float X = 0, float Y = 0, float Z = 0) { x = X; y = Y; z = Z; }
};

// Source Euler angles. Component order is (pitch, yaw, roll) degrees; yaw about
// +Z counter-clockwise from above (recon §4).
class QAngle {
public:
    float x, y, z;
    QAngle() : x(0), y(0), z(0) {}
    QAngle(float pitch, float yaw, float roll) : x(pitch), y(yaw), z(roll) {}
    void Init(float pitch = 0, float yaw = 0, float roll = 0) { x = pitch; y = yaw; z = roll; }
};

// Opaque entity dictionary pointer; the plugin never dereferences it.
struct edict_s;
typedef struct edict_s edict_t;

// Console command argv wrapper (SDK public/tier1/convar.h / icvar). Only the
// accessors ClientCommand() might read are shimmed.
class CCommand {
public:
    CCommand() {}
    int ArgC() const { return 0; }
    const char *Arg(int) const { return ""; }
    const char *ArgS() const { return ""; }
    const char *GetCommandString() const { return ""; }
};

// materialsystem/imageformat.h subset. Values match the SDK enum ordering for
// the formats the capture path uses.
enum ImageFormat {
    IMAGE_FORMAT_UNKNOWN = -1,
    IMAGE_FORMAT_RGBA8888 = 0,
    IMAGE_FORMAT_ABGR8888,
    IMAGE_FORMAT_RGB888,
    IMAGE_FORMAT_BGR888,
    IMAGE_FORMAT_BGRA8888,
    IMAGE_FORMAT_ARGB8888,
    IMAGE_FORMAT_R32F = 30,
    IMAGE_FORMAT_RGBA32323232F
};

#endif // PP_SDK_TYPES_H
