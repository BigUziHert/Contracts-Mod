#pragma once

#include "routine_logic.h"
#include "routine_locations.h" // The SDK's Vector3 must already be declared.

#include <cstring>
#include <string>

// One immutable set of habits per contract. Runtime availability selects from these
// four places, so the card never advertises a separately rerolled destination.
namespace RoutinePlan
{
struct Plan
{
    std::uint32_t seed = 0;
    int townIndex = -1;
    unsigned occupation = 0;
    int offsetMinutes = 0;
    int route[4] = {-1, -1, -1, -1}; // Work, Shops, Leisure, Rest; indices into kLocations.
};
static_assert(static_cast<int>(RoutineData::PlaceKind::Work) == static_cast<int>(Routine::Phase::Work));
static_assert(static_cast<int>(RoutineData::PlaceKind::Shops) == static_cast<int>(Routine::Phase::Shops));
static_assert(static_cast<int>(RoutineData::PlaceKind::Leisure) == static_cast<int>(Routine::Phase::Leisure));
static_assert(static_cast<int>(RoutineData::PlaceKind::Rest) == static_cast<int>(Routine::Phase::Rest));

inline bool NamedOccupation(unsigned occupation)
{
    return occupation == RoutineData::Local || occupation == RoutineData::Laborer ||
        occupation == RoutineData::DockWorker || occupation == RoutineData::LivestockHand;
}
inline const char* OccupationName(unsigned occupation)
{
    switch (occupation)
    {
    case RoutineData::Local: return "Local resident";
    case RoutineData::Laborer: return "Laborer";
    case RoutineData::DockWorker: return "Dock worker";
    case RoutineData::LivestockHand: return "Livestock hand";
    default: return "Unknown";
    }
}
inline std::uint32_t Mix(std::uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7FEB352Du;
    value ^= value >> 15;
    value *= 0x846CA68Bu;
    return value ^ (value >> 16);
}
inline bool Build(Plan& plan, int townIndex, unsigned occupation, std::uint32_t seed)
{
    plan = Plan{}; // Failed preparation cannot expose a partially constructed route/card.
    if (townIndex < 0 || townIndex >= RoutineData::kTownCount || !NamedOccupation(occupation)) return false;
    Plan prepared;
    prepared.seed = seed;
    prepared.townIndex = townIndex;
    prepared.occupation = occupation;
    prepared.offsetMinutes = Routine::BoundedOffset(Mix(seed));
    for (int phase = 0; phase < 4; ++phase)
    {
        int eligible[RoutineData::kLocationCount]{};
        unsigned count = 0;
        for (int index = 0; index < RoutineData::kLocationCount; ++index)
        {
            const auto& location = RoutineData::kLocations[index];
            if (location.enabled && location.town == RoutineData::kTowns[townIndex].id &&
                static_cast<int>(location.kind) == phase && (location.occupations & occupation) != 0)
                eligible[count++] = index;
        }
        if (count == 0) return false;
        const std::uint32_t salt = static_cast<std::uint32_t>(phase + 1) * 0x9E3779B9u;
        prepared.route[phase] = eligible[Mix(seed ^ salt) % count];
    }
    plan = prepared;
    return true;
}
inline bool Valid(const Plan& plan)
{
    if (plan.townIndex < 0 || plan.townIndex >= RoutineData::kTownCount || !NamedOccupation(plan.occupation)) return false;
    for (int phase = 0; phase < 4; ++phase)
    {
        const int index = plan.route[phase];
        if (index < 0 || index >= RoutineData::kLocationCount) return false;
        const auto& location = RoutineData::kLocations[index];
        if (!location.enabled || location.town != RoutineData::kTowns[plan.townIndex].id ||
            static_cast<int>(location.kind) != phase || (location.occupations & plan.occupation) == 0) return false;
    }
    return true;
}
inline bool Candidates(const Plan& plan, Routine::Candidate out[4])
{
    if (!out) return false;
    for (int phase = 0; phase < 4; ++phase)
    {
        out[phase] = Routine::Candidate{};
        out[phase].available = false;
    }
    if (!Valid(plan)) return false;
    for (int phase = 0; phase < 4; ++phase)
    {
        const auto& location = RoutineData::kLocations[plan.route[phase]];
        out[phase] = {plan.route[phase], Routine::PhaseMask(static_cast<Routine::Phase>(phase)),
            location.occupations, {location.openMinute, location.closeMinute}, location.enabled,
            phase == static_cast<int>(Routine::Phase::Rest), 0};
    }
    return true;
}
inline const char* CardLocationName(const RoutineData::Location& location)
{
    // Explicit alias preserves the exterior clue without truncating arbitrary names.
    return std::strcmp(location.name, "General store frontage") == 0 ? "Store frontage" : location.name;
}
inline std::array<std::string, 6> CardLines(const Plan& plan)
{
    std::array<std::string, 6> lines{};
    if (!Valid(plan)) return lines;
    lines[0] = std::string("Occupation: ") + OccupationName(plan.occupation);
    lines[1] = std::string("Town: ") + RoutineData::kTowns[plan.townIndex].name;
    constexpr const char* prefixes[4] = {"Day: ", "Afternoon: ", "Evening: ", "Late: "};
    for (int phase = 0; phase < 4; ++phase)
        lines[phase + 2] = std::string(prefixes[phase]) + CardLocationName(RoutineData::kLocations[plan.route[phase]]);
    return lines;
}
}
