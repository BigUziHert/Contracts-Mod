#pragma once

// Synchronous, optional sink. No game natives, waits or persistent resources here.
// Vector3 is supplied by the SDK or the native-shim test before this header.
namespace StartupTrace
{
struct Event
{
    const char* stage;
    unsigned model;
    int ped;
    bool hasPoint;
    Vector3 point;
    int freePeds;
    const char* detail;
};
using Sink = void (*)(const Event&);
inline Sink sink = nullptr;
inline void Record(const char* stage, unsigned model = 0, int ped = 0,
    const Vector3* point = nullptr, int freePeds = -1, const char* detail = "")
{
    if (sink) sink({stage, model, ped, point != nullptr, point ? *point : Vector3{}, freePeds, detail});
}
}
