#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using Ped = int;
using Hash = unsigned;
using ULONGLONG = unsigned long long;
struct Vector3 { float x = 0, y = 0, z = 0; };
#include "../rdr2 scripting environment/samples/Pools/routine_plan.h"
#include "../rdr2 scripting environment/samples/Pools/target_ai_logic.h"
#include "../rdr2 scripting environment/samples/Pools/routine_debug_view.h"

static unsigned checks = 0;
static void Check(bool result, const char* message)
{
    ++checks;
    if (!result) { std::fprintf(stderr, "FAILED: %s\n", message); std::exit(EXIT_FAILURE); }
}
constexpr Hash Joaat(const char* text)
{
    Hash hash = 0;
    for (; *text; ++text)
    {
        unsigned c = static_cast<unsigned char>(*text);
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        hash += c; hash += hash << 10; hash ^= hash >> 6;
    }
    hash += hash << 3; hash ^= hash >> 11; hash += hash << 15;
    return hash;
}
static Hash joaat(const char* text) { return Joaat(text); }
static float DistSq(Vector3 a, Vector3 b)
{
    const float x = a.x - b.x, y = a.y - b.y, z = a.z - b.z;
    return x * x + y * y + z * z;
}
enum ContractState { CONTRACT_NONE, CONTRACT_UNKNOWN, CONTRACT_FOUND, CONTRACT_DEAD, CONTRACT_PAID };
static ContractState g_state = CONTRACT_UNKNOWN;
struct ContractDef { Vector3 spawn{}; };
struct ActiveContract { Ped target = 77; const ContractDef* def = nullptr; TargetAI::Memory ai; ULONGLONG photoMs = 0; };
static ActiveContract C;
static Ped pedMe = 1;
struct RuntimeFixture
{
    bool enabled = true;
    RoutinePlan::Plan plan;
    ContractDef definition;
    int destination = -1;
    Vector3 centre{};
    float wanderRadius = 22.0f;
    Routine::Controller controller;
    bool selectPending = false, resumeRequested = false, destinationValid = true;
} R;
struct World
{
    ULONGLONG now = 1000;
    int minute = 600;
    bool exists = true, dead = false, loaded = true, inside = false;
    bool hogtied = false, hogtying = false, lassoed = false, ragdoll = false, gettingUp = false;
    bool combatAnyone = false, combatPlayer = false, vehicle = false;
    Hash usingScenario = 0;
    int combatStatus = 7, routineStatus = 0;
    Vector3 player{}, target{3, 4, 0};
    unsigned coordinates = 0, deathReads = 0, liveReads = 0, clockReads = 0;
    unsigned markerUpdates = 0, markerClears = 0, drawCalls = 0, keySamples = 0;
    bool markerHasSpawn = false, markerHasDestination = false, releasedF8 = false;
} w;
static bool ContractActive() { return g_state == CONTRACT_UNKNOWN || g_state == CONTRACT_FOUND || g_state == CONTRACT_DEAD; }
static bool IsRoutine(const ContractDef& def) { return R.enabled && &def == &R.definition; }
static bool TargetExists() { return C.target != 0 && w.exists; }
static ULONGLONG GetTickCount64() { return w.now; }
static int RoutineMinute() { ++w.clockReads; return w.minute; }
static void ReadLiving(Ped ped)
{
    Check(ped == C.target && w.exists && !w.dead, "activity queries only examine a confirmed living target");
    ++w.liveReads;
}
namespace ENTITY
{
static Vector3 GET_ENTITY_COORDS(Ped ped, bool, bool)
{
    Check(ped == pedMe || (ped == C.target && w.exists), "coordinates never read from a vanished target");
    ++w.coordinates; return ped == pedMe ? w.player : w.target;
}
}
namespace PED
{
static bool IS_PED_DEAD_OR_DYING(Ped ped, bool) { Check(ped == C.target && w.exists, "death query requires existing target"); ++w.deathReads; return w.dead; }
static bool IS_PED_HOGTIED(Ped ped) { ReadLiving(ped); return w.hogtied; }
static bool IS_PED_BEING_HOGTIED(Ped ped) { ReadLiving(ped); return w.hogtying; }
static bool IS_PED_LASSOED(Ped ped) { ReadLiving(ped); return w.lassoed; }
static bool IS_PED_RAGDOLL(Ped ped) { ReadLiving(ped); return w.ragdoll; }
static bool IS_PED_IN_COMBAT(Ped ped, Ped other) { ReadLiving(ped); return other == 0 ? w.combatAnyone : w.combatPlayer; }
static bool IS_PED_IN_ANY_VEHICLE(Ped ped, bool) { ReadLiving(ped); return w.vehicle; }
static bool IS_PED_USING_SCENARIO_HASH(Ped ped, Hash hash) { ReadLiving(ped); return hash != 0 && w.usingScenario == hash; }
static bool IS_PED_USING_ANY_SCENARIO(Ped ped) { ReadLiving(ped); return w.usingScenario != 0; }
}
namespace TASK
{
static bool IS_PED_GETTING_UP(Ped ped) { ReadLiving(ped); return w.gettingUp; }
static int GET_SCRIPT_TASK_STATUS(Ped ped, Hash task, bool)
{
    ReadLiving(ped);
    return task == Joaat("SCRIPT_TASK_COMBAT") ? w.combatStatus : w.routineStatus;
}
}
namespace RoutineSpawn { static bool Loaded(Vector3) { ReadLiving(C.target); return w.loaded; } }
namespace INTERIOR
{
static int GET_INTERIOR_FROM_COLLISION(Vector3) { ReadLiving(C.target); return w.inside ? 1 : 0; }
static bool IS_COLLISION_MARKED_OUTSIDE(Vector3) { ReadLiving(C.target); return !w.inside; }
}
namespace RoutineDebugBlips
{
static void Clear() { ++w.markerClears; w.markerHasSpawn = w.markerHasDestination = false; }
static void Update(bool enabled, const Vector3* spawn, const Vector3* destination)
{
    Check(enabled, "enabled display updates only its own debug marker layer");
    ++w.markerUpdates; w.markerHasSpawn = spawn != nullptr; w.markerHasDestination = destination != nullptr;
}
}
static void DrawTextToScreen(const char* text, float x, float y, float scale, int, int, int, int)
{
    Check(text && *text && x > 0 && x < 1 && y > 0 && y < 1 && scale > 0, "debug text is nonempty and screen positioned");
    ++w.drawCalls;
}
constexpr unsigned VK_F8 = 0x77;
static bool IsKeyJustUp(unsigned key)
{
    Check(key == VK_F8, "production integration consumes only F8 for this toggle");
    ++w.keySamples;
    const bool released = w.releasedF8; w.releasedF8 = false; return released;
}
#include "routine_debug_bridge_under_test.h"

