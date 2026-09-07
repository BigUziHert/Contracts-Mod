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

struct World
{
    unsigned now = 1000, waits = 0, cancelAfter = 999999;
    unsigned loadedAfter = 0, targetDiesAfter = 999999, settleAfterWaits = 999999, ownershipLostAfter = 999999;
    int minute = 720, clockRate = 2000, taskStatus = 0;
    bool canInteract = true, collision = true, nav = true, safe = true;
    bool outside = true, groundOk = true, occupied = false, hit = false;
    bool sceneActive = false, startOk = true;
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
    float wanderRadius = 0, avoidRadius = 0;
    int wanderCalls = 0, keepCalls = 0, avoidCalls = 0, waterCalls = 0;
    int travelCalls = 0, standCalls = 0, statusCalls = 0;
    int activityCalls = 0, scenarioReads = 0;
    int travelTimeout = 0;
    Hash lastTaskHash = 0;
    Hash scenarioInUse = 0;
    bool mayEnterWater = true;
} w;
static struct OwnedPedFixture { Ped ped = 77; } ownedPed;
static bool OwnedPedIdentityMatches() { return w.waits < w.ownershipLostAfter; }
static ULONGLONG RuntimeNowMs() { return w.now; }
static ULONGLONG GetTickCount64() { return w.now; }
static bool PlayerAvailable() { return w.waits < w.cancelAfter; }
static bool LivingPed(Ped ped) { return ped != 0 && w.waits < w.targetDiesAfter; }
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
    w.lastIgnored = ignore; return w.occupied || (w.occupiedNear && Within(point, w.occupiedAt, 20.0f));
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
static int GET_SCRIPT_TASK_STATUS(Ped, Hash hash, bool)
{
    ++w.statusCalls; w.lastTaskHash = hash; return w.taskStatus;
}
static void TASK_FOLLOW_NAV_MESH_TO_COORD(Ped, Vector3 centre, float speed, int timeout, float range, int flags, float heading)
{
    Check(speed == 1.0f && range == 2.0f && flags == 0 && heading == 40000.0f,
        "travel uses verified walking flags with bounded target range");
    ++w.travelCalls; w.travelCentre = centre; w.travelTimeout = timeout;
}
static void TASK_STAND_STILL(Ped, int duration)
{
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
static void SET_PED_KEEP_TASK(Ped, bool keep) { Check(keep, "routine task is kept"); ++w.keepCalls; }
static bool IS_PED_USING_ANY_SCENARIO(Ped ped)
{
    Check(ped == 77, "ambient scenario observation examines the existing target"); ++w.scenarioReads;
    return w.scenarioInUse != 0;
}
}

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
    Check(w.requests == 0 && w.starts == 0, "already loaded candidates never issue an unnecessary streaming request");
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
    w = {}; std::srand(23); w.failFirstSafeCalls = 5;
    RoutineRuntime fallback;
    Check(PrepareRoutineContract(fallback), "unusable preferred location permits the all-day fallback");
    Check(RoutineData::kLocations[fallback.destination].kind == RoutineData::PlaceKind::Rest,
        "bounded alternate is the declared overnight/public fallback");
    Check(w.requests == 0 && w.safeCalls == 6, "five failed preferred candidates permit one validated fallback without unneeded streaming");
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
    R.definition = {"Valentine", "Livestock hand", "Valentine", R.centre, Tune::kReAggroSightDist,
        RoutineModels(2, RoutineData::LivestockHand), &kHumanTarget, nullptr, ResetRoutine};
    w.pedPosition = R.centre; w.pedPosition.x += 40;
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
    Check(w.travelCalls == 1 && w.lastTaskHash == Joaat("SCRIPT_TASK_FOLLOW_NAV_MESH_TO_COORD"),
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
    Check(w.wanderCalls == 1 && Within(R.centre, fixed, .001f) && w.lastTaskHash == Joaat("SCRIPT_TASK_WANDER_IN_AREA"),
        "wandering does not move the centre or repeat a running task");
    w.minute = 900;
    const int tasks = TaskCount();
    Tick();
    Check(R.selectPending && TaskCount() == tasks, "phase change first requests fresh location selection");
    Tick();
    Check(R.destination == R.plan.route[1] && w.travelCalls == 2, "afternoon physically travels to the advertised shop frontage");

    w.minute = 1100;
    Check(StartRoutineWander(77, R.definition), "post-search recovery requests current schedule");
    Tick(); Tick();
    Check(R.destination == R.plan.route[2], "resuming after time change chooses evening destination");
    const int beforeJump = TaskCount();
    w.minute = 100;
    Tick();
    Check(R.selectPending && TaskCount() == beforeJump, "midnight time skip requests reselection without teleporting");
    Tick();
    Check(R.destination == R.plan.route[3], "overnight clock selects the advertised all-day fallback");

    SetDaytimeFixture(); BeginTravel();
    const int beforePause = TaskCount();
    pausedDurationMs += 45000;
    Tick();
    Check(R.selectPending && TaskCount() == beforePause,
        "pause or fade wholly skipped by the outer loop forces fresh selection before another task");
    Tick();
    Check(R.destination == R.plan.route[0] && w.travelCalls == 2 && R.pauseSnapshotMs == pausedDurationMs,
        "resumption publishes the new pause snapshot and restarts current valid travel once");
    Tick();
    Check(w.travelCalls == 2 && !R.selectPending, "unchanged pause duration does not retrigger resumption");
}
static void TestSuspensionAndAvailability()
{
    SetDaytimeFixture(); BeginTravel();
    const int tasks = TaskCount(), statusCalls = w.statusCalls;
    Tick(16, false); Tick(30000, false);
    Check(R.controller.state == Routine::State::Suspended && TaskCount() == tasks && w.statusCalls == statusCalls,
        "combat, search or restraint priority performs no routine task or task-status recovery");
    w.minute = 900;
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
    w.pedPosition = RoutineData::kLocations[R.plan.route[1]].anchor; w.pedPosition.x += 200;
    StartRoutineWander(77, R.definition); Tick(); Tick();
    Check(R.destination == R.plan.route[3], "travel plus minimum stay skips a shop that closes before a useful arrival");
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
    Check(w.travelCalls == 3 && R.selectPending && R.controller.IsCoolingDown(failed, w.now) && w.standCalls == 1,
        "exhausted recovery waits once and cools down the failed location");
    Tick();
    Check(R.destination == R.plan.route[3], "failed destination is excluded from immediate reselection");

    SetDaytimeFixture(); BeginTravel();
    const int expired = R.destination;
    Tick(300000);
    Check(R.controller.IsCoolingDown(expired, w.now) && w.standCalls == 1 && w.travelCalls == 1,
        "absolute travel deadline ends a stuck task even when native reports it active");
    ResetRoutine();
    Check(!R.controller.IsCoolingDown(expired, w.now) && !R.enabled && R.nextValidationMs == 0 && R.nextStreamRequestMs == 0,
        "cleanup resets cooldown, deferred selection and validation deadlines");
    const int count = TaskCount();
    UpdateRoutine(77, R.definition, true);
    Check(TaskCount() == count, "disabled runtime never revives a cleaned-up target");
}

