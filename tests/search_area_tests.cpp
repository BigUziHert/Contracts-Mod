#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using Blip = int;
using Hash = unsigned;
struct Vector3 { float x = 0, y = 0, z = 0; };
#include "../rdr2 scripting environment/samples/Pools/routine_locations.h"

static unsigned checks = 0;
static void Check(bool condition, const char* message)
{
    ++checks;
    if (!condition) { std::fprintf(stderr, "FAILED: %s\n", message); std::exit(EXIT_FAILURE); }
}
static bool SamePoint(Vector3 a, Vector3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
static float DistSq(Vector3 a, Vector3 b)
{
    const float x = a.x - b.x, y = a.y - b.y, z = a.z - b.z;
    return x * x + y * y + z * z;
}
struct ContractDef { Vector3 spawn; float searchRadius = 0; bool routine = false; };
static bool IsRoutine(const ContractDef& definition) { return definition.routine; }
enum ContractState { CONTRACT_NONE, CONTRACT_UNKNOWN, CONTRACT_FOUND, CONTRACT_DEAD, CONTRACT_PAID };
static ContractState g_state = CONTRACT_UNKNOWN;
static struct Contract { const ContractDef* def = nullptr; Blip searchBlip = 0; Vector3 targetPos; } C;
static struct Routine
{
    Vector3 centre;
    float wanderRadius = RoutineData::kWanderRadius;
    int destination = 0;
    bool destinationValid = true;
    bool ambientFallback = false;
} R;
static constexpr Hash BLIP_STYLE_MP_MISSION_GIVER = 1234;
static struct World
{
    Blip handle = 77;
    Vector3 centre;
    float radius = 0;
    bool exists = false;
    unsigned creates = 0, moves = 0, styles = 0, coordinateReads = 0, existenceReads = 0;
    std::vector<std::string> events;
} w;
namespace MAP
{
static Blip BLIP_ADD_FOR_RADIUS(Hash style, Vector3 centre, float radius)
{
    Check(style == BLIP_STYLE_MP_MISSION_GIVER, "search creation keeps the mission radius style");
    ++w.creates; w.exists = true; w.centre = centre; w.radius = radius;
    w.events.push_back("create"); return w.handle;
}
static bool DOES_BLIP_EXIST(Blip blip) { ++w.existenceReads; return blip == w.handle && w.exists; }
static Vector3 GET_BLIP_COORDS(Blip blip)
{
    Check(blip == w.handle && w.exists, "search coordinates are read only from the existing search handle");
    ++w.coordinateReads; return w.centre;
}
static void SET_BLIP_COORDS(Blip blip, Vector3 centre)
{
    Check(blip == w.handle && w.exists, "search movement preserves the existing circle handle");
    ++w.moves; w.centre = centre; w.events.push_back("move");
}
}
static void StyleTargetBlip(Blip blip, const char* color, bool large, bool pulse)
{
    Check(blip == w.handle && w.exists && std::strcmp(color, "BLIP_MODIFIER_MP_COLOR_32") == 0 && large && !pulse,
        "initial search circle keeps its existing color and large non-pulsing style");
    ++w.styles;
}
static void TraceCardInspection() { w.events.push_back("trace"); }
static void UpdateRoutineDebug() { w.events.push_back("debug"); }
static void UpdateCard() { w.events.push_back("card"); }
static void WAIT(int delay) { Check(delay == 0, "frame tail preserves its zero-delay yield"); w.events.push_back("wait"); }

#include "search_area_under_test.h"

static ContractDef definition;
static void Reset(bool routine = true)
{
    w = {}; C = {}; R = {}; g_state = CONTRACT_UNKNOWN;
    definition = {{2721.924f, -1281.781f, 49.68018f}, 310.0f, routine};
    C.def = &definition;
    // This is the validated arrival, deliberately distinct from the old town anchor.
    R.centre = {2823.0f, -1416.0f, 45.5f};
    C.targetPos = R.centre;
}
static void RoutineStartsAtItsValidatedArrival()
{
    Reset(); AddSearchBlip();
    Check(C.searchBlip == w.handle && w.creates == 1 && w.styles == 1,
        "a routine search creates and styles one owned circle");
    Check(w.radius == 35.0f && w.radius == R.wanderRadius,
        "routine search uses the same 35 metre radius as wandering");
    Check(SamePoint(w.centre, R.centre) && !SamePoint(w.centre, definition.spawn),
        "routine search starts at the validated arrival instead of the obsolete town anchor");
    const Vector3 stop = w.centre;
    for (int frame = 0; frame < 5; ++frame)
    {
        C.targetPos = {stop.x + 5.0f * frame, stop.y - 3.0f * frame, stop.z};
        UpdateSearchArea();
        Check(SamePoint(w.centre, stop) && w.moves == 0 && w.creates == 1 && w.styles == 1,
            "unchanged routine stop and individual target movement make no map write");
    }
}
static void NewStopMovesTheSameCircleOnce()
{
    Reset(); AddSearchBlip();
    const Blip original = C.searchBlip;
    ++R.destination; R.centre = {2684.0f, -1399.0f, 46.7f};
    UpdateSearchArea();
    Check(C.searchBlip == original && w.moves == 1 && w.creates == 1 && w.styles == 1 &&
        SamePoint(w.centre, R.centre) && w.radius == 35.0f,
        "a new valid routine stop moves the original 35 metre circle once without recreating or restyling it");
    for (int frame = 0; frame < 4; ++frame) UpdateSearchArea();
    Check(w.moves == 1 && w.creates == 1, "subsequent frames at the new stop do not repeat the map write");
    ++R.destination;
    UpdateSearchArea();
    Check(w.moves == 1, "a new destination id at the same coordinates needs no map write");
}
static void InvalidAndInactiveSearchesDoNotMove()
{
    for (int blocked = 0; blocked < 10; ++blocked)
    {
        Reset(); AddSearchBlip();
        const Vector3 original = w.centre;
        R.centre = {100, 200, 300};
        if (blocked == 0) C.def = nullptr;
        if (blocked == 1) definition.routine = false;
        if (blocked == 2) R.destination = -1;
        if (blocked == 3) R.destinationValid = false;
        if (blocked == 4) C.searchBlip = 0;
        if (blocked == 5) w.exists = false;
        if (blocked == 6) g_state = CONTRACT_NONE;
        if (blocked == 7) g_state = CONTRACT_FOUND;
        if (blocked == 8) g_state = CONTRACT_DEAD;
        if (blocked == 9) g_state = CONTRACT_PAID;
        UpdateSearchArea();
        Check(SamePoint(w.centre, original) && w.moves == 0 && w.creates == 1 && w.styles == 1,
            "invalid or pending destinations, inactive or found contracts, and missing blips make no map write");
        Check(w.coordinateReads == 0, "rejected search updates do not read circle coordinates");
    }
}
static void LegacySearchKeepsItsDefinition()
{
    Reset(false); AddSearchBlip();
    Check(SamePoint(w.centre, definition.spawn) && w.radius == definition.searchRadius && w.radius == 310.0f,
        "legacy search retains its authored spawn point and radius");
    R.centre = {100, 200, 300}; ++R.destination;
    C.targetPos = {400, 500, 600}; UpdateSearchArea();
    Check(SamePoint(w.centre, definition.spawn) && w.moves == 0 && w.creates == 1 && w.coordinateReads == 0,
        "routine and target movement cannot alter a legacy search circle");
}
static void CachedFallbackKeepsItsAuthoredCircle()
{
    Reset(); AddSearchBlip();
    const Blip original = C.searchBlip;
    R.centre = {2822, -1415, 45.5f};
    R.ambientFallback = true; R.destinationValid = false;
    UpdateSearchArea();
    Check(C.searchBlip == original && w.moves == 1 && w.creates == 1 &&
        SamePoint(w.centre, R.centre) && w.radius == 35.0f,
        "route recovery keeps the same 35 metre search circle at the cached authored fallback");
    for (int frame = 0; frame < 4; ++frame)
    {
        C.targetPos.x += 10;
        UpdateSearchArea();
    }
    Check(w.moves == 1, "fallback wandering never makes the circle chase individual footsteps");
    R.destination = -1; R.centre = {0, 0, 0};
    UpdateSearchArea();
    Check(w.moves == 1, "fallback cannot move the search circle without a known authored destination");
}
static void MainUpdatesSearchBeforeItsProtectedTail()
{
    Reset(); AddSearchBlip(); w.events.clear();
    ++R.destination; R.centre = {100, 200, 300};
    RunProductionSearchFrameTail();
    Check(w.events == std::vector<std::string>{"move", "trace", "debug", "card", "wait"},
        "the actual main tail updates search before inspection trace, debug, card rendering and yield");
    w.events.clear(); RunProductionSearchFrameTail();
    Check(w.events == std::vector<std::string>{"trace", "debug", "card", "wait"} && w.moves == 1,
        "an unchanged search leaves the protected main tail order intact without another map write");
}
int main()
{
    RoutineStartsAtItsValidatedArrival();
    NewStopMovesTheSameCircleOnce();
    InvalidAndInactiveSearchesDoNotMove();
    LegacySearchKeepsItsDefinition();
    CachedFallbackKeepsItsAuthoredCircle();
    MainUpdatesSearchBeforeItsProtectedTail();
    std::printf("Search area native bridge: %u checks passed.\n", checks);
}
