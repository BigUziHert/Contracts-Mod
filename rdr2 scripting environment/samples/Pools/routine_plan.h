#pragma once

#include "routine_logic.h"
#include "routine_locations.h" // The SDK's Vector3 must already be declared.

#include <cstring>
#include <string>

// One immutable set of habits per contract. Runtime availability selects from these
// five places, so the card never advertises a separately rerolled destination.
namespace RoutinePlan
{
struct Plan
{
    std::uint32_t seed = 0;
    int townIndex = -1;
    unsigned occupation = 0;
    int offsetMinutes = 0; // Legacy bridge field; never shifts the fixed schedule.
    int route[Routine::kPhaseCount] = {-1, -1, -1, -1, -1}; // Work, Shops, Leisure, Rest, Lunch.
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
inline unsigned GeneratedOccupation(int townIndex, std::uint32_t seed)
{
    if (townIndex < 0 || townIndex >= RoutineData::kTownCount) return 0;
    switch (RoutineData::kTowns[townIndex].id)
    {
    case RoutineData::TownId::SaintDenis:
        return ((seed >> 8) & 1u) ? RoutineData::Laborer : RoutineData::DockWorker;
    case RoutineData::TownId::VanHorn: return RoutineData::DockWorker;
    case RoutineData::TownId::Annesburg: return RoutineData::Laborer;
    case RoutineData::TownId::Rhodes:
    case RoutineData::TownId::Blackwater:
    case RoutineData::TownId::Valentine:
    case RoutineData::TownId::Strawberry:
        return ((seed >> 8) & 1u) ? RoutineData::Laborer : RoutineData::LivestockHand;
    }
    return 0;
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
    for (int phase = 0; phase < Routine::kPhaseCount; ++phase)
    {
        int eligible[RoutineData::kLocationCount]{};
        unsigned count = 0;
        for (int index = 0; index < RoutineData::kLocationCount; ++index)
        {
            const auto& location = RoutineData::kLocations[index];
            if (location.enabled && location.town == RoutineData::kTowns[townIndex].id &&
                RoutineData::SupportsPhase(location, static_cast<Routine::Phase>(phase)) &&
                (location.occupations & occupation) != 0)
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
    for (int phase = 0; phase < Routine::kPhaseCount; ++phase)
    {
        const int index = plan.route[phase];
        if (index < 0 || index >= RoutineData::kLocationCount) return false;
        const auto& location = RoutineData::kLocations[index];
        if (!location.enabled || location.town != RoutineData::kTowns[plan.townIndex].id ||
            !RoutineData::SupportsPhase(location, static_cast<Routine::Phase>(phase)) ||
            (location.occupations & plan.occupation) == 0) return false;
    }
    return true;
}
inline bool Candidates(const Plan& plan, Routine::Candidate out[Routine::kPhaseCount])
{
    if (!out) return false;
    for (int phase = 0; phase < Routine::kPhaseCount; ++phase)
    {
        out[phase] = Routine::Candidate{};
        out[phase].available = false;
    }
    if (!Valid(plan)) return false;
    for (int phase = 0; phase < Routine::kPhaseCount; ++phase)
    {
        const auto& location = RoutineData::kLocations[plan.route[phase]];
        out[phase] = {plan.route[phase], Routine::PhaseMask(static_cast<Routine::Phase>(phase)),
            location.occupations, {location.openMinute, location.closeMinute}, location.enabled,
            phase == static_cast<int>(Routine::Phase::Rest), 0};
        // Public approaches have no claimed business opening time. Bound actual
        // meal/evening selection to this requested visit, including arrival time.
        if (phase == static_cast<int>(Routine::Phase::Lunch)) out[phase].hours = {660, 720};
        if (phase == static_cast<int>(Routine::Phase::Leisure)) out[phase].hours = {1140, 180};
    }
    return true;
}
inline const char* CardLocationName(const RoutineData::Location& location)
{
    // Explicit short aliases preserve area/frontage meaning and fit the card.
    struct Alias { const char* id; const char* name; };
    constexpr Alias aliases[] = {
        {"sd_docks", "Dock area"}, {"sd_docks_east", "East dock area"},
        {"val_auction_south", "South auction"}, {"val_auction_north", "North auction"},
        {"asb_factory_street", "Factory street"}, {"asb_north_yard", "North yard"},
        {"val_saloon", "Smithfield's area"}, {"sd_fancy_saloon", "Fancy saloon area"},
        {"blw_public", "Western campfire"}, {"blw_north_civic", "Civic approach"},
        {"sd_vegetable_market", "Vegetable market"}, {"val_theatre", "Theatre approach"},
        {"sd_photographer", "Photo studio area"}, {"str_south_loop", "South-loop street"},
        {"sd_trapper", "Trapper market"}
    };
    for (const auto& alias : aliases)
        if (std::strcmp(location.id, alias.id) == 0) return alias.name;
    return std::strcmp(location.name, "General store frontage") == 0 ? "Store frontage" : location.name;
}
inline std::array<std::string, 6> CardLines(const Plan& plan)
{
    std::array<std::string, 6> lines{};
    if (!Valid(plan)) return lines;
    lines[0] = std::string(OccupationName(plan.occupation)) + ", " + RoutineData::kTowns[plan.townIndex].name;
    constexpr Routine::Phase phases[] = {Routine::Phase::Work, Routine::Phase::Lunch,
        Routine::Phase::Shops, Routine::Phase::Leisure, Routine::Phase::Rest};
    constexpr const char* prefixes[] = {"Work 06-11/12-16 ", "Lunch 11-12 ",
        "Errands 16-19 ", "Eve 19-03 ", "Rest 03-06 "};
    for (int row = 0; row < Routine::kPhaseCount; ++row)
        lines[row + 1] = std::string(prefixes[row]) +
            CardLocationName(RoutineData::kLocations[plan.route[static_cast<int>(phases[row])]]);
    return lines;
}
}