static void TravelToLeisure(unsigned seed)
{
    SetDaytimeFixture();
    R.plan.seed = seed; w.minute = 1100;
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
        ResetRoutine();
        Check(R.ambientClearanceUntilMs == 0 && !R.enabled && w.activityCalls == 0,
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
    Check(R.selectPending && TaskCount() == tasks, "post-priority resume reselects the current schedule before tasking");

    TravelToLeisure(28); Tick();
    w.scenarioInUse = Joaat("WORLD_HUMAN_SMOKE"); w.taskStatus = 7;
    Tick(5000); const int beforePhase = TaskCount();
    w.minute = 60; Tick();
    Check(R.selectPending && TaskCount() == beforePhase, "a native scenario cannot suppress a new scheduled phase");
    Tick();
    Check(R.destination == R.plan.route[3] && w.travelCalls == 2 && R.ambientClearanceUntilMs == 0,
        "phase change travels to the new destination without carrying old-stop prop clearance grace");

    TravelToLeisure(28); Tick();
    w.scenarioInUse = Joaat("WORLD_HUMAN_SMOKE"); Tick();
    const int beforeUnload = TaskCount(), beforeReads = w.scenarioReads;
    w.nav = false; Tick(30000);
    Check(R.controller.state == Routine::State::Suspended && TaskCount() == beforeUnload && w.scenarioReads == beforeReads,
        "an active ambient scenario cannot mask unavailable navigation");

    SetDaytimeFixture(); BeginTravel();
    w.scenarioInUse = Joaat("WORLD_HUMAN_SMOKE"); w.taskStatus = 7;
    const int failed = R.destination;
    Tick(100); Tick(4100); Tick(100); Tick(4100); Tick(100); Tick(4100);
    Check(w.travelCalls == 3 && w.standCalls == 1 && R.controller.IsCoolingDown(failed, w.now) && w.scenarioReads == 0,
        "a scenario during travel cannot mask dropped nav tasks or their bounded failure cooldown");
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
    const ULONGLONG clearanceUntil = R.ambientClearanceUntilMs;
    w.scenarioInUse = 0;
    Tick(100); Tick(4000);
    Check(R.controller.state == Routine::State::Wandering && R.destinationValid && w.wanderCalls == 2 &&
        R.ambientClearanceUntilMs == clearanceUntil,
        "scenario exit preserves a bounded grace period while missing-task recovery resumes wandering");
    const int afterExitProbes = w.probes;
    w.taskStatus = 0; Tick(500);
    Check(R.destinationValid && R.destination == destination && w.standCalls == 0 &&
        w.probes == afterExitProbes && w.wanderCalls == 2,
        "lingering prop clearance hits remain ignored during the five-second exit tail");
    Tick(1100);
    Check(!R.destinationValid && R.controller.state == Routine::State::Waiting && R.selectPending && w.standCalls == 1,
        "full occupied-space validation resumes after the bounded prop-exit grace");
    ResetRoutine();
    Check(R.ambientClearanceUntilMs == 0, "contract cleanup resets the observed ambient clearance timer");
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
    w.scenarioInUse = 0;
    Tick();
    Check(R.selectPending && TaskCount() == tasks && R.fallbackRecheckMs > w.now,
        "ending the native scenario permits the pending fallback retry without submitting a stale task");
    Tick();
    Check(R.destination == R.plan.route[3] && R.controller.state == Routine::State::Wandering && w.wanderCalls == 2,
        "an unavailable preferred stop still falls back through one fresh native wander task");
}

int main()
{
    TestPreparedDefinition();
    TestGeneratedTownModels();
    TestPreparationFailureAndFallback();
    TestDeploymentAndWander();
    TestPedOriginAndBoundedPlacement();
    TestTravelAndClock();
    TestSuspensionAndAvailability();
    TestTravelRecovery();
    TestAmbientWanderingAndRecovery();
    TestAmbientPriorityAndTravelRecovery();
    TestAmbientPropClearance();
    TestAmbientFallbackRecheck();
    std::printf("Routine runtime bridge: %u checks passed.\n", checks);
}
