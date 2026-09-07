#include <array>
#include <algorithm>
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
using Interior = int;
using Any = std::uint64_t;
struct Vector3
{
    float x, y, z;
    Vector3(float a = 0, float b = 0, float c = 0) : x(a), y(b), z(c) {}
};
static unsigned checks = 0;
static ULONGLONG pausedDurationMs = 0;
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
static float DistSq(Vector3 a, Vector3 b)
{
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
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
static Hash joaat(const char* text) { return Joaat(text); }
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

struct ScenarioFixture
{
    int id;
    Hash hash;
    Vector3 position;
    bool exists = true, active = true, compatible = true;
    Ped user = 0;
};
struct World
{
    unsigned now = 1000, waits = 0, cancelAfter = 999999;
    unsigned loadedAfter = 0, targetDiesAfter = 999999, settleAfterWaits = 999999, ownershipLostAfter = 999999;
    unsigned interactionStopsAfter = 999999;
    int minute = 720, clockRate = 2000, taskStatus = 0;
    bool canInteract = true, collision = true, nav = true, safe = true;
    bool outside = true, groundOk = true, occupied = false, hit = false, waterPresent = false;
    bool sceneActive = false, startOk = true;
    bool rejectPreparedCandidate = false;
    int rejectInitialCandidates = 0, rejectTown = -1;
    std::vector<const char*> attemptedLocations;
    int interior = 0, probeStatus = 2, starts = 0, stops = 0, safeCalls = 0;
    int requests = 0, failFirstSafeCalls = 0, probes = 0;
    int placementCalls = 0;
    bool placementResult = false;
    float safeDx = 0, groundDz = 0;
    Ped lastIgnored = -1, wanderedPed = 0;
    Vector3 wanderCentre{};
    Vector3 pedPosition{}, settledPosition{};
    Vector3 travelCentre{}, occupiedAt{};
    bool occupiedNear = false;
    bool occupiedExact = false;
    float wanderRadius = 0, avoidRadius = 0;
    int wanderCalls = 0, keepCalls = 0, avoidCalls = 0, waterCalls = 0;
    int travelCalls = 0, standCalls = 0, statusCalls = 0;
    int activityCalls = 0, scenarioReads = 0, scenarioExits = 0, occupancyReads = 0;
    bool scenarioExitPending = false;
    int travelTimeout = 0;
    Hash lastTaskHash = 0, activeTaskHash = 0;
    Hash scenarioInUse = 0;
    bool mayEnterWater = true;
    std::vector<ScenarioFixture> points;
    int selectedPoint = 0, pointTasks = 0, clears = 0, pointQueries = 0;
    bool scenarioBase = false, scenarioTask = false, exiting = false, stallExit = false;
    bool interiorReady = true;
    bool scenarioActive = false;
} w;
static ScenarioFixture* Scenario(int id)
{
    for (auto& point : w.points) if (point.id == id) return &point;
    return nullptr;
}
static struct OwnedPedFixture { Ped ped = 77; } ownedPed;
static bool OwnedPedIdentityMatches() { return w.waits < w.ownershipLostAfter; }
static ULONGLONG RuntimeNowMs() { return w.now; }
static ULONGLONG GetTickCount64() { return w.now; }
static bool PlayerAvailable() { return w.waits < w.cancelAfter; }
static bool LivingPed(Ped ped) { return ped != 0 && w.waits < w.targetDiesAfter; }
static bool CanStartInteraction() { return PlayerAvailable() && w.canInteract && w.waits < w.interactionStopsAfter; }
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
static int GET_MILLISECONDS_PER_GAME_MINUTE() { return w.clockRate; }
}
namespace ENTITY
{
static bool HAS_COLLISION_LOADED_AROUND_POSITION(Vector3) { return w.collision && w.now >= w.loadedAfter; }
static Vector3 GET_ENTITY_COORDS(Ped, bool, bool) { return w.waits >= w.settleAfterWaits ? w.settledPosition : w.pedPosition; }
static bool PLACE_ENTITY_ON_GROUND_PROPERLY(Ped ped, bool flag)
{
    Check(ped == 77 && flag, "settling retains the existing placement native parameters");
    ++w.placementCalls; return w.placementResult;
}
}
namespace PATH
{
static bool IS_NAVMESH_LOADED_IN_AREA(Vector3, Vector3) { return w.nav && w.now >= w.loadedAfter; }
static bool GET_SAFE_COORD_FOR_PED(Vector3 position, bool, Vector3* result, int)
{
    ++w.safeCalls; *result = position; result->x += w.safeDx;
    return w.safe && w.safeCalls > w.failFirstSafeCalls;
}
static void ADD_NAVMESH_REQUIRED_REGION(float, float, float) {}
}
namespace MISC
{
static bool GET_GROUND_Z_AND_NORMAL_FOR_3D_COORD(Vector3 query, float* ground, Vector3* normal)
{
    // Catalogue ground fixture: every sourced anchor uses a 2.5 m height tolerance,
    // and validation queries 1 m above that bound. Geometry never depends on a
    // preceding candidate-finder call, which need not run during revalidation.
    *ground = query.z - 3.5f + w.groundDz; *normal = {0, 0, 1}; return w.groundOk;
}
static bool IS_POSITION_OCCUPIED(Vector3 point, float, bool, bool, bool, bool, bool, Ped ignore, bool)
{
    ++w.occupancyReads;
    w.lastIgnored = ignore; return w.occupied || w.rejectPreparedCandidate ||
        (w.occupiedNear && Within(point, w.occupiedAt, 20.0f)) || (w.occupiedExact && Within(point, w.occupiedAt, .1f));
}
}
namespace INTERIOR
{
static bool IS_COLLISION_MARKED_OUTSIDE(Vector3) { return w.outside; }
static int GET_INTERIOR_FROM_COLLISION(Vector3) { return w.interior; }
static int GET_INTERIOR_AT_COORDS(Vector3) { return w.interior; }
static bool IS_INTERIOR_READY(Interior) { return w.interiorReady; }
}
namespace WATER { static bool GET_WATER_HEIGHT(Vector3 point, float* height) { *height = point.z; return w.waterPresent; } }
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
static int GET_SCENARIO_POINTS_IN_AREA(Vector3, float, Any* points, int capacity)
{
    ++w.pointQueries;
    Check(points[0] == static_cast<Any>(capacity), "scenario array has the RAGE capacity header");
    const int count = (std::min)(capacity, static_cast<int>(w.points.size()));
    for (int index = 0; index < count; ++index) points[index + 1] = w.points[index].id;
    return count;
}
static bool DOES_SCENARIO_POINT_EXIST(int id) { const auto p = Scenario(id); return p && p->exists; }
static Hash _GET_SCENARIO_POINT_TYPE(int id) { const auto p = Scenario(id); return p ? p->hash : 0; }
static bool _IS_SCENARIO_POINT_ACTIVE(int id) { const auto p = Scenario(id); return p && p->active; }
static bool _IS_SCENARIO_IN_USE(int id) { const auto p = Scenario(id); return p && p->user != 0; }
static Ped _GET_PED_USING_SCENARIO_POINT(int id) { const auto p = Scenario(id); return p ? p->user : 0; }
static Vector3 _GET_SCENARIO_POINT_COORDS(int id, bool) { const auto p = Scenario(id); return p ? p->position : Vector3{}; }
static bool IS_SCENARIO_TYPE_ENABLED(const char*) { return true; }
static bool PED_HAS_USE_SCENARIO_TASK(Ped) { return w.scenarioTask; }
static bool _PED_IS_IN_SCENARIO_BASE(Ped) { return w.scenarioBase; }
static bool IS_PED_ACTIVE_IN_SCENARIO(Ped, int) { return w.scenarioActive; }
static bool IS_PED_EXITING_SCENARIO(Ped, bool) { return w.exiting; }
static void TASK_USE_SCENARIO_POINT(Ped ped, int id, const char* clip, int duration, bool enter, bool warp,
    Hash conditional, bool p7, float p8, bool p9)
{
    Check(ped == 77 && !clip && duration == -1 && enter && !warp && conditional == 0 && !p7 && p8 == -1 && !p9,
        "scenario task borrows the owned ped with normal entry and no warp");
    ++w.pointTasks; ++w.activityCalls; w.selectedPoint = id; w.scenarioTask = true;
    if (auto point = Scenario(id)) point->user = ped;
}
static void CLEAR_PED_TASKS(Ped ped, bool p1, bool p2)
{
    Check(ped == 77 && p1 && !p2, "routine exit recovery uses only ordinary task clearing");
    ++w.clears;
    if (!w.stallExit)
    {
        if (auto point = Scenario(w.selectedPoint)) point->user = 0;
        w.scenarioInUse = 0; w.selectedPoint = 0; w.scenarioTask = w.scenarioBase = w.scenarioActive = w.exiting = false;
        w.taskStatus = 7;
    }
}
static void SET_PED_PATH_PREFER_TO_AVOID_WATER(Ped, bool avoid, float radius)
{
    Check(avoid, "routine path prefers avoiding water"); ++w.avoidCalls; w.avoidRadius = radius;
}
static void SET_PED_PATH_MAY_ENTER_WATER(Ped, bool value) { ++w.waterCalls; w.mayEnterWater = value; }
static void TASK_WANDER_IN_AREA(Ped ped, Vector3 centre, float radius, float, float, int)
{
    ++w.wanderCalls; w.wanderedPed = ped; w.wanderCentre = centre; w.wanderRadius = radius;
    w.activeTaskHash = Joaat("SCRIPT_TASK_WANDER_IN_AREA");
}
static int GET_SCRIPT_TASK_STATUS(Ped, Hash hash, bool)
{
    ++w.statusCalls; w.lastTaskHash = hash;
    return hash == w.activeTaskHash ? w.taskStatus : 7;
}
static void TASK_FOLLOW_NAV_MESH_TO_COORD(Ped, Vector3 centre, float speed, int timeout, float range, int flags, float heading)
{
    Check(speed == 1.0f && range == 2.0f && flags == 0 && heading == 40000.0f,
        "travel uses verified walking flags with bounded target range");
    if (w.scenarioExitPending)
    {
        Check(w.scenarioExits > 0, "routine requests scenario exit before scheduled travel");
        w.scenarioExitPending = false;
    }
    ++w.travelCalls; w.travelCentre = centre; w.travelTimeout = timeout;
    w.activeTaskHash = Joaat("SCRIPT_TASK_FOLLOW_NAV_MESH_TO_COORD");
}
[[maybe_unused]] static void TASK_STAND_STILL(Ped, int duration)
{
    if (w.scenarioExitPending)
    {
        Check(w.scenarioExits > 0, "routine requests scenario exit before waiting");
        w.scenarioExitPending = false;
    }
    Check(duration == -1, "waiting persists until a later explicit routine decision"); ++w.standCalls;
}
[[maybe_unused]] static void TASK_START_SCENARIO_IN_PLACE_HASH(Ped, Hash, int, bool, Hash, float, bool)
{
    ++w.activityCalls;
    Check(false, "routine must never force an in-place scenario");
}
}
namespace PED
{
static void SET_PED_SHOULD_PLAY_NORMAL_SCENARIO_EXIT(Ped ped)
{
    Check(ped == 77, "normal scenario exit applies only to the owned target");
    ++w.scenarioExits;
}
static void SET_PED_KEEP_TASK(Ped, bool keep) { Check(keep, "routine task is kept"); ++w.keepCalls; }
static bool IS_PED_USING_ANY_SCENARIO(Ped ped)
{
    Check(ped == 77, "ambient scenario observation examines the existing target"); ++w.scenarioReads;
    return w.scenarioInUse != 0;
}
static bool IS_PED_USING_THIS_SCENARIO(Ped, int id) { return id > 0 && w.selectedPoint == id; }
static bool IS_PED_USING_SCENARIO_HASH(Ped, Hash hash) { return w.scenarioInUse == hash; }
static bool _CAN_PED_USE_SCENARIO_POINT(Ped, int id, int, int, int)
{ const auto point = Scenario(id); return point && point->compatible; }
}