static void Fixture()
{
    R = {}; C = {}; w = {}; g_state = CONTRACT_UNKNOWN;
    Check(RoutinePlan::Build(R.plan, 2, RoutineData::LivestockHand, 29), "debug fixture uses a real route plan");
    R.plan.offsetMinutes = 0;
    R.destination = R.plan.route[0]; R.centre = {3, 8, 0};
    R.controller.state = Routine::State::Travelling;
    R.controller.destinationId = R.destination;
    C.def = &R.definition;
    routineDebugEnabled = true; routineDebugNextSampleMs = 0; routineDebugLines = {};
}
static std::string Activity() { return ObserveRoutineDebug().doing; }
static void ExistenceDeathAndFreshCoordinates()
{
    Fixture(); g_state = CONTRACT_NONE;
    Check(!ObserveRoutineDebug().active && w.coordinates == 0 && w.liveReads == 0, "idle overlay never reads target state");
    Fixture(); w.exists = false;
    Check(!ObserveRoutineDebug().targetExists && w.coordinates == 0 && w.deathReads == 0 && w.liveReads == 0,
        "vanished target exits before any entity/death/activity native");
    Fixture(); w.dead = true; R.controller.state = Routine::State::Wandering; w.usingScenario = Joaat("WORLD_HUMAN_SMOKE");
    const auto corpse = ObserveRoutineDebug();
    Check(corpse.targetDead && corpse.targetExists && w.coordinates == 2 && w.deathReads == 1 && w.liveReads == 0,
        "dead target can report fresh position but never stale routine activity");
    Check(corpse.playerDistance == 5.0f, "distance reads the actual current corpse coordinates");
    Fixture(); g_state = CONTRACT_DEAD; w.exists = false;
    const auto vanishedCorpse = ObserveRoutineDebug();
    Check(vanishedCorpse.targetDead && !vanishedCorpse.targetExists && w.coordinates == 0 && w.liveReads == 0,
        "photographed missing corpse retains known deceased status without stale position");
    Fixture(); const auto living = ObserveRoutineDebug();
    Check(living.playerDistance == 5.0f && living.destinationDistance == 4.0f && living.x == 3 && living.y == 4,
        "player-to-target and target-to-destination measurements remain distinct");
    w.target = {0, 0, 12};
    Check(ObserveRoutineDebug().playerDistance == 12.0f, "uncached observation reads fresh three-dimensional distance");
}
static void PriorityAndScenarioLabels()
{
    struct Priority { bool World::*field; const char* label; };
    for (const auto& priority : std::array<Priority, 5>{{{&World::hogtied,"Hogtied"}, {&World::hogtying,"Being hogtied"},
        {&World::lassoed,"Lassoed"}, {&World::ragdoll,"Ragdoll"}, {&World::gettingUp,"Getting up"}}})
    {
        Fixture(); R.controller.state = Routine::State::Wandering; w.usingScenario = Joaat("WORLD_HUMAN_SMOKE");
        C.ai.state = TargetAI::State::Engaged; w.*(priority.field) = true;
        Check(Activity() == priority.label, "physical restraint/recovery wins over stale routine and engagement state");
    }
    Fixture(); C.ai.state = TargetAI::State::Engaged; Check(Activity() == "Fighting", "engaged policy reports fighting");
    C.ai.state = TargetAI::State::Search; Check(Activity() == "Searching for player", "search policy reports investigation");
    C.ai.state = TargetAI::State::Wander; C.ai.pendingEngagement = true;
    Check(Activity() == "Preparing to fight", "deferred engagement wins over travel");
    Fixture(); w.combatStatus = 1; Check(Activity() == "Fighting", "active native combat task wins over routine state");
    Fixture(); w.combatAnyone = true; Check(Activity() == "Fighting another actor", "combat with another actor reports its priority");
    w.combatPlayer = true; Check(Activity() == "Walking to destination", "lingering player combat flag alone does not invent active combat");
    Fixture(); w.vehicle = true; Check(Activity() == "In a vehicle", "vehicle blocks routine activity label");
    Fixture(); w.loaded = false; Check(Activity() == "Paused: area not loaded", "unloaded area is identified");
    Fixture(); R.controller.state = Routine::State::Suspended; w.usingScenario = Joaat("WORLD_HUMAN_SMOKE");
    Check(Activity() == "Routine paused", "suspension takes priority over an observed scenario");
    Fixture(); R.selectPending = true; Check(Activity() == "Choosing destination", "pending selection never advertises stale travel");
    Fixture(); R.controller.state = Routine::State::Wandering; w.routineStatus = 7;
    Check(Activity() == "Wander task pending / recovery", "without actual scenario evidence debug never invents smoking");
    w.usingScenario = Joaat("WORLD_HUMAN_SMOKE");
    Check(Activity() == "Smoking (ambient)" && ObserveRoutineDebug().taskActive,
        "actual native smoking is identified as a healthy ambient pause during wandering");
    w.usingScenario = Joaat("WORLD_HUMAN_DRINKING");
    Check(Activity() == "Drinking (ambient)", "actual native drinking is identified without assigned activity metadata");
    w.now += 100000;
    Check(Activity() == "Drinking (ambient)", "debug does not invent expiry for a native ambient scenario");
    w.usingScenario = Joaat("WORLD_HUMAN_STARE_STOIC");
    Check(Activity() == "Ambient scenario", "other actual scenarios use a truthful generic label");
    R.controller.state = Routine::State::Travelling;
    Check(Activity() == "Ambient scenario" && !ObserveRoutineDebug().taskActive,
        "observed scenario during travel does not claim its missing nav task is healthy");
    w.usingScenario = 0; R.controller.state = Routine::State::Wandering;
    Check(Activity() == "Wander task pending / recovery", "ending native scenario immediately clears its debug label");
    Fixture(); w.routineStatus = 7; Check(Activity() == "Travel task pending / recovery", "missing travel task is not reported as healthy movement");
    R.controller.state = Routine::State::Wandering;
    Check(Activity() == "Wander task pending / recovery", "missing wander task is visible");
    w.routineStatus = 1; Check(Activity() == "Wandering near destination", "active wander task has the correct label");
}
static void ScheduleAndReadOnlySnapshot()
{
    struct Case { int minute, offset, nextMinute, nextPhase; };
    for (const auto& item : std::array<Case, 6>{{{600,0,840,1}, {900,0,1080,2}, {1300,0,0,3},
        {60,0,360,0}, {0,30,30,3}, {1439,-30,330,0}}})
    {
        Fixture(); w.minute = item.minute; R.plan.offsetMinutes = item.offset;
        const auto snapshot = ObserveRoutineDebug();
        Check(snapshot.nextMinute == item.nextMinute && std::strcmp(snapshot.nextDestination,
            RoutineData::kLocations[R.plan.route[item.nextPhase]].name) == 0, "next scheduled destination handles phase offset and midnight");
    }
    Fixture(); R.destination = -1;
    Check(!ObserveRoutineDebug().hasDestination, "unset destination cannot index the catalogue");
    R.destination = RoutineData::kLocationCount;
    Check(!ObserveRoutineDebug().hasDestination, "out-of-range destination cannot index the catalogue");
    Fixture(); R.plan.route[0] = -1;
    Check(Activity() == "No town routine", "invalid route prevents unsafe next-location indexing");
    Fixture();
    std::array<unsigned char, sizeof(R)> beforeR{};
    std::array<unsigned char, sizeof(C)> beforeC{};
    std::memcpy(beforeR.data(), &R, sizeof(R)); std::memcpy(beforeC.data(), &C, sizeof(C));
    const Vector3 originalTarget = w.target, originalPlayer = w.player;
    for (int repeat = 0; repeat < 20; ++repeat) ObserveRoutineDebug();
    Check(std::memcmp(beforeR.data(), &R, sizeof(R)) == 0 && std::memcmp(beforeC.data(), &C, sizeof(C)) == 0,
        "observation changes no contract/controller/route/task state");
    Check(DistSq(w.target, originalTarget) == 0 && DistSq(w.player, originalPlayer) == 0 && w.markerUpdates == 0,
        "snapshot changes neither world coordinates nor markers");
}
static void SamplingRenderingAndEarlyToggle()
{
    Fixture(); UpdateRoutineDebug();
    Check(w.clockReads == 1 && w.drawCalls > 0 && w.markerHasSpawn && w.markerHasDestination,
        "first enabled frame samples immediately and renders debug markers/text");
    const unsigned drawPerFrame = w.drawCalls, initialReads = w.liveReads;
    for (int frame = 0; frame < 10; ++frame) { w.now += 20; UpdateRoutineDebug(); }
    Check(w.clockReads == 1 && w.liveReads == initialReads && w.drawCalls == drawPerFrame * 11 && w.markerUpdates == 11,
        "observer is throttled while cached lines and markers render each frame");
    w.now = 1249; UpdateRoutineDebug();
    Check(w.clockReads == 1, "sampling cannot occur before the 250ms interval");
    w.now = 1250; UpdateRoutineDebug(); Check(w.clockReads == 2, "sampling occurs at four hertz when frame reaches its deadline");
    w.now = 3000; UpdateRoutineDebug(); Check(w.clockReads == 3, "slow frame samples once instead of replaying missed observations");
    w.releasedF8 = true; SampleProductionDebugKey(); // Production integration runs this before its early-return guard.
    Check(!routineDebugEnabled && w.markerClears == 1 && !w.markerHasSpawn && routineDebugNextSampleMs == 0,
        "F8 off clears markers immediately without waiting for the next render frame");
    const unsigned draws = w.drawCalls, reads = w.clockReads, markers = w.markerUpdates;
    UpdateRoutineDebug(); SampleProductionDebugKey();
    Check(w.drawCalls == draws && w.clockReads == reads && w.markerUpdates == markers && w.markerClears == 1,
        "disabled display performs no observation/drawing and one release cannot toggle twice");
    w.releasedF8 = true; SampleProductionDebugKey(); UpdateRoutineDebug();
    Check(routineDebugEnabled && w.clockReads == reads + 1 && w.drawCalls > draws,
        "re-enabling invalidates cached sampling deadline and immediately shows fresh data");
}
int main()
{
    ExistenceDeathAndFreshCoordinates(); PriorityAndScenarioLabels(); ScheduleAndReadOnlySnapshot(); SamplingRenderingAndEarlyToggle();
    std::printf("Routine debug bridge: %u checks passed.\n", checks);
}
