#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using Ped = int;
using Entity = int;
using BOOL = int;
using DWORD = unsigned;
using ULONGLONG = unsigned long long;
using Hash = unsigned;
struct Vector3
{
    float x, y, z;
    Vector3(float a = 0, float b = 0, float c = 0) : x(a), y(b), z(c) {}
};
static unsigned checks = 0;
static void Check(bool value, const char* message)
{
    ++checks;
    if (!value) { std::fprintf(stderr, "FAILED: %s\n", message); std::exit(1); }
}
static bool Within(Vector3 a, Vector3 b, float distance)
{
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz <= distance * distance;
}

// Only the game data types are shimmed. The test compiles the actual runtime,
// plan, location catalogue, scheduling policy and spawn validator below.
constexpr Hash Joaat(const char* text)
{
    Hash value = 0;
    for (; *text; ++text)
    {
        unsigned c = static_cast<unsigned char>(*text);
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        value += c; value += value << 10; value ^= value >> 6;
    }
    value += value << 3; value ^= value >> 11; value += value << 15;
    return value;
}
struct ModelSet { const Hash* list; int count; };
template<std::size_t N> constexpr ModelSet Models(const Hash (&values)[N]) { return {values, static_cast<int>(N)}; }
struct ContractDef;
struct TargetBehavior
{
    void (*setup)(Ped, const ContractDef&);
    void (*update)(Ped, const ContractDef&);
};
static const TargetBehavior kHumanTarget{nullptr, nullptr};
struct ContractDef
{
    const char* name;
    const char* targetDesc;
    const char* hint;
    Vector3 spawn;
    float searchRadius;
    ModelSet models;
    const TargetBehavior* behavior;
    void (*onSpawned)(const ContractDef&);
    void (*onCleanup)();
};
namespace Tune { constexpr float kReAggroSightDist = 45.0f; }
static constexpr Hash SD_DOCK[] = {Joaat("a_m_m_sddockworkers_02"), Joaat("a_m_m_nbxdockworkers_01")};

struct World
{
    unsigned now = 1000, waits = 0, cancelAfter = 999999;
    int minute = 720;
    bool canInteract = true, collision = true, nav = true, safe = true;
    bool outside = true, groundOk = true, occupied = false, hit = false;
    bool sceneActive = false, startOk = true;
    int interior = 0, probeStatus = 2, starts = 0, stops = 0, safeCalls = 0;
    int requests = 0, failFirstRequests = 0, probes = 0;
    float inputGround = 0, safeDx = 0, groundDz = 0;
    Ped lastIgnored = -1, wanderedPed = 0;
    Vector3 wanderCentre{};
    Vector3 pedPosition{};
    float wanderRadius = 0, avoidRadius = 0;
    int wanderCalls = 0, keepCalls = 0, avoidCalls = 0, waterCalls = 0;
    bool mayEnterWater = true;
} w;
static bool PlayerAvailable() { return w.waits < w.cancelAfter; }
static bool CanStartInteraction() { return PlayerAvailable() && w.canInteract; }
template<typename Predicate> static bool WaitUntil(DWORD timeout, Predicate predicate)
{
    const unsigned start = w.now;
    while (PlayerAvailable())
    {
        if (predicate()) return true;
        if (w.now - start >= timeout) return false;
        w.now += 16; ++w.waits;
    }
    return false;
}
namespace CLOCK
{
static int GET_CLOCK_HOURS() { return w.minute / 60; }
static int GET_CLOCK_MINUTES() { return w.minute % 60; }
}
namespace ENTITY
{
static bool HAS_COLLISION_LOADED_AROUND_POSITION(Vector3) { return w.collision; }
static Vector3 GET_ENTITY_COORDS(Ped, bool, bool) { return w.pedPosition; }
}
namespace PATH
{
static bool IS_NAVMESH_LOADED_IN_AREA(Vector3, Vector3) { return w.nav; }
static bool GET_SAFE_COORD_FOR_PED(Vector3 position, bool, Vector3* result, int)
{
    ++w.safeCalls; w.inputGround = position.z; *result = position; result->x += w.safeDx;
    return w.safe && w.requests > w.failFirstRequests;
}
static void ADD_NAVMESH_REQUIRED_REGION(float, float, float) {}
}
namespace MISC
{
static bool GET_GROUND_Z_AND_NORMAL_FOR_3D_COORD(Vector3, float* ground, Vector3* normal)
{
    *ground = w.inputGround + w.groundDz; *normal = {0, 0, 1}; return w.groundOk;
}
static bool IS_POSITION_OCCUPIED(Vector3, float, bool, bool, bool, bool, bool, Ped ignore, bool)
{
    w.lastIgnored = ignore; return w.occupied;
}
}
namespace INTERIOR
{
static bool IS_COLLISION_MARKED_OUTSIDE(Vector3) { return w.outside; }
static int GET_INTERIOR_FROM_COLLISION(Vector3) { return w.interior; }
}
namespace WATER { static bool GET_WATER_HEIGHT(Vector3, float*) { return false; } }
namespace SHAPETEST
{
static int START_EXPENSIVE_SYNCHRONOUS_SHAPE_TEST_LOS_PROBE(Vector3, Vector3, int, Entity ignore, int)
{
    w.lastIgnored = ignore; return ++w.probes;
}
static int GET_SHAPE_TEST_RESULT(int, BOOL* hit, Vector3*, Vector3*, Entity*) { *hit = w.hit; return w.probeStatus; }
}
namespace STREAMING
{
static bool IS_LOAD_SCENE_ACTIVE() { return w.sceneActive; }
static bool LOAD_SCENE_START_SPHERE(Vector3, float, int) { ++w.starts; w.sceneActive = w.startOk; return w.startOk; }
static void LOAD_SCENE_STOP() { ++w.stops; w.sceneActive = false; }
static void REQUEST_COLLISION_AT_COORD(Vector3) { ++w.requests; }
}
namespace TASK
{
static void SET_PED_PATH_PREFER_TO_AVOID_WATER(Ped, bool avoid, float radius)
{
    Check(avoid, "routine path prefers avoiding water"); ++w.avoidCalls; w.avoidRadius = radius;
}
static void SET_PED_PATH_MAY_ENTER_WATER(Ped, bool value) { ++w.waterCalls; w.mayEnterWater = value; }
static void TASK_WANDER_IN_AREA(Ped ped, Vector3 centre, float radius, float, float, int)
{
    ++w.wanderCalls; w.wanderedPed = ped; w.wanderCentre = centre; w.wanderRadius = radius;
}
}
namespace PED { static void SET_PED_KEEP_TASK(Ped, bool keep) { Check(keep, "routine wander keeps its task"); ++w.keepCalls; } }