#include "../rdr2 scripting environment/samples/Pools/routine_runtime.h"

static void ObservePreparation(const StartupTrace::Event& event)
{
    if (std::strcmp(event.stage, "candidate_prepare_begin") != 0) return;
    for (const char* previous : w.attemptedLocations)
        Check(std::strcmp(previous, event.detail) != 0, "each startup pass tries an authored location at most once");
    w.attemptedLocations.push_back(event.detail);
    w.rejectPreparedCandidate = w.attemptedLocations.size() <= static_cast<std::size_t>(w.rejectInitialCandidates);
    for (const auto& location : RoutineData::kLocations)
        if (std::strcmp(location.id, event.detail) == 0 && static_cast<int>(location.town) == w.rejectTown)
            w.rejectPreparedCandidate = true;
}

static void CheckPreparedCard(const RoutineRuntime& prepared)
{
    Check(prepared.enabled && RoutinePlan::Valid(prepared.plan), "accepted alternate retains a complete compatible routine plan");
    const auto& location = RoutineData::kLocations[prepared.destination];
    const int phase = static_cast<int>(location.kind);
    constexpr int cardRowForPhase[] = {1, 3, 4, 5, 2};
    Check(prepared.plan.route[phase] == prepared.destination &&
        prepared.cardLines[cardRowForPhase[phase]].find(RoutinePlan::CardLocationName(location)) != std::string::npos,
        "accepted alternate is inserted into its matching advertised card habit before publication");
    Check(location.town == RoutineData::kTowns[prepared.plan.townIndex].id &&
        prepared.definition.models.list == RoutineModels(prepared.plan.townIndex, prepared.plan.occupation).list &&
        std::strcmp(prepared.definition.targetDesc, RoutinePlan::OccupationName(prepared.plan.occupation)) == 0,
        "alternate town, occupation and model pool all agree with the prepared identity");
    Check(prepared.definition.searchRadius == location.wanderRadius && prepared.wanderRadius == RoutineData::kWanderRadius,
        "the prepared search area matches the shared 45-metre wander radius");
}

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
    Check(w.requests == 0 && w.starts == 0, "already loaded candidates never issue an unnecessary streaming request");
    const auto& location = RoutineData::kLocations[prepared.destination];
    Check(Within(prepared.definition.spawn, prepared.centre, .001f), "definition spawn is the validated point");
    Check(prepared.wanderRadius == location.wanderRadius, "normal wander radius belongs to the selected destination");
    Check(prepared.fallbackDestination == prepared.destination && Within(prepared.fallbackCentre, prepared.centre, .001f) &&
        !prepared.ambientFallback, "accepted spawn caches its authored area for ambient continuation");
    Check(prepared.definition.searchRadius == prepared.wanderRadius && prepared.wanderRadius == RoutineData::kWanderRadius,
        "prepared investigation radius matches the uniform destination wander radius");
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

static void TestGeneratedTownModels()
{
    for (int town = 0; town < RoutineData::kTownCount; ++town)
        for (const unsigned seed : {0u, 256u})
        {
            const unsigned occupation = RoutinePlan::GeneratedOccupation(town, seed);
            RoutinePlan::Plan plan;
            Check(RoutinePlan::Build(plan, town, occupation, seed),
                "each generated town and occupation has a complete compatible route");
            const ModelSet models = RoutineModels(town, occupation);
            Check(models.list && models.count > 0 && models.list[0] != 0,
                "every generated town supplies a nonempty target model set");
            Check(RoutineModels(town, occupation).list == models.list,
                "selected town models retain stable storage after preparation");
            switch (RoutineData::kTowns[town].id)
            {
            case RoutineData::TownId::VanHorn:
                Check(occupation == RoutineData::DockWorker && models.list[0] == Joaat("a_m_m_vhtboatcrew_01"),
                    "Van Horn dock-worker clues use its verified boat-crew archetype");
                break;
            case RoutineData::TownId::Annesburg:
                Check(occupation == RoutineData::Laborer && models.list[0] == Joaat("a_m_m_asbtownfolk_01_laborer"),
                    "Annesburg laborer clues use its verified local laborer archetype");
                break;
            case RoutineData::TownId::SaintDenis:
                Check(occupation == RoutineData::Laborer
                    ? models.list[0] == Joaat("a_m_m_sdlaborers_02") && models.list != SD_DOCK
                    : occupation == RoutineData::DockWorker && models.list == SD_DOCK,
                    "Saint Denis laborer and dock-worker clues select their respective verified model pools");
                break;
            default: break;
            }
        }
    Check(!RoutineModels(-1, RoutineData::Laborer).list &&
        RoutineModels(RoutineData::kTownCount, RoutineData::Laborer).count == 0,
        "invalid town indices cannot silently substitute a Saint Denis model");
}

