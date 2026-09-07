#pragma once

#include "routine_locations.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

// Testing overlay only. Vector3, Blip, MAP, joaat, Literal, GetTickCount64 and
// the existing RemoveBlip(Blip&) helper must be declared before this header.
// No ped, task, scenario, route, streaming or gameplay-blip state is modified.
namespace RoutineDebugBlips
{
namespace Detail
{
inline constexpr std::uint64_t kRetryMs = 2000;
inline constexpr unsigned kMaximumAttempts = 3;
struct Marker
{
    Blip handle = 0;
    Vector3 position{};
    std::string label;
    std::uint64_t nextAttemptMs = 0;
    unsigned attempts = 0;
};
struct State
{
    bool enabled = false;
    std::array<Marker, RoutineData::kLocationCount> anchors{};
    Marker spawn, destination;
};
inline State state;

inline bool Finite(const Vector3& point)
{
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}
inline bool Same(const Vector3& a, const Vector3& b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}
inline void Remove(Marker& marker)
{
    RemoveBlip(marker.handle);
    marker = Marker{};
}
inline const char* TownName(RoutineData::TownId id)
{
    for (const auto& town : RoutineData::kTowns) if (town.id == id) return town.name;
    return "Unknown town";
}
inline void Sync(Marker& marker, const Vector3* point, float scale, std::uint64_t now)
{
    if (!point || !Finite(*point)) { Remove(marker); return; }
    if (marker.handle)
    {
        if (!MAP::DOES_BLIP_EXIST(marker.handle))
        {
            // Do not touch an already missing handle. Start a fresh, bounded
            // recovery episode after a delay rather than recreating every frame.
            marker.handle = 0;
            marker.attempts = 0;
            marker.nextAttemptMs = now + kRetryMs;
            return;
        }
        if (!Same(marker.position, *point))
        {
            // Rockstar updates retained handles in short_update.c:27719.
            MAP::SET_BLIP_COORDS(marker.handle, *point);
            marker.position = *point;
        }
        return;
    }
    if (marker.attempts >= kMaximumAttempts || now < marker.nextAttemptMs) return;
    ++marker.attempts;
    marker.nextAttemptMs = now + kRetryMs;
    // Local RPF catalog: blip_styles/README.md:86 defines COLOR_RED and Always
    // visibility. textures/blips/README.md:85 lists the small pedestrian dot.
    // marston6.c:78913 independently uses the matching DEBUG_RED modifier.
    const Blip created = MAP::BLIP_ADD_FOR_COORDS(joaat("BLIP_STYLE_DEBUG_RED"), *point);
    if (!created || !MAP::DOES_BLIP_EXIST(created)) return;
    marker.handle = created;
    marker.position = *point;
    marker.attempts = 0;
    MAP::SET_BLIP_SPRITE(marker.handle, joaat("BLIP_AMBIENT_PED_SMALL"), true);
    MAP::SET_BLIP_SCALE(marker.handle, scale);
    MAP::_SET_BLIP_NAME(marker.handle, Literal(marker.label.c_str()));
}
}

inline void Clear()
{
    for (auto& marker : Detail::state.anchors) Detail::Remove(marker);
    Detail::Remove(Detail::state.spawn);
    Detail::Remove(Detail::state.destination);
    Detail::state.enabled = false;
}

inline void Update(bool enabled, const Vector3* spawn, const Vector3* destination)
{
    if (!enabled)
    {
        if (Detail::state.enabled) Clear();
        return;
    }
    Detail::state.enabled = true;
    const std::uint64_t now = GetTickCount64();
    for (int index = 0; index < RoutineData::kLocationCount; ++index)
    {
        const auto& location = RoutineData::kLocations[index];
        auto& marker = Detail::state.anchors[index];
        if (marker.label.empty())
            marker.label = std::string("Routine: ") + Detail::TownName(location.town) + " - " + location.name;
        Detail::Sync(marker, location.enabled ? &location.anchor : nullptr, .8f, now);
    }
    if (Detail::state.spawn.label.empty()) Detail::state.spawn.label = "Routine: Exact initial spawn";
    if (Detail::state.destination.label.empty()) Detail::state.destination.label = "Routine: Current destination";
    Detail::Sync(Detail::state.spawn, spawn, 1.2f, now);
    Detail::Sync(Detail::state.destination, destination, 1.5f, now);
}
}