#include "../rdr2 scripting environment/samples/Pools/routine_runtime.h"

static RoutineRuntime MakePrepared()
{
    w = {}; std::srand(23);
    RoutineRuntime prepared;
    Check(PrepareRoutineContract(prepared), "available outdoor route prepares successfully");
    return prepared;
}
static void TestPreparedDefinition()
{
    ResetRoutine();
    RoutineRuntime prepared = MakePrepared();
    Check(!R.enabled, "preparing a candidate does not publish it into active runtime");
    Check(prepared.enabled && RoutinePlan::Valid(prepared.plan), "prepared route is complete and enabled");
    Check(w.safeCalls > 0 && w.probes > 0, "a committed candidate passes actual nav and body validators");
    const auto& location = RoutineData::kLocations[prepared.destination];
    Check(Within(prepared.definition.spawn, prepared.centre, .001f), "definition spawn is the validated point");
    Check(prepared.wanderRadius == location.wanderRadius, "normal wander radius belongs to the selected destination");
    Check(prepared.definition.searchRadius == Tune::kReAggroSightDist, "AI sight radius remains separate from wandering");
    Check(std::strcmp(prepared.definition.targetDesc, RoutinePlan::OccupationName(prepared.plan.occupation)) == 0,
        "definition occupation matches the immutable clue plan");
    Check(prepared.definition.behavior == &kHumanTarget && prepared.definition.onCleanup == ResetRoutine,
        "prepared target retains combat bridge and explicit local cleanup hook");
    Check(prepared.definition.models.list && prepared.definition.models.count > 0, "prepared definition retains stable model storage");

    R = prepared;
    const ContractDef* stable = &R.definition;
    Check(IsRoutine(R.definition) && !IsRoutine(prepared.definition), "only the published stable definition owns the routine");
    prepared = {};
    Check(stable == &R.definition && IsRoutine(*stable) && stable->name && stable->models.list[0],
        "discarding provisional storage does not invalidate active definition data");
    const auto clues = RoutinePlan::CardLines(R.plan);
    Check(!clues[0].empty() && !clues[5].empty(), "published route supports every habit card line");
}