static void TestPreparationFailureAndFallback()
{
    StartupTrace::sink = ObservePreparation;
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
        Check(w.waits <= 376 && w.attemptedLocations.size() <= 8,
            "startup fallback retains a bounded streaming and candidate-attempt budget");
        Check(w.starts == w.stops, "failed startup releases only its successful scene requests");
        Check(w.wanderCalls == 0, "preparation does not task any ped");
    }
    w = {}; std::srand(23); w.failFirstSafeCalls = 5;
    RoutineRuntime fallback;
    Check(PrepareRoutineContract(fallback), "unusable preferred location permits the all-day fallback");
    Check(RoutineData::kLocations[fallback.destination].kind == RoutineData::PlaceKind::Rest,
        "bounded alternate is the declared overnight/public fallback");
    Check(w.requests == 0 && w.safeCalls == 6, "five failed preferred candidates permit one validated fallback without unneeded streaming");
    CheckPreparedCard(fallback);

    w = {}; std::srand(23); w.rejectInitialCandidates = 2;
    RoutineRuntime alternate;
    Check(PrepareRoutineContract(alternate) && w.attemptedLocations.size() == 3,
        "failure of both selected stops continues to another compatible authored location");
    Check(alternate.plan.townIndex == fallback.plan.townIndex,
        "startup exhausts compatible choices in its original town before moving to another town");
    CheckPreparedCard(alternate);

    w = {}; std::srand(23); w.rejectTown = static_cast<int>(RoutineData::kTowns[fallback.plan.townIndex].id);
    RoutineRuntime otherTown;
    Check(PrepareRoutineContract(otherTown) && otherTown.plan.townIndex != fallback.plan.townIndex,
        "an unavailable starting town permits a complete compatible plan in the next town");
    CheckPreparedCard(otherTown);

    w = {}; std::srand(23); w.occupied = true;
    RoutineRuntime untouched = alternate;
    Check(!PrepareRoutineContract(untouched) && w.attemptedLocations.size() == 8 && w.safeCalls == 40 &&
        std::strcmp(RoutineSpawn::diagnostic.check, "candidate_budget_exhausted") == 0,
        "all occupied authored sites end this pass after eight finite five-point searches");
    Check(untouched.destination == alternate.destination && untouched.cardLines == alternate.cardLines &&
        R.destination == previous.destination && R.cardLines == previous.cardLines && R.plan.seed == previous.plan.seed,
        "exhausted startup leaves both its caller's prepared data and the existing live contract untouched");

    w = {}; std::srand(23); w.canInteract = false;
    RoutineRuntime interrupted;
    Check(!PrepareRoutineContract(interrupted) && w.attemptedLocations.empty() &&
        std::strcmp(RoutineSpawn::diagnostic.check, "interaction_interrupted") == 0,
        "pause, fade, mount or combat before preparation stops all candidate work");
    w = {}; std::srand(23); w.loadedAfter = 1100; w.interactionStopsAfter = 1;
    Check(!PrepareRoutineContract(interrupted) && !interrupted.enabled && w.attemptedLocations.size() == 1 &&
        w.starts == 1 && w.stops == 1 && std::strcmp(RoutineSpawn::diagnostic.check, "interaction_interrupted") == 0,
        "an interaction interrupted while streaming cannot publish a candidate or continue to another site");
    StartupTrace::sink = nullptr;
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
    const int safeCalls = w.safeCalls;
    Check(ValidateRoutineDeployment(77, R.definition) && w.safeCalls == safeCalls,
        "non-idempotent candidate projection cannot reject an unchanged valid saved destination");
    w.safe = false;
    Check(ValidateRoutineDeployment(77, R.definition) && w.safeCalls == safeCalls,
        "saved point geometry is revalidated without requiring another candidate-finder success");
    w.safe = true;
    w.safeDx = 0;
    const auto& place = RoutineData::kLocations[R.destination];
    Check(place.openMinute != place.closeMinute, "daytime fixture selected a closing destination");
    w.minute = place.closeMinute;
    Check(!ValidateRoutineDeployment(77, R.definition), "venue closing during capture rejects deployment");
    ContractDef unrelated{};
    Check(ValidateRoutineDeployment(77, unrelated), "legacy definitions bypass routine-specific deployment checks");
    Check(ValidateRoutinePlacement(77, unrelated), "legacy definitions bypass routine-specific placement checks");

    w = {}; w.requests = 1; w.loadedAfter = 1100;
    Check(ValidateRoutineDeployment(77, R.definition) && w.waits > 0 && w.starts == 1 && w.stops == 1,
        "postcapture residency loss gets one bounded owned-scene reload");
    w = {}; w.requests = 1; w.loadedAfter = 1100; w.targetDiesAfter = 1;
    Check(!ValidateRoutineDeployment(77, R.definition) && w.waits > 0 && w.stops == 1,
        "target death during collision reload prevents deployment after yielding");
    w = {}; w.collision = false;
    Check(!ValidateRoutineDeployment(77, R.definition) && w.waits <= 188 && w.stops == 1,
        "persistent postcapture streaming failure stays hidden and releases loader within its deadline");

    w = {};
    Check(StartRoutineWander(77, R.definition) && R.resumeRequested && w.wanderCalls == 0,
        "combat recovery requests fresh selection without an obsolete wander task");
    Check(!StartRoutineWander(77, unrelated) && w.wanderCalls == 0,
        "ordinary target definition cannot request routine tasks");

    const auto cleanup = R.definition.onCleanup;
    cleanup();
    Check(!R.enabled && R.destination == -1 && !IsRoutine(R.definition) && R.plan.townIndex == -1,
        "cleanup retires plan, identity and destination");
    Check(w.wanderCalls == 0 && !StartRoutineWander(77, R.definition) && !R.resumeRequested && !R.selectPending,
        "cleanup touches no game task or borrowed resource and disables later wander");
    cleanup();
    Check(!R.enabled && w.wanderCalls == 0, "routine cleanup is idempotent");
}

static void PlacementFixture()
{
    R = MakePrepared();
    w = {};
    w.pedPosition = R.definition.spawn;
    w.pedPosition.z += 1.0f;
    w.settledPosition = w.pedPosition;
    ownedPed.ped = 77;
    ResetRoutineStartDiagnostic();
}
static void TestPedOriginAndBoundedPlacement()
{
    PlacementFixture();
    Check(!Within(w.pedPosition, R.definition.spawn, .75f),
        "regression control: old spherical check rejects a normal standing origin one metre above ground");
    Check(ValidateRoutinePlacement(77, R.definition),
        "separate horizontal and vertical tolerances accept a standing ped above validated foot-level ground");
    const Vector3 invalidOffsets[] = {{.8f, 0, 1}, {0, .8f, 1}, {0, 0, 2.1f}, {0, 0, -.26f}};
    for (const auto& offset : invalidOffsets)
    {
        w.pedPosition = {R.definition.spawn.x + offset.x, R.definition.spawn.y + offset.y, R.definition.spawn.z + offset.z};
        Check(!ValidateRoutinePlacement(77, R.definition) &&
            std::strcmp(RoutineSpawn::diagnostic.check, "ped_position_out_of_bounds") == 0,
            "horizontal drift, elevated roof-level origin and under-floor placement remain rejected");
    }
    PlacementFixture(); w.groundDz = .5f;
    Check(!ValidateRoutinePlacement(77, R.definition),
        "acceptable ped origin does not bypass changed surface geometry at its actual horizontal position");
    PlacementFixture(); w.occupied = true;
    Check(!ValidateRoutinePlacement(77, R.definition),
        "acceptable ped origin does not bypass occupied ground-space validation");

    PlacementFixture(); // Placement native defaults to false while geometry is correct.
    Check(WaitForRoutinePlacement(77, R.definition) && w.placementCalls == 1 && w.waits == 0 &&
        routineStartDiagnostic.placementAttempts == 1 && !routineStartDiagnostic.placementResult,
        "false placement-native return is diagnostic only when actual settled geometry is valid");
    PlacementFixture();
    w.pedPosition.x += 2; w.settleAfterWaits = 1;
    Check(WaitForRoutinePlacement(77, R.definition) && w.waits > 0 && w.placementCalls == 2 &&
        w.now >= 1100 && w.now < 1200,
        "placement becoming valid after a frame succeeds on the next rate-limited settling attempt");
    PlacementFixture();
    w.pedPosition.x += 2; w.placementResult = true;
    const unsigned start = w.now;
    Check(!WaitForRoutinePlacement(77, R.definition) && w.now - start >= 1500 && w.now - start <= 1516 &&
        w.placementCalls > 1 && w.placementCalls <= 16,
        "successful native returns cannot accept persistent bad geometry and settling stops within its finite deadline");
    PlacementFixture(); w.pedPosition.x += 2; w.cancelAfter = 1;
    Check(!WaitForRoutinePlacement(77, R.definition) && w.waits == 1 && w.placementCalls == 1,
        "player loss aborts settling after a yield without another placement attempt");
    PlacementFixture(); w.pedPosition.x += 2; w.targetDiesAfter = 1;
    Check(!WaitForRoutinePlacement(77, R.definition) && w.waits == 1 && w.placementCalls == 1 &&
        std::strcmp(RoutineSpawn::diagnostic.check, "interaction_interrupted") == 0,
        "subject loss aborts settling after a yield without attempting to place a dead target");
    PlacementFixture(); w.pedPosition.x += 2; w.ownershipLostAfter = 1;
    Check(!WaitForRoutinePlacement(77, R.definition) && w.waits == 1 && w.placementCalls == 1 &&
        std::strcmp(RoutineSpawn::diagnostic.check, "subject_ownership_changed") == 0,
        "ownership changing across a yield stops all further placement calls on the borrowed or recycled handle");
    PlacementFixture(); ownedPed.ped = 88;
    Check(!WaitForRoutinePlacement(77, R.definition) && w.placementCalls == 0 &&
        std::strcmp(RoutineSpawn::diagnostic.check, "subject_ownership_changed") == 0,
        "a ped handle different from the owned subject never receives a placement task");
    PlacementFixture(); w.canInteract = false;
    Check(!WaitForRoutinePlacement(77, R.definition) && w.placementCalls == 0 && w.waits == 0,
        "an interrupted interaction never starts a settling native");
}

