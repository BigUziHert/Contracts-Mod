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
#include "../rdr2 scripting environment/samples/Pools/routine_activity.h"
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
struct ActiveContract { Ped target = 77; const ContractDef* def = nullptr; TargetAI::Memory ai; ULONGLONG photoMs = 0; bool combatExitPending = false; };
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
    RoutineActivity::Controller activity;
    bool activityFallback = false, activityPointValid = true;
    bool selectPending = false, resumeRequested = false, destinationValid = true, ambientFallback = false;
} R;
struct World
{
    ULONGLONG now = 1000;
    int minute = 600;
    bool exists = true, dead = false, loaded = true, inside = false;
    bool hogtied = false, hogtying = false, lassoed = false, ragdoll = false, gettingUp = false;
    bool combatAnyone = false, combatPlayer = false, vehicle = false, sitting = false;
    Hash usingScenario = 0;
    bool assignedActivity = false;
    unsigned assignedActivityReads = 0;
    int combatStatus = 7, routineStatus = 0;
    Vector3 player{}, target{3, 4, 0};
    unsigned coordinates = 0, deathReads = 0, liveReads = 0, clockReads = 0;
    unsigned combatTaskReads = 0, combatPlayerReads = 0, scenarioReads = 0, seatedReads = 0, routineTaskReads = 0;
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
static bool IS_PED_IN_COMBAT(Ped ped, Ped other)
{
    ReadLiving(ped);
    if (other == 0) return w.combatAnyone;
    Check(other == pedMe, "combat evidence queries the current player");
    ++w.combatPlayerReads; return w.combatPlayer;
}
static bool IS_PED_IN_ANY_VEHICLE(Ped ped, bool) { ReadLiving(ped); return w.vehicle; }
static bool IS_PED_USING_SCENARIO_HASH(Ped ped, Hash hash) { ReadLiving(ped); return hash != 0 && w.usingScenario == hash; }
static bool IS_PED_USING_ANY_SCENARIO(Ped ped) { ReadLiving(ped); ++w.scenarioReads; return w.usingScenario != 0; }
static bool IS_PED_SITTING(Ped ped) { ReadLiving(ped); ++w.seatedReads; return w.sitting; }
}
namespace TASK
{
static bool IS_PED_GETTING_UP(Ped ped) { ReadLiving(ped); return w.gettingUp; }
static int GET_SCRIPT_TASK_STATUS(Ped ped, Hash task, bool)
{
    ReadLiving(ped);
    if (task != Joaat("SCRIPT_TASK_COMBAT")) { ++w.routineTaskReads; return w.routineStatus; }
    ++w.combatTaskReads; return w.combatStatus;
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
// The runtime suite checks the native evidence behind this read-only boundary;
// these observer tests check that unconfirmed intended activities never get labels.
namespace RoutineActivityBridge
{
static bool Active(Ped ped, const RoutineActivity::Point&, bool = false)
{
    ReadLiving(ped); ++w.assignedActivityReads; return w.assignedActivity;
}
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
    C.combatExitPending = true;
    Check(Activity() == "Preparing to fight", "pending chair exit does not claim the combat task is already running");
    w.gettingUp = true;
    Check(Activity() == "Getting up", "physical recovery retains priority over pending chair exit");
    w.gettingUp = C.combatExitPending = false;
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
static void CombatEvidenceBeforePriority()
{
    struct Priority { TargetAI::State state; bool pending, hogtied, routine; const char* label; };
    for (const auto& priority : std::array<Priority, 5>{{
        {TargetAI::State::Engaged, false, false, true, "Fighting"},
        {TargetAI::State::Search, false, false, true, "Searching for player"},
        {TargetAI::State::Wander, true, false, true, "Preparing to fight"},
        {TargetAI::State::Engaged, false, true, true, "Hogtied"},
        {TargetAI::State::Engaged, false, false, false, "No town routine"}}})
    {
        Fixture(); C.ai.state = priority.state; C.ai.pendingEngagement = priority.pending;
        w.hogtied = priority.hogtied; R.enabled = priority.routine;
        w.combatStatus = 0; w.combatPlayer = true;
        w.usingScenario = Joaat("WORLD_HUMAN_STARE_STOIC"); w.sitting = true;
        const auto snapshot = ObserveRoutineDebug();
        Check(std::strcmp(snapshot.doing, priority.label) == 0, "combat evidence preserves the existing activity priority");
        Check(snapshot.combatTaskStatus == 0 && snapshot.nativeCombat && snapshot.inScenario && snapshot.seated,
            "queued combat and both physical-state flags are captured before priority returns");
        Check(w.combatTaskReads == 1 && w.combatPlayerReads == 1 && w.scenarioReads == 1 && w.seatedReads == 1,
            "priority snapshot samples each combat evidence source exactly once");
        const auto lines = RoutineDebugView::Format(snapshot);
        if (priority.routine && !priority.hogtied)
            Check(lines[3].find("[task 0, engine Y, SCENARIO, SEATED]") != std::string::npos,
                "priority display exposes queued combat despite an engine combat flag and a seated target");
        else Check(lines[3].find("[task") == std::string::npos, "restraint and absent-routine labels keep their own meaning");
    }
    Fixture(); C.ai.state = TargetAI::State::Engaged; w.combatStatus = 1;
    auto snapshot = ObserveRoutineDebug();
    Check(snapshot.combatTaskStatus == 1 && !snapshot.nativeCombat && !snapshot.inScenario && !snapshot.seated,
        "standing active-task evidence remains independent of the engine combat flag");
    Check(RoutineDebugView::Format(snapshot)[3] == "Doing: Fighting [task 1, engine N]",
        "standing combat omits inactive scenario and seated flags");
    w.combatStatus = 7; w.sitting = true;
    snapshot = ObserveRoutineDebug();
    Check(snapshot.combatTaskStatus == 7 && snapshot.seated && !snapshot.inScenario,
        "sitting without a reported scenario and a missing combat task remain observable");
}
static void AmbientRecoveryLabels()
{
    for (int status : {0, 1, 7})
    {
        Fixture(); R.ambientFallback = true; R.destinationValid = false; R.controller.state = Routine::State::Waiting;
        R.selectPending = true; w.routineStatus = status;
        const auto snapshot = ObserveRoutineDebug();
        Check(snapshot.hasDestination && snapshot.fallback && !snapshot.destinationValid &&
            std::strcmp(snapshot.destination, RoutineData::kLocations[R.destination].name) == 0,
            "ambient recovery preserves and identifies its authored work stop as a fallback");
        Check(snapshot.taskActive == (status != 7) && w.routineTaskReads == 1,
            "waiting controller reads the actual fallback wander task instead of defaulting to inactive");
        Check(std::strcmp(snapshot.doing, status == 7 ? "Wander task pending / recovery" : "Wandering while route recovers") == 0,
            "pending route retry reports healthy ambient wandering or the real missing task");
        const auto lines = RoutineDebugView::Format(snapshot);
        Check(lines[3].find("Waiting for a usable destination") == std::string::npos &&
            lines[4].find("[fallback]") != std::string::npos,
            "rendered recovery names its fallback without advertising stationary waiting");
        UpdateRoutineDebug();
        Check(w.markerHasDestination,
            "debug destination marker retains the cached authored fallback during route validation retries");
    }
    Fixture(); R.ambientFallback = true; R.controller.state = Routine::State::Waiting;
    R.selectPending = true; w.routineStatus = 7;
    w.usingScenario = Joaat("WORLD_HUMAN_SMOKE");
    Check(Activity() == "Smoking (ambient)" && ObserveRoutineDebug().taskActive,
        "a pending route retry leaves observed fallback smoking visible and healthy");
    w.usingScenario = Joaat("WORLD_HUMAN_DRINKING");
    Check(Activity() == "Drinking (ambient)", "fallback drinking retains its specific native scenario label");
    w.usingScenario = Joaat("WORLD_HUMAN_STARE_STOIC");
    Check(Activity() == "Ambient scenario", "another observed fallback scenario is not mislabeled as generic wandering");
    C.ai.state = TargetAI::State::Engaged;
    Check(Activity() == "Fighting", "combat priority wins over an ambient recovery retry");
    w.hogtied = true;
    Check(Activity() == "Hogtied", "physical restraint wins over stale ambient recovery and combat");
    w.hogtied = false; C.ai.state = TargetAI::State::Wander; w.loaded = false;
    Check(Activity() == "Paused: area not loaded", "unloaded target does not advertise active recovery wandering");
    w.loaded = true; R.controller.state = Routine::State::Suspended;
    Check(Activity() == "Routine paused", "suspended target does not advertise active recovery wandering");

    Fixture(); R.ambientFallback = true; R.controller.state = Routine::State::Waiting; R.selectPending = true;
    std::array<unsigned char, sizeof(R)> before{};
    std::memcpy(before.data(), &R, sizeof(R));
    const Vector3 target = w.target;
    for (int repeat = 0; repeat < 5; ++repeat) ObserveRoutineDebug();
    Check(std::memcmp(before.data(), &R, sizeof(R)) == 0 && DistSq(target, w.target) == 0 && w.markerUpdates == 0,
        "fallback debug observations never select a route, alter task state, move the ped or update markers");
    Fixture(); R.destinationValid = false;
    UpdateRoutineDebug();
    Check(!w.markerHasDestination, "a rejected uncached destination still cannot create a debug destination marker");
}
static void ScheduleAndReadOnlySnapshot()
{
    struct Case { int minute, nextMinute; Routine::Phase phase, nextPhase; };
    using Routine::Phase;
    for (const auto& item : std::array<Case, 16>{{
        {0,180,Phase::Leisure,Phase::Rest}, {179,180,Phase::Leisure,Phase::Rest},
        {180,360,Phase::Rest,Phase::Work}, {359,360,Phase::Rest,Phase::Work},
        {360,660,Phase::Work,Phase::Lunch}, {659,660,Phase::Work,Phase::Lunch},
        {660,720,Phase::Lunch,Phase::Work}, {719,720,Phase::Lunch,Phase::Work},
        {720,960,Phase::Work,Phase::Shops}, {959,960,Phase::Work,Phase::Shops},
        {960,1140,Phase::Shops,Phase::Leisure}, {1139,1140,Phase::Shops,Phase::Leisure},
        {1140,180,Phase::Leisure,Phase::Rest}, {1439,180,Phase::Leisure,Phase::Rest},
        {1440,180,Phase::Leisure,Phase::Rest}, {-1,180,Phase::Leisure,Phase::Rest}}})
    for (int offset : {-60, -30, 0, 30, 60})
    {
        Fixture(); w.minute = item.minute; R.plan.offsetMinutes = offset;
        const auto snapshot = ObserveRoutineDebug();
        Check(snapshot.nextMinute == item.nextMinute && std::strcmp(snapshot.nextDestination,
            RoutineData::kLocations[R.plan.route[static_cast<int>(item.nextPhase)]].name) == 0,
            "next destination uses exact activity boundary across midnight and ignores legacy offsets");
        Check(std::strcmp(snapshot.intended, Routine::PhaseName(item.phase)) == 0 &&
            std::strcmp(snapshot.nextActivity, Routine::PhaseName(item.nextPhase)) == 0,
            "intended and next activity describe the exact current and next time windows");
    }
    Fixture(); w.minute = 660;
    const auto lunch = ObserveRoutineDebug();
    Check(lunch.nextMinute == 720 && std::strcmp(lunch.nextDestination,
        RoutineData::kLocations[R.plan.route[static_cast<int>(Phase::Work)]].name) == 0,
        "lunch always advertises a return to the target's same assigned workplace");
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
static void AssignedActivityEvidenceAndPriority()
{
    struct Case { RoutineActivity::Kind kind; const char* label; };
    for (const auto& item : std::array<Case, 6>{{
        {RoutineActivity::Kind::Work,"Working (scenario)"}, {RoutineActivity::Kind::Eat,"Eating (scenario)"},
        {RoutineActivity::Kind::Drink,"Drinking (scenario)"}, {RoutineActivity::Kind::Social,"Social activity (scenario)"},
        {RoutineActivity::Kind::Rest,"Resting (scenario)"}, {RoutineActivity::Kind::Sleep,"Sleeping (scenario)"}}})
    {
        Fixture(); R.controller.state = Routine::State::Wandering;
        R.activity.state = RoutineActivity::State::Active;
        R.activity.point = {42, item.kind, Joaat("WORLD_HUMAN_STARE_STOIC")};
        Check(Activity() == "Wandering near destination",
            "active controller metadata without native confirmation cannot claim assigned activity");
        w.usingScenario = R.activity.point.hash;
        Check(Activity() == "Ambient scenario",
            "a generic observed scenario is not proof of the exact assigned activity");
        w.assignedActivity = true;
        Check(Activity() == item.label, "confirmed matching assigned scenario produces its specific observed action");
        R.activityPointValid = false;
        Check(Activity() == "Ambient scenario", "invalidated point cannot advertise assigned activity even with stale native use evidence");
        R.activityPointValid = true;
        w.assignedActivity = false;
        Check(Activity() == "Ambient scenario", "loss of native confirmation immediately removes assigned action label");
        w.assignedActivity = true; R.selectPending = true;
        Check(Activity() == item.label, "a pending route check does not hide a still-performing assigned scenario");
        R.activity.state = RoutineActivity::State::Entering;
        Check(Activity() == "Entering activity scenario", "entry state does not prematurely claim eating, sleeping or working");
        R.activity.state = RoutineActivity::State::Exiting;
        Check(Activity() == "Exiting activity scenario", "exit state remains distinct even while the old scenario is still active");
        R.controller.state = Routine::State::Suspended;
        Check(Activity() == "Exiting activity scenario",
            "after an interruption, route suspension cannot hide a routine scenario exit already underway");
        w.loaded = false;
        Check(Activity() == "Paused: area not loaded", "unloaded area retains priority over the recorded exit transition");
        w.loaded = true;
        C.ai.state = TargetAI::State::Search;
        Check(Activity() == "Searching for player", "search priority overrides routine exit/active labels");
        C.ai.state = TargetAI::State::Engaged;
        Check(Activity() == "Fighting", "combat priority overrides routine activity labels");
        C.combatExitPending = true;
        Check(Activity() == "Preparing to fight", "combat scenario exit retains its higher-priority distinct label");
        w.hogtied = true;
        Check(Activity() == "Hogtied", "restraint priority overrides every assigned scenario label");
    }
    for (int minute : {600, 660, 800, 960, 1140, 180})
    {
        Fixture(); w.minute = minute; R.activityFallback = true;
        R.controller.state = Routine::State::Wandering;
        const auto snapshot = ObserveRoutineDebug();
        Check(snapshot.fallback && Activity() == "Wandering while route recovers",
            "failed or unavailable activity shows ambient fallback independently of intended time window");
        w.routineStatus = 7;
        Check(Activity() == "Wander task pending / recovery", "missing activity-fallback wander task is not called a successful action");
        R.controller.state = Routine::State::Travelling; w.routineStatus = 1;
        Check(Activity() == "Walking to destination" && !ObserveRoutineDebug().fallback,
            "previous activity failure cannot mislabel current travel as wandering or the new stop as failed");
    }
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
    ExistenceDeathAndFreshCoordinates(); PriorityAndScenarioLabels(); CombatEvidenceBeforePriority(); AmbientRecoveryLabels();
    ScheduleAndReadOnlySnapshot(); AssignedActivityEvidenceAndPriority(); SamplingRenderingAndEarlyToggle();
    std::printf("Routine debug bridge: %u checks passed.\n", checks);
}