static void TestPreparationFailureAndFallback()
{
    const RoutineRuntime previous = R;
    for (int failure = 0; failure < 5; ++failure)
    {
        w = {}; std::srand(23);
        if (failure == 0) w.collision = false;
        if (failure == 1) w.nav = false;
        if (failure == 2) w.outside = false;
        if (failure == 3) w.occupied = true;
        if (failure == 4) w.cancelAfter = 0;
        RoutineRuntime rejected;
        Check(!PrepareRoutineContract(rejected) && !rejected.enabled, "failed validation never publishes an enabled candidate");
        Check(R.enabled == previous.enabled && R.destination == previous.destination &&
            Within(R.centre, previous.centre, .001f) && R.definition.models.list == previous.definition.models.list &&
            R.plan.seed == previous.plan.seed, "failed replacement preparation leaves old runtime intact");
        Check(w.waits <= 376, "two-location startup has a bounded streaming budget");
        Check(w.starts == w.stops, "failed startup releases only its successful scene requests");
        Check(w.wanderCalls == 0, "preparation does not task any ped");
    }
    w = {}; std::srand(23); w.failFirstRequests = 1;
    RoutineRuntime fallback;
    Check(PrepareRoutineContract(fallback), "unusable preferred location permits the all-day fallback");
    Check(RoutineData::kLocations[fallback.destination].kind == RoutineData::PlaceKind::Rest,
        "bounded alternate is the declared overnight/public fallback");
    Check(w.requests == 2 && w.safeCalls <= 10, "startup tries at most two locations and five projected candidates each");
}

static void TestDeploymentAndWander()
{
    R = MakePrepared();
    w.requests = 1;
    Check(ValidateRoutineDeployment(77, R.definition) && w.lastIgnored == 77,
        "deployment revalidates prepared location while ignoring its own provisional ped");
    w.pedPosition = R.definition.spawn;
    Check(ValidateRoutinePlacement(77, R.definition), "actual ped placement confirms the selected ground point");
    w.pedPosition.x += 2;
    Check(!ValidateRoutinePlacement(77, R.definition), "native placement drift never reveals a misplaced target");
    w.pedPosition = R.definition.spawn;
    w.collision = false;
    Check(!ValidateRoutinePlacement(77, R.definition), "collision disappearing after placement rejects deployment");
    w.collision = true;
    w.canInteract = false;
    Check(!ValidateRoutineDeployment(77, R.definition), "suspension prevents revealing the target");
    w.canInteract = true; w.outside = false;
    Check(!ValidateRoutineDeployment(77, R.definition), "changed collision/interior status rejects deployment");
    w.outside = true; w.safeDx = 2;
    Check(!ValidateRoutineDeployment(77, R.definition), "nav projection drifting from prepared point rejects deployment");
    w.safeDx = 0;
    const auto& place = RoutineData::kLocations[R.destination];
    Check(place.openMinute != place.closeMinute, "daytime fixture selected a closing destination");
    w.minute = place.closeMinute;
    Check(!ValidateRoutineDeployment(77, R.definition), "venue closing during capture rejects deployment");
    ContractDef unrelated{};
    Check(ValidateRoutineDeployment(77, unrelated), "legacy definitions bypass routine-specific deployment checks");
    Check(ValidateRoutinePlacement(77, unrelated), "legacy definitions bypass routine-specific placement checks");

    w = {};
    const Vector3 fixed = R.centre;
    Check(StartRoutineWander(77, R.definition), "active definition starts normal wandering");
    Check(w.wanderedPed == 77 && Within(w.wanderCentre, fixed, .001f) && w.wanderRadius == R.wanderRadius,
        "wander starts around fixed validated centre with its own radius");
    Check(w.avoidRadius == R.wanderRadius && !w.mayEnterWater && w.keepCalls == 1,
        "wander applies water path policy and keeps the task once");
    R.definition.spawn = {0, 0, 0};
    Check(StartRoutineWander(77, R.definition) && Within(w.wanderCentre, fixed, .001f),
        "fixed activity centre never follows unrelated spawn/position changes");
    const int issued = w.wanderCalls;
    Check(!StartRoutineWander(77, unrelated) && w.wanderCalls == issued,
        "ordinary target definition cannot issue routine tasks");

    const auto cleanup = R.definition.onCleanup;
    cleanup();
    Check(!R.enabled && R.destination == -1 && !IsRoutine(R.definition) && R.plan.townIndex == -1,
        "cleanup retires plan, identity and destination");
    Check(w.wanderCalls == issued && !StartRoutineWander(77, R.definition),
        "cleanup touches no game task or borrowed resource and disables later wander");
    cleanup();
    Check(!R.enabled && w.wanderCalls == issued, "routine cleanup is idempotent");
}

int main()
{
    TestPreparedDefinition();
    TestPreparationFailureAndFallback();
    TestDeploymentAndWander();
    std::printf("Routine runtime bridge: %u checks passed.\n", checks);
}