static void SetDaytimeFixture()
{
    ResetRoutine(); w = {}; pausedDurationMs = 0;
    Check(RoutinePlan::Build(R.plan, 2, RoutineData::LivestockHand, 29), "fixture creates a real compatible town route");
    R.plan.offsetMinutes = 0;
    R.enabled = true;
    R.destination = R.plan.route[0];
    const auto& place = RoutineData::kLocations[R.destination];
    R.centre = place.anchor; R.wanderRadius = place.wanderRadius;
    R.fallbackDestination = R.destination; R.fallbackCentre = R.centre;
    R.definition = {"Valentine", "Livestock hand", "Valentine", R.centre, RoutineData::kWanderRadius,
        RoutineModels(2, RoutineData::LivestockHand), &kHumanTarget, nullptr, ResetRoutine};
    w.pedPosition = R.centre; w.pedPosition.x += 90;
}
static void Tick(unsigned elapsed = 16, bool mayAct = true)
{
    w.now += elapsed; UpdateRoutine(77, R.definition, mayAct);
}
static int TaskCount() { return w.travelCalls + w.wanderCalls + w.standCalls + w.activityCalls; }
static void BeginTravel()
{
    Check(StartRoutineWander(77, R.definition), "routine setup requests its first schedule");
    Tick();
    Check(R.selectPending && TaskCount() == 0, "resume first reselects before issuing a task");
    Tick();
    Check(R.controller.state == Routine::State::Travelling && w.travelCalls == 1,
        "validated distant destination produces one physical walking task");
}
static void TestTravelAndClock()
{
    SetDaytimeFixture(); BeginTravel();
    const Vector3 fixed = R.centre;
    Check(Within(w.travelCentre, fixed, .001f) && w.travelTimeout == 300000 && !w.mayEnterWater,
        "travel targets fixed activity centre with finite deadline and water path policy");
    for (int frame = 0; frame < 20; ++frame) Tick();
    Check(w.travelCalls == 1 && w.activeTaskHash == Joaat("SCRIPT_TASK_FOLLOW_NAV_MESH_TO_COORD"),
        "active navmesh task is observed without per-frame reissue");
    w.taskStatus = 1;
    Tick(100);
    Check(w.travelCalls == 1, "both native status zero and one count as active travel");
    w.taskStatus = 0;
    w.pedPosition = fixed;
    Tick();
    Check(R.controller.state == Routine::State::Wandering && w.wanderCalls == 1 && Within(w.wanderCentre, fixed, .001f),
        "arrival switches once to wandering around the validated destination");
    w.pedPosition.x += 10;
    for (int frame = 0; frame < 20; ++frame) Tick();
    Check(w.wanderCalls == 1 && Within(R.centre, fixed, .001f) && w.activeTaskHash == Joaat("SCRIPT_TASK_WANDER_IN_AREA"),
        "wandering does not move the centre or repeat a running task");
    w.minute = 1020;
    const int tasks = TaskCount();
    Tick();
    Check(R.selectPending && TaskCount() == tasks, "phase change first requests fresh location selection");
    Tick();
    Check(R.destination == R.plan.route[1] && w.travelCalls == 2, "afternoon physically travels to the advertised shop frontage");

    w.minute = 1200;
    Check(StartRoutineWander(77, R.definition), "post-search recovery requests current schedule");
    Tick(); Tick();
    Check(R.destination == R.plan.route[2], "resuming after time change chooses evening destination");
    const int beforeJump = TaskCount();
    w.minute = 240;
    Tick();
    Check(R.selectPending && TaskCount() == beforeJump, "time skip into the three-to-six rest window reselects without teleporting");
    Tick();
    Check(R.destination == R.plan.route[3], "overnight clock selects the advertised all-day fallback");

    SetDaytimeFixture(); BeginTravel();
    const int beforePause = TaskCount();
    pausedDurationMs += 45000;
    Tick();
    Check(R.selectPending && TaskCount() == beforePause,
        "pause or fade wholly skipped by the outer loop forces fresh selection before another task");
    Tick();
    Check(R.destination == R.plan.route[0] && w.travelCalls == 1 && R.pauseSnapshotMs == pausedDurationMs,
        "resumption publishes the new pause snapshot and preserves healthy current travel");
    Tick();
    Check(w.travelCalls == 1 && !R.selectPending, "unchanged pause duration does not retrigger resumption");

    w.pedPosition = R.centre; Tick();
    const Vector3 held = R.centre;
    const float radius = R.wanderRadius;
    const int safeCalls = w.safeCalls, beforeAmbientPause = TaskCount();
    w.scenarioInUse = Joaat("WORLD_HUMAN_SMOKE"); w.taskStatus = 7;
    w.occupied = true; w.hit = true; pausedDurationMs += 45000;
    Tick(); Tick();
    Check(TaskCount() == beforeAmbientPause && R.controller.state == Routine::State::Wandering &&
        Within(R.centre, held, .001f) && R.wanderRadius == radius && w.safeCalls == safeCalls && w.scenarioExits == 0,
        "pause while in a scenario preserves the exact destination and healthy ambient task without candidate projection");
}
static void BeginShopTravelFromAcceptedWork()
{
    SetDaytimeFixture(); BeginTravel();
    w.pedPosition = R.centre; Tick();
    const int held = R.destination;
    const Vector3 heldCentre = R.centre;
    w.minute = 1020;
    w.pedPosition = RoutineData::kLocations[R.plan.route[1]].anchor; w.pedPosition.x += 90;
    Tick(); Tick();
    Check(R.destination == R.plan.route[1] && R.controller.state == Routine::State::Travelling &&
        R.fallbackDestination == held && Within(R.fallbackCentre, heldCentre, .001f),
        "scheduled travel retains the previously accepted area until arrival");
}
static void TestArrivalAtWanderBoundary()
{
    BeginShopTravelFromAcceptedWork();
    const int held = R.fallbackDestination, arrived = R.destination;
    const Vector3 heldCentre = R.fallbackCentre, destinationCentre = R.centre;
    const int travelCalls = w.travelCalls, wanderCalls = w.wanderCalls;
    w.pedPosition = destinationCentre; w.pedPosition.x += 45.01f;
    Tick();
    Check(R.controller.state == Routine::State::Travelling && w.travelCalls == travelCalls &&
        w.wanderCalls == wanderCalls && R.fallbackDestination == held && Within(R.fallbackCentre, heldCentre, .001f),
        "just outside 45 metres continues travel and preserves the previous accepted area");
    w.pedPosition = destinationCentre; w.pedPosition.x += 45.0f;
    Tick();
    Check(R.controller.state == Routine::State::Wandering && w.wanderCalls == wanderCalls + 1 &&
        w.travelCalls == travelCalls && w.wanderRadius == 45.0f && Within(w.wanderCentre, destinationCentre, .001f),
        "entry at exactly 45 metres interrupts walking with one native wander task around the fixed destination");
    Check(R.fallbackDestination == arrived && Within(R.fallbackCentre, destinationCentre, .001f),
        "the newly arrived area is published in the same update that starts wandering");
    w.pedPosition.x += 1.0f;
    for (int frame = 0; frame < 20; ++frame) Tick();
    Check(w.wanderCalls == wanderCalls + 1 && w.travelCalls == travelCalls &&
        Within(R.centre, destinationCentre, .001f) && Within(R.fallbackCentre, destinationCentre, .001f),
        "healthy wandering crossing the boundary does not oscillate into travel or move the authored centre");

    SetDaytimeFixture();
    const int previous = R.fallbackDestination;
    w.minute = 1020;
    w.pedPosition = RoutineData::kLocations[R.plan.route[1]].anchor; w.pedPosition.x += 20.0f;
    Check(StartRoutineWander(77, R.definition), "nearby scheduled fixture requests route selection");
    Tick(); Tick();
    Check(R.destination == R.plan.route[1] && R.destination != previous &&
        R.controller.state == Routine::State::Wandering && w.travelCalls == 0 && w.wanderCalls == 1 &&
        R.fallbackDestination == R.destination && Within(w.wanderCentre, R.centre, .001f),
        "selection while already within the next area starts wandering without travelling to its centre");

    BeginShopTravelFromAcceptedWork();
    const int cached = R.fallbackDestination, beforeBlocked = TaskCount();
    const Vector3 cachedCentre = R.fallbackCentre;
    w.pedPosition = R.centre; w.pedPosition.x += 45.0f;
    Tick(16, false);
    Check(R.controller.state == Routine::State::Suspended && TaskCount() == beforeBlocked &&
        R.fallbackDestination == cached && Within(R.fallbackCentre, cachedCentre, .001f),
        "combat or restraint at the boundary cannot publish arrival or take over the ped");
    Tick();
    Check(R.selectPending && TaskCount() == beforeBlocked && R.fallbackDestination == cached,
        "priority release reselects the route before claiming arrival inside the area");
    Tick();
    Check(R.controller.state == Routine::State::Wandering && R.fallbackDestination == R.destination &&
        R.fallbackDestination != cached && w.wanderCalls == 2,
        "successful reselection after priority release starts area wandering once");

    BeginShopTravelFromAcceptedWork();
    const int retained = R.fallbackDestination;
    const Vector3 retainedCentre = R.fallbackCentre;
    w.pedPosition = R.centre; w.pedPosition.x += 45.0f;
    w.occupied = true; Tick(1000);
    Check(R.controller.state == Routine::State::Waiting && R.ambientFallback &&
        R.fallbackDestination == retained && Within(R.fallbackCentre, retainedCentre, .001f) &&
        Within(w.wanderCentre, retainedCentre, .001f),
        "an unavailable destination at the boundary resumes cached-area wandering without publishing false arrival");
    Tick();
    Check(R.ambientFallback && R.fallbackDestination == retained && Within(R.fallbackCentre, retainedCentre, .001f),
        "failed destination reselection keeps the last arrived area intact");
}
static void TestSuspensionAndAvailability()
{
    SetDaytimeFixture(); BeginTravel();
    const int tasks = TaskCount(), statusCalls = w.statusCalls;
    Tick(16, false); Tick(30000, false);
    Check(R.controller.state == Routine::State::Suspended && TaskCount() == tasks && w.statusCalls == statusCalls,
        "combat, search or restraint priority performs no routine task or task-status recovery");
    w.minute = 1020;
    Tick();
    Check(R.selectPending && TaskCount() == tasks, "resumption re-evaluates clock before taking control");
    Tick();
    Check(R.destination == R.plan.route[1], "resumed route uses current phase");
    const int resumedTasks = TaskCount();
    w.collision = false;
    const int requestCount = w.requests;
    Tick(); Tick();
    Check(TaskCount() == resumedTasks && R.controller.state == Routine::State::Suspended && w.requests == requestCount + 1,
        "unloaded target receives one rate-limited stream request and no movement task");
    Tick(1000);
    Check(w.requests == requestCount + 2 && TaskCount() == resumedTasks, "stream retry remains rate limited while navigation is unavailable");
    w.collision = true;
    Tick(); Tick();
    Check(R.controller.state != Routine::State::Suspended, "loaded target reselects and resumes without replacement");

    SetDaytimeFixture();
    w.minute = 1070;
    w.pedPosition = RoutineData::kLocations[R.plan.route[1]].anchor; w.pedPosition.x += 245;
    StartRoutineWander(77, R.definition); Tick(); Tick();
    Check(R.destination == R.plan.route[3], "travel estimate skips errands that cannot be reached before the evening boundary");
    SetDaytimeFixture();
    w.occupiedNear = true; w.occupiedAt = RoutineData::kLocations[R.plan.route[0]].anchor;
    StartRoutineWander(77, R.definition); Tick(); Tick();
    Check(R.destination == R.plan.route[3], "occupied work area falls back without clearing or claiming other actors");
    SetDaytimeFixture(); w.clockRate = 0;
    StartRoutineWander(77, R.definition); Tick(); Tick();
    Check(R.destination == R.plan.route[3], "unknown game-clock rate leaves only the all-day fallback eligible");
}
static void TestTravelRecovery()
{
    SetDaytimeFixture(); BeginTravel();
    const int failed = R.destination;
    w.taskStatus = 7;
    Tick(100); Tick(4100);
    Check(w.travelCalls == 2, "missing nav task receives the first delayed recovery");
    Tick(100); Tick(4100);
    Check(w.travelCalls == 3, "missing nav task receives only the second bounded recovery");
    Tick(100); Tick(4100);
    Check(w.travelCalls == 3 && R.selectPending && R.controller.IsCoolingDown(failed, w.now) && w.standCalls == 0 && w.wanderCalls == 1,
        "exhausted recovery resumes ambient wandering and cools down the failed travel location");
    Tick();
    Check(R.destination == R.plan.route[3], "failed destination is excluded from immediate reselection");

    SetDaytimeFixture(); BeginTravel();
    const int expired = R.destination;
    Tick(300000);
    Check(R.controller.IsCoolingDown(expired, w.now) && w.standCalls == 0 && w.wanderCalls == 1 && w.travelCalls == 1,
        "absolute travel deadline replaces a stuck travel task with cached-area wandering");
    ResetRoutine();
    Check(!R.controller.IsCoolingDown(expired, w.now) && !R.enabled && R.nextValidationMs == 0 && R.nextStreamRequestMs == 0,
        "cleanup resets cooldown, deferred selection and validation deadlines");
    const int count = TaskCount();
    UpdateRoutine(77, R.definition, true);
    Check(TaskCount() == count, "disabled runtime never revives a cleaned-up target");
}
static void TestTravelEstimateAndLongDeadline()
{
    SetDaytimeFixture();
    Check(RoutineTravelMinutes({}, {85, 0, 0}) == 24,
        "short-route ETA allows walking only the remaining 40 metres to the edge of the 45 metre area");
    Check(RoutineTravelMinutes({}, {45, 0, 0}) == 0 && RoutineTravelMinutes({}, {40, 0, 0}) == 0,
        "arrival at or inside the wander radius has no remaining travel allowance");
    Check(RoutineTravelMinutes({}, {}) == 0, "a zero-distance visit needs no travel allowance");
    w.minute = 240;
    w.pedPosition = RoutineData::kLocations[R.plan.route[3]].anchor;
    w.pedPosition.x += 500;
    BeginTravel();
    const auto estimate = Routine::TravelEstimateMs(std::sqrt(static_cast<double>(DistSq(w.pedPosition, R.centre))));
    Check(w.travelTimeout == static_cast<int>(estimate) && w.travelTimeout > 300000 &&
        R.controller.TripTimeoutMs() == estimate,
        "a long same-town route shares its walking estimate with both native and controller deadlines");
    const int eta = RoutineTravelMinutes(w.pedPosition, R.centre);
    const auto areaEstimate = Routine::TravelEstimateMs(std::sqrt(static_cast<double>(DistSq(w.pedPosition, R.centre))) - R.wanderRadius);
    Check(eta == static_cast<int>(std::ceil(static_cast<double>(areaEstimate) / w.clockRate)) && areaEstimate < estimate,
        "long-route ETA stops at area entry while the task keeps its conservative full-distance deadline");
    w.pedPosition = R.centre; w.pedPosition.x += 200;
    Tick(300000);
    Check(R.controller.state == Routine::State::Travelling && w.travelCalls == 1 && w.standCalls == 0,
        "progressing long travel survives 300 seconds without failure or restarting its task");
    w.pedPosition = R.centre; Tick();
    Check(R.controller.state == Routine::State::Wandering && w.wanderCalls == 1,
        "the admitted long route can arrive within its extended deadline");
}

