#pragma once
#include "routine_activity.h"
#include <algorithm>

// Borrow existing world-authored scenario points; never create furniture, alter
// population groups, attach a named PERSCHAR schedule, warp or release the ped.
// API/animation evidence and remaining engine limitations: docs/daily-routine-sources.md.
namespace RoutineActivityBridge
{
using Kind = RoutineActivity::Kind;
using Point = RoutineActivity::Point;
constexpr unsigned kWork = Routine::PhaseMask(Routine::Phase::Work);
constexpr unsigned kLunch = Routine::PhaseMask(Routine::Phase::Lunch);
constexpr unsigned kErrands = Routine::PhaseMask(Routine::Phase::Shops);
constexpr unsigned kEvening = Routine::PhaseMask(Routine::Phase::Leisure);
constexpr unsigned kNight = Routine::PhaseMask(Routine::Phase::Rest);
constexpr unsigned kAll = RoutineData::AllOccupations;
struct Type { const char* name; Hash hash; Kind kind; unsigned phases; unsigned occupations; };
#define ROUTINE_TYPE(name, kind, phases, occupations) { name, Joaat(name), Kind::kind, phases, occupations }
// Only established scenario TYPES, never conditional animation names. Pickup-only
// chain fragments and bartender/player/named-character roles are deliberately absent.
inline const Type kTypes[] = {
    ROUTINE_TYPE("WORLD_HUMAN_BROOM", Work, kWork, kAll),
    ROUTINE_TYPE("WORLD_HUMAN_BROOM_WORKING", Work, kWork, kAll),
    ROUTINE_TYPE("WORLD_HUMAN_STRAW_BROOM_WORKING", Work, kWork, kAll),
    ROUTINE_TYPE("WORLD_HUMAN_PUSH_BROOM_WORKING", Work, kWork, kAll),
    ROUTINE_TYPE("WORLD_HUMAN_HAMMER_GROUND", Work, kWork, kAll),
    ROUTINE_TYPE("WORLD_HUMAN_HAMMER_TABLE", Work, kWork, kAll),
    ROUTINE_TYPE("WORLD_HUMAN_HAMMER_WALL", Work, kWork, kAll),
    ROUTINE_TYPE("WORLD_HUMAN_HAMMER_KNEEL", Work, kWork, kAll),
    ROUTINE_TYPE("WORLD_HUMAN_SAW_WOOD", Work, kWork, RoutineData::Laborer | RoutineData::DockWorker),
    ROUTINE_TYPE("WORLD_HUMAN_CLIPBOARD", Work, kWork, kAll),
    ROUTINE_TYPE("WORLD_HUMAN_FARMER_RAKE", Work, kWork, RoutineData::LivestockHand | RoutineData::Laborer),
    ROUTINE_TYPE("WORLD_HUMAN_FARMER_WEEDING", Work, kWork, RoutineData::LivestockHand | RoutineData::Laborer),
    ROUTINE_TYPE("WORLD_HUMAN_FEED_CHICKEN", Work, kWork, RoutineData::LivestockHand),
    ROUTINE_TYPE("WORLD_HUMAN_FEED_PIGS", Work, kWork, RoutineData::LivestockHand),
    ROUTINE_TYPE("PROP_HUMAN_SEAT_CHAIR_TABLE_EATING_KNIFE_FORK", Eat, kLunch | kEvening, kAll),
    ROUTINE_TYPE("PROP_CAMP_SEAT_CHAIR_TABLE_STEW", Eat, kLunch | kEvening, kAll),
    ROUTINE_TYPE("PROP_CAMP_SEAT_CHAIR_STEW", Eat, kLunch | kEvening, kAll),
    ROUTINE_TYPE("WORLD_CAMP_SIT_GROUND_STEW", Eat, kLunch | kEvening, kAll),
    ROUTINE_TYPE("WORLD_HUMAN_COFFEE_DRINK", Drink, kLunch | kEvening, kAll),
    ROUTINE_TYPE("WORLD_HUMAN_DRINKING", Drink, kLunch | kEvening, kAll),
    ROUTINE_TYPE("WORLD_HUMAN_SIT_DRINK", Drink, kLunch | kEvening, kAll),
    ROUTINE_TYPE("WORLD_HUMAN_LEAN_BACK_WALL_DRINKING", Drink, kLunch | kEvening, kAll),
    ROUTINE_TYPE("WORLD_HUMAN_LEAN_WALL_DRINKING", Drink, kLunch | kEvening, kAll),
    ROUTINE_TYPE("PROP_HUMAN_SEAT_CHAIR_TABLE_DRINKING", Drink, kLunch | kEvening, kAll),
    ROUTINE_TYPE("PROP_HUMAN_SEAT_CHAIR_TABLE_DRINKING_WHISKEY", Drink, kLunch | kEvening, kAll),
    // Bar-customer types include NO_DRINK variants. They are social activity,
    // never evidence that a target drank or successfully took a lunch break.
    ROUTINE_TYPE("WORLD_HUMAN_BARCUSTOMER", Social, kEvening, kAll),
    ROUTINE_TYPE("WORLD_HUMAN_BARCUSTOMER_BEER", Social, kEvening, kAll),
    ROUTINE_TYPE("WORLD_HUMAN_BARCUSTOMER_WHISKEY", Social, kEvening, kAll),
    ROUTINE_TYPE("WORLD_HUMAN_SMOKE", Social, kErrands | kEvening, kAll),
    ROUTINE_TYPE("WORLD_HUMAN_SMOKE_CIGAR", Social, kErrands | kEvening, kAll),
    ROUTINE_TYPE("WORLD_HUMAN_LEAN_READ_PAPER", Social, kErrands | kEvening, kAll),
    ROUTINE_TYPE("PROP_HUMAN_SEAT_CHAIR_READ_NEWSPAPER", Social, kErrands | kEvening, kAll),
    ROUTINE_TYPE("WORLD_CAMP_FIRE_STANDING", Social, kEvening, kAll),
    ROUTINE_TYPE("WORLD_CAMP_FIRE_SIT_GROUND", Social, kEvening, kAll),
    ROUTINE_TYPE("WORLD_CAMP_FIRE_SEATED_GROUND", Social, kEvening, kAll),
    ROUTINE_TYPE("PROP_CAMP_FIRE_SEATED", Social, kEvening, kAll),
    ROUTINE_TYPE("PROP_CAMP_FIRE_SEAT_BENCH", Social, kEvening, kAll),
    ROUTINE_TYPE("PROP_CAMP_FIRE_SEAT_CHAIR", Social, kEvening, kAll),
    ROUTINE_TYPE("WORLD_HUMAN_SIT_GROUND", Rest, kErrands | kEvening | kNight, kAll),
    ROUTINE_TYPE("WORLD_HUMAN_SEAT_STEPS", Rest, kErrands | kEvening | kNight, kAll),
    ROUTINE_TYPE("PROP_HUMAN_SEAT_BENCH", Rest, kErrands | kEvening | kNight, kAll),
    ROUTINE_TYPE("PROP_HUMAN_SEAT_BENCH_BACK", Rest, kErrands | kEvening | kNight, kAll),
    ROUTINE_TYPE("PROP_HUMAN_SEAT_CHAIR", Rest, kErrands | kEvening | kNight, kAll),
    ROUTINE_TYPE("PROP_HUMAN_SEAT_BENCH_TIRED", Rest, kNight, kAll),
    ROUTINE_TYPE("WORLD_HUMAN_SLEEP_GROUND_ARM", Sleep, kNight, kAll),
    ROUTINE_TYPE("WORLD_HUMAN_SLEEP_GROUND_PILLOW", Sleep, kNight, kAll),
    ROUTINE_TYPE("WORLD_HUMAN_SLEEP_GROUND_PILLOW_NO_PILLOW", Sleep, kNight, kAll),
    // Bed types have awake resting conditional variations. Describe them as rest.
    ROUTINE_TYPE("PROP_HUMAN_SLEEP_BED_PILLOW", Rest, kNight, kAll),
    ROUTINE_TYPE("PROP_HUMAN_SLEEP_BED_PILLOW_LEFT", Rest, kNight, kAll),
    ROUTINE_TYPE("PROP_HUMAN_SLEEP_BED_PILLOW_RIGHT", Rest, kNight, kAll)
};
#undef ROUTINE_TYPE
inline const Type* Describe(Hash hash)
{
    for (const auto& type : kTypes) if (type.hash == hash) return &type;
    return nullptr;
}
inline bool Exiting(Ped ped) { return TASK::IS_PED_EXITING_SCENARIO(ped, true) != 0; }
inline bool Busy(Ped ped)
{
    return PED::IS_PED_USING_ANY_SCENARIO(ped) || TASK::PED_HAS_USE_SCENARIO_TASK(ped) || Exiting(ped);
}
inline bool Active(Ped ped, Point point, bool confirmed = false)
{
    // Base establishes successful entry once. A confirmed scenario may leave its
    // base to play a healthy conditional/graph transition: ambient_fishing_scenario.c:
    // 103-141 uses ACTIVE(ped,1) for ongoing liveness, BASE only for initialization.
    // The SDK's misleading second parameter is a mode flag, never a point handle.
    return point.id > 0 && TASK::DOES_SCENARIO_POINT_EXIST(point.id) &&
        PED::IS_PED_USING_THIS_SCENARIO(ped, point.id) &&
        PED::IS_PED_USING_SCENARIO_HASH(ped, point.hash) &&
        (TASK::_PED_IS_IN_SCENARIO_BASE(ped) || (confirmed && TASK::IS_PED_ACTIVE_IN_SCENARIO(ped, 1))) && !Exiting(ped);
}
inline bool SpaceReady(Ped ped, const Vector3& position, bool occupiedBySelf)
{
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) return false;
    if (!RoutineSpawn::Loaded(position)) return false;
    const Interior interior = INTERIOR::GET_INTERIOR_FROM_COLLISION(position);
    if (interior)
    {
        if (!INTERIOR::IS_INTERIOR_READY(interior)) return false;
        const Interior coordinateInterior = INTERIOR::GET_INTERIOR_AT_COORDS(position);
        if (coordinateInterior && coordinateInterior != interior) return false;
    }
    else if (!INTERIOR::IS_COLLISION_MARKED_OUTSIDE(position)) return false;
    if (occupiedBySelf) return true; // no nearby-ped/standing-body rejection of healthy seated actors
    Vector3 approach;
    if (!PATH::GET_SAFE_COORD_FOR_PED(position, false, &approach, 0)) return false;
    const float dx = approach.x - position.x, dy = approach.y - position.y;
    if (!std::isfinite(approach.x) || !std::isfinite(approach.y) || !std::isfinite(approach.z) ||
        dx * dx + dy * dy > 9.0f || std::fabs(approach.z - position.z) > 1.75f || !RoutineSpawn::Loaded(approach)) return false;
    if (INTERIOR::GET_INTERIOR_FROM_COLLISION(approach) != interior) return false;
    // Check the navigation approach, not the authored seat/bed volume containing
    // the intended furniture. TASK_USE_SCENARIO_POINT owns its exact placement.
    if (MISC::IS_POSITION_OCCUPIED(approach, .35f, false, true, true, false, false, ped, true)) return false;
    float water = 0;
    if (WATER::GET_WATER_HEIGHT(Vector3(approach.x, approach.y, approach.z + 2.0f), &water) && water >= approach.z - .2f) return false;
    return true;
}
inline bool Valid(Ped ped, Point point, bool ownedActive)
{
    if (point.id <= 0 || !TASK::DOES_SCENARIO_POINT_EXIST(point.id) ||
        TASK::_GET_SCENARIO_POINT_TYPE(point.id) != point.hash || !TASK::_IS_SCENARIO_POINT_ACTIVE(point.id)) return false;
    const Type* type = Describe(point.hash);
    if (!type || !TASK::IS_SCENARIO_TYPE_ENABLED(type->name)) return false;
    const Ped user = TASK::_GET_PED_USING_SCENARIO_POINT(point.id);
    const bool self = ownedActive && Active(ped, point, true);
    const bool reservedBySelf = user == ped && PED::IS_PED_USING_THIS_SCENARIO(ped, point.id);
    if ((user && user != ped) || (TASK::_IS_SCENARIO_IN_USE(point.id) && user != ped && !self)) return false;
    // The compatibility native may reject an already occupied point, even when
    // our ped is its legitimate occupant. Preserve an observed healthy activity.
    if (!self && !reservedBySelf && !PED::_CAN_PED_USE_SCENARIO_POINT(ped, point.id, 0, 0, 1)) return false;
    return SpaceReady(ped, TASK::_GET_SCENARIO_POINT_COORDS(point.id, true), self);
}
inline Point Find(Ped ped, const RoutineData::Location& location, Routine::Phase phase,
    unsigned occupation, unsigned seed, RoutineActivity::Controller& controller, std::uint64_t now)
{
    constexpr int capacity = 64;
    constexpr float radius = 35.0f;
    // RAGE script arrays store their capacity before eight-byte value slots.
    // native3.c:80917 and homeinvasion.c:91618 pass the array header, not data[0].
    alignas(8) Any points[capacity + 1]{};
    static_assert(sizeof(Any) == 8, "Scenario array slots require the RAGE 64-bit ABI");
    points[0] = capacity;
    const int reported = TASK::GET_SCENARIO_POINTS_IN_AREA(location.anchor, radius, points, capacity);
    const int count = (std::min)((std::max)(reported, 0), capacity);
    Point best;
    unsigned bestRank = (std::numeric_limits<unsigned>::max)();
    for (int index = 0; index < count; ++index)
    {
        const int id = static_cast<int>(points[index + 1]);
        if (id <= 0 || controller.IsCoolingDown(id, now) || !TASK::DOES_SCENARIO_POINT_EXIST(id)) continue;
        const Hash hash = TASK::_GET_SCENARIO_POINT_TYPE(id);
        const Type* type = Describe(hash);
        if (!type || !(type->phases & Routine::PhaseMask(phase)) || !(type->occupations & occupation)) continue;
        const Vector3 position = TASK::_GET_SCENARIO_POINT_COORDS(id, true);
        const float dx = position.x - location.anchor.x, dy = position.y - location.anchor.y;
        Point candidate{id, type->kind, hash};
        if (dx * dx + dy * dy > radius * radius ||
            std::fabs(position.z - location.anchor.z) > location.maxHeightDelta + 1.0f || !Valid(ped, candidate, false))
        { controller.Reject(id, now); continue; }
        // Score authored position and type, never transient handle order, to
        // retain an individual's preferences across streaming/re-enumeration.
        const unsigned x = static_cast<unsigned>(static_cast<int>(std::floor(position.x * 4.0f)));
        const unsigned y = static_cast<unsigned>(static_cast<int>(std::floor(position.y * 4.0f)));
        unsigned rank = RoutinePlan::Mix(seed ^ hash ^ RoutinePlan::Mix(x) ^ RoutinePlan::Mix(y + 0x9e3779b9u));
        // A nighttime sleeper prefers an authored sleep point to a generic seat.
        if (phase == Routine::Phase::Rest && type->kind == Kind::Sleep) rank >>= 2;
        if (!best.id || rank < bestRank) { best = candidate; bestRank = rank; }
    }
    return best;
}
inline void Start(Ped ped, Point point)
{
    // beat_drunk_dueler.c:2496: normal enter enabled, warp disabled, no forced
    // conditional clip. A bounded controller observes success before reporting it.
    TASK::TASK_USE_SCENARIO_POINT(ped, point.id, nullptr, -1, true, false, 0, false, -1.0f, false);
}
}