static void TravelToLeisure(unsigned seed)
{
    SetDaytimeFixture();
    R.plan.seed = seed; w.minute = 1200;
    w.pedPosition = RoutineData::kLocations[R.plan.route[2]].anchor; w.pedPosition.x += 90;
    BeginTravel();
    Check(R.destination == R.plan.route[2], "ambient fixture physically travels to a real leisure destination");
    w.pedPosition = R.centre;
}
static void TestAmbientWanderingAndRecovery()
{
    for (unsigned seed : {28u, 29u, 30u})
    {
        TravelToLeisure(seed); Tick();
        Check(R.controller.state == Routine::State::Wandering && w.wanderCalls == 1 && w.activityCalls == 0,
            "every leisure seed starts area wandering without forcing smoking or drinking");
        const Vector3 fixed = R.centre;
        // The native area-wander task may pause at a world scenario and cease to
        // report its top-level task as active. Observe rather than replace it.
        w.taskStatus = 7;
        w.scenarioInUse = Joaat(seed == 28 ? "WORLD_HUMAN_SMOKE" : seed == 29 ? "WORLD_HUMAN_DRINKING" : "WORLD_HUMAN_STARE_STOIC");
        const int tasks = TaskCount();
        for (int frame = 0; frame < 20; ++frame) Tick(5000);
        Check(TaskCount() == tasks && w.scenarioReads >= 20 && Within(R.centre, fixed, .001f) &&
            R.controller.state == Routine::State::Wandering && !R.controller.IsCoolingDown(R.destination, w.now),
            "actual native scenarios remain uninterrupted beyond the former forced-activity duration");
        w.scenarioInUse = 0;
        Tick(16); Tick(1400);
        Check(TaskCount() == tasks, "ambient exit starts the normal missing-task grace without immediate retasking");
        Tick(100);
        Check(w.wanderCalls == 2 && w.activityCalls == 0 && R.controller.state == Routine::State::Wandering,
            "an ended ambient scenario with no surviving wander task receives one delayed recovery");
        w.taskStatus = 1;
        Tick(10000);
        Check(w.wanderCalls == 2, "a recovered native wander task is left running");
        for (int recovery = 0; recovery < 3; ++recovery)
        {
            w.taskStatus = 7; w.scenarioInUse = Joaat("WORLD_HUMAN_SMOKE"); Tick(5000);
            w.scenarioInUse = 0; Tick(); Tick(1500);
            Check(w.wanderCalls == 3 + recovery && w.standCalls == 0 && !R.controller.IsCoolingDown(R.destination, w.now),
                "independent ambient pauses regain wandering without exhausting a lifetime retry budget");
            w.taskStatus = 1; Tick();
        }
        ResetRoutine();
        Check(!R.enabled && w.activityCalls == 0,
            "cleanup retires ambient observation data without touching scenario props");
    }
    SetDaytimeFixture(); R.plan.seed = 30;
    BeginTravel(); w.pedPosition = R.centre; Tick();
    Check(w.activityCalls == 0 && w.wanderCalls == 1, "work destinations also delegate all local behavior to native wandering");

    w.scenarioInUse = Joaat("WORLD_HUMAN_SMOKE"); Tick(5000);
    const int tasks = TaskCount();
    w.scenarioInUse = 0; w.taskStatus = 0;
    Tick(10000);
    Check(TaskCount() == tasks, "ambient exit needs no recovery when native area wandering remains active");
}

static void TestAmbientPriorityAndTravelRecovery()
{
    TravelToLeisure(29); Tick();
    w.scenarioInUse = Joaat("WORLD_HUMAN_DRINKING"); w.taskStatus = 7;
    Tick(5000);
    const int tasks = TaskCount(), reads = w.scenarioReads;
    Tick(16, false); Tick(30000, false);
    Check(TaskCount() == tasks && w.scenarioReads == reads && R.controller.state == Routine::State::Suspended,
        "combat, search, restraint or handoff priority suspends without routine tasks or ambient reads");
    Tick();
    Check(R.activity.state == RoutineActivity::State::Exiting && TaskCount() == tasks,
        "post-priority resume waits for an observed scenario exit before current-time reselection");
    Tick();
    Check(R.selectPending && TaskCount() == tasks, "completed exit reselects before submitting movement");

    TravelToLeisure(28); Tick();
    w.scenarioInUse = Joaat("WORLD_HUMAN_SMOKE"); w.taskStatus = 7;
    Tick(5000); const int beforePhase = TaskCount();
    w.minute = 240; Tick();
    Check(R.activity.state == RoutineActivity::State::Exiting && TaskCount() == beforePhase,
        "a phase change requests exit without submitting movement in the exit frame");
    w.scenarioExitPending = true; Tick(); Tick();
    Check(R.destination == R.plan.route[3] && w.travelCalls == 2 && w.scenarioExits == 1 && !w.scenarioExitPending,
        "phase change requests a normal scenario exit before travelling to the new destination");
    Tick();
    Check(w.scenarioExits == 1, "scenario exit remains transition-only during travel");

    TravelToLeisure(28); Tick();
    w.scenarioInUse = Joaat("WORLD_HUMAN_SMOKE"); w.outside = false;
    Tick(1100);
    Check(R.controller.state == Routine::State::Wandering && w.standCalls == 0 && w.scenarioExits == 0 && w.wanderCalls == 1,
        "a transient exterior query at a held area leaves healthy ambient behavior uninterrupted");

    TravelToLeisure(28); Tick();
    w.scenarioInUse = Joaat("WORLD_HUMAN_SMOKE"); Tick();
    const int beforeUnload = TaskCount(), beforeReads = w.scenarioReads;
    w.nav = false; Tick(30000);
    Check(R.controller.state == Routine::State::Suspended && TaskCount() == beforeUnload && w.scenarioReads == beforeReads,
        "an active ambient scenario cannot mask unavailable navigation");

    SetDaytimeFixture(); BeginTravel();
    w.scenarioInUse = Joaat("WORLD_HUMAN_SMOKE"); w.taskStatus = 7;
    const int failed = R.destination;
    Tick(100); Tick(4100);
    Check(w.travelCalls == 1 && w.scenarioExits == 1 && R.activity.state == RoutineActivity::State::Exiting,
        "dropped travel waits for an unrelated scenario to exit before retrying navigation");
    Tick(); Tick();
    for (int attempt = 0; attempt < 8 && !R.controller.IsCoolingDown(failed, w.now); ++attempt) Tick(4100);
    Check(w.travelCalls == 4 && w.standCalls == 0 && w.wanderCalls == 1 && R.controller.IsCoolingDown(failed, w.now) &&
        R.controller.state == Routine::State::Waiting && R.ambientFallback,
        "an unrelated mid-travel scenario cannot mask dropped nav tasks or replace the cached-area wander fallback");
}

static void TestAmbientPropClearance()
{
    TravelToLeisure(29); Tick();
    const int destination = R.destination;
    // The engine's own bottle/cigarette is external to the ped collision ignore.
    // Simulate a shape/occupancy hit after entry without changing world availability.
    w.scenarioInUse = Joaat("WORLD_HUMAN_DRINKING"); w.taskStatus = 7;
    w.occupied = true; w.hit = true;
    const int validatedProbes = w.probes;
    Tick(1100);
    Check(R.destination == destination && R.destinationValid && R.controller.state == Routine::State::Wandering &&
        w.wanderCalls == 1 && w.standCalls == 0 && w.probes == validatedProbes,
        "an observed ambient prop cannot invalidate the already-validated destination while its scenario runs");
    w.scenarioInUse = 0;
    Tick(100); Tick(4000);
    Check(R.controller.state == Routine::State::Wandering && R.destinationValid && w.wanderCalls == 2,
        "scenario exit leaves the held destination valid while missing-task recovery resumes wandering");
    const int afterExitProbes = w.probes;
    w.taskStatus = 0; Tick(500);
    Check(R.destinationValid && R.destination == destination && w.standCalls == 0 &&
        w.probes == afterExitProbes && w.wanderCalls == 2,
        "lingering props cannot invalidate a destination after area wandering resumes");
    Tick(1100);
    Check(R.destinationValid && R.controller.state == Routine::State::Wandering && !R.selectPending && w.standCalls == 0 &&
        w.probes == afterExitProbes,
        "occupied-space validation stays disabled at an arrived stop after the former five-second tail");

    for (const int invalid : {0, 1, 2})
    {
        TravelToLeisure(29); Tick();
        w.outside = invalid != 0;
        w.interior = invalid == 1 ? 1 : 0;
        w.waterPresent = invalid == 2;
        Tick(1100);
        Check(R.destinationValid && R.controller.state == Routine::State::Wandering && w.standCalls == 0 && w.wanderCalls == 1,
            "intermittent exterior, interior or water queries cannot cancel a healthy accepted wander area");
    }

    TravelToLeisure(29); Tick();
    w.occupied = true; w.hit = true;
    const int arrivedOccupancy = w.occupancyReads, arrivedProbes = w.probes;
    Tick(6000);
    Check(R.destinationValid && w.standCalls == 0 && w.occupancyReads == arrivedOccupancy && w.probes == arrivedProbes,
        "a bystander at the centre never cancels healthy wandering even without a preceding scenario");
    w.nav = false; Tick();
    Check(R.controller.state == Routine::State::Suspended && w.standCalls == 0,
        "unloaded navigation still suspends an occupied held destination without a task");
}

static void TestAmbientFallbackRecheck()
{
    SetDaytimeFixture();
    w.occupiedNear = true; w.occupiedAt = RoutineData::kLocations[R.plan.route[0]].anchor;
    StartRoutineWander(77, R.definition); Tick(); Tick();
    Check(R.destination == R.plan.route[3], "unavailable workplace selects the actual all-day fallback");
    w.pedPosition = R.centre; Tick();
    Check(R.controller.state == Routine::State::Wandering && w.wanderCalls == 1,
        "fallback arrival begins normal area wandering");
    w.scenarioInUse = Joaat("WORLD_HUMAN_SMOKE"); w.taskStatus = 7;
    const int tasks = TaskCount();
    const ULONGLONG retryDue = R.fallbackRecheckMs;
    Tick(65000); Tick(5000);
    Check(TaskCount() == tasks && !R.selectPending && R.controller.state == Routine::State::Wandering &&
        R.fallbackRecheckMs == retryDue && retryDue < w.now,
        "expired fallback availability retry stays pending without interrupting a healthy native scenario");
    const Vector3 held = R.centre;
    w.scenarioInUse = 0; w.taskStatus = 1;
    Tick();
    Check(R.selectPending && TaskCount() == tasks && R.fallbackRecheckMs > w.now,
        "ending the native scenario permits the pending fallback retry without submitting a stale task");
    Tick();
    Check(R.destination == R.plan.route[3] && R.controller.state == Routine::State::Wandering && w.wanderCalls == 1 &&
        Within(R.centre, held, .001f),
        "an unavailable preferred stop preserves the same fallback centre and healthy native wander task");
}

static void TestHealthyWanderKeepsAcceptedArea()
{
    SetDaytimeFixture(); BeginTravel();
    w.pedPosition = R.centre; Tick();
    const int destination = R.destination;
    const Vector3 centre = R.centre;
    Check(R.fallbackDestination == destination && Within(R.fallbackCentre, centre, .001f),
        "normal arrival updates the cached authored ambient area");
    w.pedPosition.x += 45.0f;
    w.groundOk = false; w.safe = false; w.outside = false; w.interior = 1;
    w.waterPresent = true; w.hit = true; w.occupied = true;
    const int tasks = TaskCount(), probes = w.probes, safeCalls = w.safeCalls;
    for (int frame = 0; frame < 12; ++frame) Tick(1100);
    Check(R.destination == destination && R.destinationValid && R.controller.state == Routine::State::Wandering &&
        !R.ambientFallback && Within(R.centre, centre, .001f) && R.wanderRadius == 45.0f,
        "a target at its 45-metre boundary retains its authored stop despite transient point-query failures");
    Check(TaskCount() == tasks && w.standCalls == 0 && w.probes == probes && w.safeCalls == safeCalls,
        "healthy area wandering does not rerun spawn validation or receive replacement tasks");
    w.scenarioInUse = Joaat("WORLD_HUMAN_SMOKE"); w.taskStatus = 7;
    for (int frame = 0; frame < 12; ++frame) Tick(1100);
    Check(TaskCount() == tasks && w.scenarioExits == 0 && R.destination == destination && R.destinationValid,
        "native ambient scenarios retain the same accepted area when ground and projection queries are unavailable");
}

static void TestUnavailablePhaseContinuesAmbiently()
{
    for (const bool scenario : {false, true})
    {
        TravelToLeisure(29); Tick();
        const int held = R.destination;
        const Vector3 centre = R.centre;
        const int tasks = TaskCount();
        w.groundOk = false;
        if (scenario) { w.scenarioInUse = Joaat("WORLD_HUMAN_DRINKING"); w.taskStatus = 7; }
        w.minute = 240; Tick(); Tick();
        if (scenario) { Tick(); w.taskStatus = 0; }
        Check(R.controller.state == Routine::State::Waiting && R.ambientFallback && R.destination == held &&
            !R.destinationValid && Within(R.centre, centre, .001f) && R.wanderRadius == 45.0f,
            "an unavailable new phase retains the last accepted authored area for ambient continuation");
        const int continuedTasks = tasks + (scenario ? 1 : 0);
        Check(TaskCount() == continuedTasks && w.standCalls == 0 && w.scenarioExits == (scenario ? 1 : 0),
            "a phase exit resumes ambient wandering when the next area is unavailable");
        Tick(); // Settle the controller's initial availability request before measuring repeated frames.
        const int afterFailureSafeCalls = w.safeCalls;
        for (int frame = 0; frame < 50; ++frame) Tick(16);
        Check(TaskCount() == continuedTasks && w.safeCalls == afterFailureSafeCalls,
            "pending per-frame updates neither spam tasks nor repeatedly query failed destinations");
        for (int retry = 0; retry < 3; ++retry) { Tick(5000); Tick(); }
        Check(TaskCount() == continuedTasks && w.safeCalls > afterFailureSafeCalls && w.scenarioExits == (scenario ? 1 : 0) &&
            R.destination == held && Within(R.centre, centre, .001f),
            "bounded availability retries continue without interrupting the cached ambient task");
        const int beforePriority = TaskCount(), reads = w.statusCalls;
        Tick(16, false); Tick(10000, false);
        Check(TaskCount() == beforePriority && w.statusCalls == reads && R.controller.state == Routine::State::Suspended,
            "combat or restraint priority blocks fallback tasking and status probes");
        Tick(); Tick();
        Check(TaskCount() == beforePriority && R.ambientFallback,
            "returning from priority with no usable next stop preserves the running cached-area task");
        w.groundOk = true; w.taskStatus = 0;
        for (int frame = 0; frame < 3; ++frame) Tick(5000);
        Check(R.destination == R.plan.route[3] && !R.ambientFallback &&
            R.controller.state == Routine::State::Travelling && w.travelCalls == 2 && w.standCalls == 0,
            "a newly available advertised destination resumes physical scheduled travel without freezing");
        Check(w.scenarioExits == (scenario ? 1 : 0),
            "a schedule exit is not repeated by later successful destination recovery");
    }
}

static void TestFailedTravelUsesCachedArea()
{
    SetDaytimeFixture(); BeginTravel();
    const int fallback = R.fallbackDestination;
    const Vector3 centre = R.fallbackCentre;
    // First exhaust the native task recovery budget while destination geometry is
    // sound. The next selection pass then sees every authored candidate blocked.
    w.taskStatus = 7;
    Tick(100); Tick(4100); Tick(100); Tick(4100); Tick(100); Tick(4100);
    Check(w.travelCalls == 3 && w.wanderCalls == 1 && w.standCalls == 0 && R.ambientFallback &&
        R.destination == fallback && Within(w.wanderCentre, centre, .001f) && w.wanderRadius == 45.0f,
        "failed travel receives exactly one native wander task around its cached authored fallback");
    w.groundOk = false; w.taskStatus = 0; Tick();
    const int tasks = TaskCount();
    for (int frame = 0; frame < 12; ++frame) Tick(1000);
    Check(TaskCount() == tasks && R.controller.state == Routine::State::Waiting && R.ambientFallback &&
        R.destination == fallback && Within(R.centre, centre, .001f),
        "all-blocked follow-up selection leaves cached-area wandering active across repeated retries");
    w.taskStatus = 7; Tick(); Tick(1400);
    Check(TaskCount() == tasks, "a missing fallback wander task gets a grace period before recovery");
    Tick(100);
    Check(w.wanderCalls == 2 && w.standCalls == 0 && Within(w.wanderCentre, centre, .001f),
        "an actually dropped fallback task is recovered at the same authored area");
    const int afterRecovery = TaskCount();
    for (int frame = 0; frame < 20; ++frame) Tick(16);
    Check(TaskCount() == afterRecovery, "a persistently missing fallback task is not reissued every frame");
    w.taskStatus = 0;
    w.groundOk = true;
    for (int frame = 0; frame < 3; ++frame) Tick(5000);
    Check(!R.ambientFallback && R.destination == R.plan.route[3] && w.travelCalls == 4 && w.standCalls == 0,
        "recovered availability moves from failed travel's ambient fallback to the alternative advertised stop");
    ResetRoutine(); const int beforeDisabled = TaskCount(); Tick(10000);
    Check(TaskCount() == beforeDisabled && R.fallbackDestination == -1 && !R.ambientFallback,
        "cleanup clears fallback ownership and cannot revive a disabled routine");
}

static void TestRejectedSavedPointCanFindAnotherPoint()
{
    SetDaytimeFixture(); BeginTravel();
    const int destination = R.destination;
    const Vector3 oldCentre = R.centre;
    w.occupiedExact = true; w.occupiedAt = oldCentre;
    const int safeCalls = w.safeCalls;
    StartRoutineWander(77, R.definition); Tick(); Tick();
    Check(R.destination == destination && R.destinationValid && !R.ambientFallback &&
        !Within(R.centre, oldCentre, .1f) && w.safeCalls > safeCalls,
        "a rejected saved travel point permits a different validated candidate at the same authored stop");
    Check(Within(R.centre, RoutineData::kLocations[destination].anchor,
            RoutineData::kLocations[destination].candidateRadius) && w.standCalls == 0 &&
        w.travelCalls == 2 && Within(w.travelCentre, R.centre, .001f),
        "a replaced endpoint receives a fresh travel task to the new point within authored candidate bounds");
}

static void TestDailyActivityTransitions()
{
    SetDaytimeFixture(); w.minute = 600; w.clockRate = 60000;
    const auto habits = R.plan;
    const int workplace = R.plan.route[static_cast<int>(Routine::Phase::Work)];
    w.points = {{501, Joaat("WORLD_HUMAN_BROOM_WORKING"), RoutineData::kLocations[workplace].anchor}};
    BeginTravel();
    Check(w.pointTasks == 0 && !RoutineActivityObserved(77), "a known workplace point cannot start before physical area arrival");
    w.pedPosition = R.centre; Tick();
    Check(w.selectedPoint == 501 && R.activity.state == RoutineActivity::State::Entering && !RoutineActivityObserved(77),
        "work arrival attempts a real point and reports entry rather than completed work activity");
    w.scenarioInUse = w.points[0].hash; Tick();
    Check(!RoutineActivityObserved(77), "a requested matching scenario without its running base is still only entry");
    w.scenarioBase = true; Tick();
    Check(RoutineActivityObserved(77) && R.activity.state == RoutineActivity::State::Active,
        "matching point, type and scenario base establish observed activity");
    const int healthyTasks = TaskCount();
    for (int frame = 0; frame < 12; ++frame) Tick(5000);
    Check(TaskCount() == healthyTasks && RoutineActivityObserved(77), "a successful work activity survives without periodic task reissue");
    w.scenarioBase = false; w.scenarioActive = true;
    Tick(5000); Tick(5000);
    Check(TaskCount() == healthyTasks && RoutineActivityObserved(77) && w.scenarioExits == 0,
        "confirmed scenario graph transitions remain healthy outside the initial base state");
    w.scenarioBase = true; w.scenarioActive = false;

    auto transition = [&](int minute, Routine::Phase phase, int pointId, Hash hash) {
        const int travelBefore = w.travelCalls;
        w.minute = minute; Tick();
        Check(R.activity.state == RoutineActivity::State::Exiting && w.travelCalls == travelBefore,
            "each real activity boundary starts a normal scenario exit without same-frame travel");
        const int destination = R.plan.route[static_cast<int>(phase)];
        w.points = {{pointId, hash, RoutineData::kLocations[destination].anchor}};
        const int pointsBefore = w.pointTasks;
        const bool needsTravel = !Within(w.pedPosition, RoutineData::kLocations[destination].anchor, RoutineData::kWanderRadius);
        // Native-free fixtures advance the exit/selection state, then simulate
        // the result of physical navmesh arrival. They do not prove a game path.
        for (int frame = 0; frame < 10 && (R.destination != destination ||
            (needsTravel ? R.controller.state != Routine::State::Travelling : w.pointTasks == pointsBefore)); ++frame) Tick();
        Check(R.destination == destination && !RoutineActivityObserved(77), "schedule selects the correct new area without claiming completed activity");
        if (needsTravel)
            Check(R.controller.state == Routine::State::Travelling && w.pointTasks == pointsBefore && w.travelCalls > travelBefore,
                "a newly selected distant area with a compatible point requires normal travel before activity discovery");
        else
            Check(w.travelCalls == travelBefore, "overlapping accepted areas do not require a redundant walk back to their centre");
        w.pedPosition = R.centre;
        for (int frame = 0; frame < 5 && w.pointTasks == pointsBefore; ++frame) Tick();
        Check(w.selectedPoint == pointId && R.activity.state == RoutineActivity::State::Entering && !RoutineActivityObserved(77),
            "the new area's intended activity is attempted only after arrival and remains labeled entry");
        w.scenarioInUse = hash; w.scenarioBase = true; Tick();
        Check(RoutineActivityObserved(77), "a completed entry is recognized at every scheduled stop");
    };
    transition(660, Routine::Phase::Lunch, 502, Joaat("PROP_HUMAN_SEAT_CHAIR_TABLE_EATING_KNIFE_FORK"));
    Check(R.activity.point.kind == RoutineActivity::Kind::Eat, "lunch observation requires an actual compatible eating scenario");
    w.minute = 710; pausedDurationMs += 45000;
    const int lunchTasks = TaskCount();
    Tick(); Tick();
    Check(RoutineActivityObserved(77) && R.destination == habits.route[4] && TaskCount() == lunchTasks,
        "pause revalidation keeps healthy lunch through 11:50 without an obsolete minimum-stay cutoff");
    transition(720, Routine::Phase::Work, 501, Joaat("WORLD_HUMAN_BROOM_WORKING"));
    Check(R.destination == workplace, "noon returns to the exact assigned morning workplace");
    transition(960, Routine::Phase::Shops, 503, Joaat("WORLD_HUMAN_SMOKE"));
    transition(1140, Routine::Phase::Leisure, 504, Joaat("WORLD_CAMP_FIRE_STANDING"));
    // Approach midnight naturally; a deliberate multi-hour jump would correctly
    // ask the route policy to re-evaluate even within the same activity phase.
    for (int minute = 1170; minute < 1440; minute += 30) { w.minute = minute; Tick(); }
    w.minute = 1439; Tick();
    const int midnightTasks = TaskCount(), midnightExits = w.scenarioExits;
    w.minute = 0; Tick();
    Check(TaskCount() == midnightTasks && w.scenarioExits == midnightExits && RoutineActivityObserved(77) &&
        R.destination == habits.route[2], "midnight preserves the observed evening activity without an invented transition");
    transition(180, Routine::Phase::Rest, 505, Joaat("WORLD_HUMAN_SLEEP_GROUND_ARM"));
    Check(R.activity.point.kind == RoutineActivity::Kind::Sleep, "nighttime sleeping is observed only from the running sleep point");
    transition(360, Routine::Phase::Work, 501, Joaat("WORLD_HUMAN_BROOM_WORKING"));
    for (int phase = 0; phase < Routine::kPhaseCount; ++phase)
        Check(R.plan.route[phase] == habits.route[phase], "all six boundaries retain the complete immutable target habit plan");
}

static void TestPointFailureRecovery()
{
    SetDaytimeFixture(); w.minute = 600; w.clockRate = 60000;
    const auto habits = R.plan;
    const Vector3 workplace = RoutineData::kLocations[habits.route[0]].anchor;
    w.points = {{601, Joaat("WORLD_HUMAN_BROOM_WORKING"), workplace},
        {602, Joaat("WORLD_HUMAN_BROOM_WORKING"), {workplace.x + 1.0f, workplace.y, workplace.z}}};
    BeginTravel(); w.pedPosition = R.centre; Tick();
    const int failed = w.selectedPoint;
    Check(failed > 0 && w.pointTasks == 1 && !RoutineActivityObserved(77), "failed-entry fixture starts an unconfirmed work point");
    Tick(static_cast<unsigned>(RoutineActivity::Controller::kEntryMs));
    Check(R.activity.IsCoolingDown(failed, w.now) && R.activity.state == RoutineActivity::State::Exiting,
        "a point whose entry never completes is excluded and exits within the bounded entry deadline");
    for (int frame = 0; frame < 12 && w.pointTasks < 2; ++frame) Tick();
    Check(w.pointTasks == 2 && w.selectedPoint > 0 && w.selectedPoint != failed && R.activity.IsCoolingDown(failed, w.now),
        "failed entry retries a distinct compatible point instead of selecting the same failed handle");
    const int good = w.selectedPoint;
    w.scenarioInUse = Scenario(good)->hash; w.scenarioBase = true; Tick();
    Check(RoutineActivityObserved(77), "another suitable point can recover to an actually observed work activity");

    const int taskCount = TaskCount(), clearCount = w.clears, exitCount = w.scenarioExits, queryCount = w.pointQueries;
    Tick(16, false); Tick(30000, false);
    Check(TaskCount() == taskCount && w.clears == clearCount && w.scenarioExits == exitCount && w.pointQueries == queryCount,
        "combat, search, restraint or handoff priority suppresses movement, activity, exit, clear and discovery calls");
    // The higher-priority interaction consumes the old scenario. Resume must
    // select the current clock's lunch activity rather than retry obsolete work.
    Scenario(good)->user = 0;
    w.scenarioInUse = 0; w.selectedPoint = 0; w.scenarioTask = w.scenarioBase = false;
    w.minute = 660;
    const int lunch = habits.route[4];
    w.points.push_back({603, Joaat("WORLD_HUMAN_DRINKING"), RoutineData::kLocations[lunch].anchor});
    for (int frame = 0; frame < 12 && R.destination != lunch; ++frame) Tick();
    Check(R.destination == lunch && R.controller.state == Routine::State::Travelling && !RoutineActivityObserved(77),
        "interruption recovery uses the current-time lunch destination and preserves walking transit");
    w.pedPosition = R.centre;
    for (int frame = 0; frame < 5 && w.selectedPoint != 603; ++frame) Tick();
    Check(w.selectedPoint == 603 && R.activity.state == RoutineActivity::State::Entering,
        "after interruption a compatible lunch point is entered at the current stop");
    w.scenarioInUse = Joaat("WORLD_HUMAN_DRINKING"); w.scenarioBase = true; Tick();
    Check(RoutineActivityObserved(77) && R.activity.point.kind == RoutineActivity::Kind::Drink,
        "interruption recovery reports observed drinking only after the actual scenario base runs");
    for (int phase = 0; phase < Routine::kPhaseCount; ++phase)
        Check(R.plan.route[phase] == habits.route[phase], "point failure and priority recovery never reroll identity or workplace");
}

int main()
{
    Check(!RoutineActivityObserved(77), "an unassigned activity is never reported as observed");
    TestPreparedDefinition();
    TestGeneratedTownModels();
    TestPreparationFailureAndFallback();
    TestDeploymentAndWander();
    TestPedOriginAndBoundedPlacement();
    TestTravelAndClock();
    TestArrivalAtWanderBoundary();
    TestSuspensionAndAvailability();
    TestTravelRecovery();
    TestTravelEstimateAndLongDeadline();
    TestAmbientWanderingAndRecovery();
    TestAmbientPriorityAndTravelRecovery();
    TestAmbientPropClearance();
    TestAmbientFallbackRecheck();
    TestHealthyWanderKeepsAcceptedArea();
    TestUnavailablePhaseContinuesAmbiently();
    TestFailedTravelUsesCachedArea();
    TestRejectedSavedPointCanFindAnotherPoint();
    TestDailyActivityTransitions();
    TestPointFailureRecovery();
    std::printf("Routine runtime bridge: %u checks passed.\n", checks);
}
