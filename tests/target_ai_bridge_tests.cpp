#include "../rdr2 scripting environment/samples/Pools/target_ai_logic.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <vector>

using Ped = int;
using Hash = unsigned int;
using DWORD = unsigned long;
struct Vector3 { float x = 0, y = 0, z = 0; };
struct ContractDef { Vector3 spawn; float searchRadius = 20.0f; };
constexpr int CAL_PROFESSIONAL = 2, CR_MEDIUM = 1, WEAPON_ATTACH_POINT_HAND_PRIMARY = 0;
constexpr Hash WEAPON_REVOLVER_CATTLEMAN = 2, WEAPON_MELEE_KNIFE = 3, ADD_REASON_DEFAULT = 4;

static void Check(bool condition, const char* description)
{
    if (!condition) { std::fprintf(stderr, "FAILED: %s\n", description); std::exit(EXIT_FAILURE); }
}

static Hash joaat(const char* text) { return text[0] == 'W' ? 1u : 5u; }
static float DistSq(const Vector3& a, const Vector3& b)
{
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}
static bool Within(const Vector3& a, const Vector3& b, float distance) { return DistSq(a, b) <= distance * distance; }

static struct Contract {
    Hash weapon = 0;
    Vector3 targetPos, lastKnownPlayerPos;
    bool damagedByPlayer = false;
    TargetAI::Memory ai;
    std::uint64_t combatRequestMs = 0;
    bool combatExitPending = false, combatExitRecovery = false;
} C;
static Ped pedMe = 1;
static int me = 0;
static Vector3 playerPos;
enum class NativeCall { CombatExit, NormalExit, ImmediateExit, DirectedExit, Clear, ImmediateClear, Combat, Search,
    WanderClear, Wander };
static struct NativeState {
    std::uint64_t now = 0;
    bool los = true, looking = false, aiming = false, intimidated = false, combat = false;
    bool ragdoll = false, gettingUp = false, hogtied = false, beingHogtied = false, lassoed = false;
    bool otherCombat = false, inVehicle = false;
    bool usingScenario = false, sitting = false, exitingScenario = false;
    bool combatExitSucceeds = true, directedExitSucceeds = true;
    int taskStatus = 7;
    int removes = 0, gives = 0, draws = 0, combats = 0, searches = 0, wanders = 0;
    int scenarioExits = 0, immediateExits = 0, directedExits = 0, clearsImmediate = 0;
    int normalExits = 0, clears = 0, wanderClears = 0;
    std::vector<NativeCall> calls;
    std::vector<Hash> weapons = { 99u };
    std::map<int, bool> combatAttributes, configFlags, fleeAttributes;
    Vector3 searchPoint, exitPoint, wanderPoint;
    float wanderRadius = 0, avoidWaterRadius = 0;
} N;
static std::uint64_t RuntimeNowMs() { return N.now; }

namespace PED {
static void SET_PED_CONFIG_FLAG(Ped, int flag, bool value) { N.configFlags[flag] = value; }
static void SET_PED_COMBAT_ATTRIBUTES(Ped, int flag, bool value) { N.combatAttributes[flag] = value; }
static void SET_PED_FLEE_ATTRIBUTES(Ped, int flag, bool value) { N.fleeAttributes[flag] = value; }
static void SET_PED_KEEP_TASK(Ped, bool) {}
static void SET_PED_COMBAT_ABILITY(Ped, int) {}
static void SET_PED_CAN_BE_INCAPACITATED(Ped, bool) {}
static void SET_PED_COMBAT_RANGE(Ped, int) {}
static void SET_PED_COMBAT_MOVEMENT(Ped, int) {}
static bool IS_PED_HEADTRACKING_PED(Ped, Ped) { return N.looking; }
static bool _IS_PED_INTIMIDATED(Ped) { return N.intimidated; }
static bool IS_PED_IN_COMBAT(Ped, Ped opponent) { return N.combat || (opponent == 0 && N.otherCombat); }
static bool IS_PED_RAGDOLL(Ped) { return N.ragdoll; }
static bool IS_PED_HOGTIED(Ped) { return N.hogtied; }
static bool IS_PED_BEING_HOGTIED(Ped) { return N.beingHogtied; }
static bool IS_PED_LASSOED(Ped) { return N.lassoed; }
static bool IS_PED_USING_ANY_SCENARIO(Ped) { return N.usingScenario; }
static bool IS_PED_SITTING(Ped) { return N.sitting; }
static bool SET_PED_SHOULD_PLAY_COMBAT_SCENARIO_EXIT(Ped, Vector3 point, int intensity)
{
    Check(intensity == 3, "combat scenario exit uses high look intensity");
    ++N.scenarioExits; N.exitPoint = point; N.calls.push_back(NativeCall::CombatExit);
    return N.combatExitSucceeds;
}
static void SET_PED_SHOULD_PLAY_NORMAL_SCENARIO_EXIT(Ped)
{
    ++N.normalExits; N.calls.push_back(NativeCall::NormalExit);
}
[[maybe_unused]] static void SET_PED_SHOULD_PLAY_IMMEDIATE_SCENARIO_EXIT(Ped)
{
    ++N.immediateExits; N.calls.push_back(NativeCall::ImmediateExit);
}
static bool SET_PED_SHOULD_PLAY_DIRECTED_NORMAL_SCENARIO_EXIT(Ped, Vector3 point)
{
    ++N.directedExits; N.exitPoint = point; N.calls.push_back(NativeCall::DirectedExit);
    return N.directedExitSucceeds;
}
}
namespace TASK {
static void SET_PED_PATH_PREFER_TO_AVOID_WATER(Ped, bool avoid, float radius)
{
    Check(avoid, "native wandering keeps the baseline water avoidance");
    N.avoidWaterRadius = radius;
}
static void SET_PED_PATH_MAY_ENTER_WATER(Ped, bool allow)
{
    Check(!allow, "native wandering keeps the baseline restriction on entering water");
}
static void TASK_WANDER_IN_AREA(Ped, Vector3 point, float radius, float first, float second, int last)
{
    Check(first == 0.0f && second == 0.0f && last == 1,
        "native wandering preserves the known-working 61f3ad8 task arguments");
    Check(!N.calls.empty() && N.calls.back() == NativeCall::WanderClear,
        "a native wander transition follows the baseline task clear");
    ++N.wanders; N.wanderPoint = point; N.wanderRadius = radius;
    N.calls.push_back(NativeCall::Wander);
}
static void TASK_COMBAT_PED(Ped, Ped, int, int) { ++N.combats; N.calls.push_back(NativeCall::Combat); }
static int GET_SCRIPT_TASK_STATUS(Ped, Hash, bool) { return N.taskStatus; }
static bool IS_PED_GETTING_UP(Ped) { return N.gettingUp; }
static bool IS_PED_EXITING_SCENARIO(Ped, bool p1)
{
    Check(p1, "scenario exit observation uses the source-backed true argument");
    return N.exitingScenario;
}
static void TASK_GO_TO_COORD_ANY_MEANS(Ped, Vector3 point, float, int, bool, int, float)
{
    ++N.searches; N.searchPoint = point; N.calls.push_back(NativeCall::Search);
}
static void CLEAR_PED_TASKS(Ped, bool p1, bool p2)
{
    Check(p1, "ordinary task clear retains the source-backed first native argument");
    if (p2) { ++N.wanderClears; N.calls.push_back(NativeCall::WanderClear); }
    else { ++N.clears; N.calls.push_back(NativeCall::Clear); }
}
[[maybe_unused]] static void CLEAR_PED_TASKS_IMMEDIATELY(Ped, bool p1, bool resetCrouch)
{
    Check(!p1 && resetCrouch, "immediate task clear retains the production native arguments");
    ++N.clearsImmediate; N.calls.push_back(NativeCall::ImmediateClear);
}
}
namespace ENTITY { static bool HAS_ENTITY_CLEAR_LOS_TO_ENTITY(Ped, Ped, int) { return N.los; } }
namespace PLAYER { static bool IS_PLAYER_FREE_AIMING_AT_ENTITY(int, Ped) { return N.aiming; } }
namespace WEAPON {
static void REMOVE_ALL_PED_WEAPONS(Ped, bool, bool) { ++N.removes; N.weapons.clear(); }
static void GIVE_WEAPON_TO_PED(Ped, Hash weapon, int, bool, bool, int, bool, float, float, Hash, bool, float, bool)
{
    Check(N.removes == 1, "clear inherited inventory before adding the selected weapon");
    ++N.gives; N.weapons.push_back(weapon);
}
static void SET_CURRENT_PED_WEAPON(Ped, Hash, bool, int, bool, bool) { ++N.draws; }
}

#include "target_ai_bridge_under_test.h"

static const Ped target = 2;
static const ContractDef def = { { 1370.0f, -1354.0f, 78.0f }, 65.0f };
static void Reset()
{
    C = {}; N = {}; playerPos = { 10.0f, 0, 0 };
    SetupHumanTarget(target, def);
    Check(N.calls == std::vector<NativeCall>{ NativeCall::WanderClear, NativeCall::Wander },
        "setup submits exactly one native wander transition without a scripted activity or pause");
    Check(DistSq(N.wanderPoint, def.spawn) == 0 && N.wanderRadius == def.searchRadius &&
        N.avoidWaterRadius == def.searchRadius, "native wandering uses the selected original contract's anchor and radius");
    N.calls.clear();
}
static void Tick(std::uint64_t now)
{
    N.now = now; UpdateHumanTarget(target, def);
    Check(N.immediateExits == 0 && N.clearsImmediate == 0,
        "combat and search never snap scenarios with immediate exit or task-clear natives");
}

static void LoadoutsAndFleeConfiguration()
{
    Reset();
    Check(N.configFlags[211], "every target retains the baseline mission-ped ambient default-task setting");
    bool sawUnarmed = false, sawKnife = false, sawGun = false;
    std::srand(1);
    for (int i = 0; i < 256; ++i)
    {
        Reset();
        Check(N.removes == 1 && std::find(N.weapons.begin(), N.weapons.end(), 99u) == N.weapons.end(),
            "every loadout removes the archetype's inherited gun exactly once");
        const bool unarmed = C.weapon == joaat("WEAPON_UNARMED");
        Check(N.weapons.size() == (unarmed ? 0u : 1u), "inventory contains only the selected loadout");
        Check(unarmed || N.weapons[0] == C.weapon, "selected weapon matches native inventory");
        Check(N.fleeAttributes[32768] && N.fleeAttributes[512], "hostile flee flags match Flaco enemy setup");
        sawUnarmed = sawUnarmed || unarmed;
        sawKnife = sawKnife || C.weapon == WEAPON_MELEE_KNIFE;
        sawGun = sawGun || C.weapon == WEAPON_REVOLVER_CATTLEMAN;
    }
    Check(sawUnarmed && sawKnife && sawGun, "exercise all three production loadout branches");
    const Hash original = C.weapon;
    EnterCombat(target, false, false);
    EnterCombat(target, false, true);
    EnterCombat(target, true, false);
    Check(C.weapon == original && N.removes == 1 && N.draws == 1 && N.combats == 2,
        "engage/recovery/adoption retain loadout and only first engagement draws it");
}

static void RestrainedTransitionsWait()
{
    using Flag = bool NativeState::*;
    for (Flag restrained : { &NativeState::ragdoll, &NativeState::gettingUp, &NativeState::hogtied,
        &NativeState::beingHogtied, &NativeState::lassoed })
    {
        Reset(); N.*restrained = true; C.damagedByPlayer = true;
        Tick(1); C.damagedByPlayer = false; Tick(10000);
        Check(N.combats == 0 && N.draws == 0, "no combat or weapon draw during any restraint state");
        N.*restrained = false; Tick(10001);
        Check(N.combats == 1 && C.ai.state == TargetAI::State::Engaged,
            "consumed damage engages once restraint ends");
        N.los = false; N.*restrained = true; Tick(18001);
        Check(N.searches == 0, "expired contact grace cannot replace a restraint with search");
        N.*restrained = false; Tick(18002);
        Check(N.searches == 1, "search can start after restraint ends");
        N.*restrained = true; Tick(28002);
        Check(N.wanders == 1, "expired search cannot replace a restraint with wandering");
        N.*restrained = false; Tick(28003);
        Check(N.wanders == 2, "wandering resumes after restraint ends");
    }
}

static void CombatTaskStatusRecoveryAndSearch()
{
    Reset(); C.damagedByPlayer = true; Tick(1); C.damagedByPlayer = false;
    N.taskStatus = 0; Tick(2001); N.taskStatus = 1; Tick(6001);
    Check(N.combats == 1, "both pending and active native task statuses suppress recovery");
    N.taskStatus = 7; Tick(6002); Tick(7001);
    Check(N.combats == 1, "task absence receives a full one-second grace");
    Tick(7002);
    Check(N.combats == 2 && N.draws == 1, "sustained missing task recovers without forcing weapon draw");
    N.combat = true; N.los = false; playerPos = { 100.0f, 0, 0 }; Tick(15002);
    Check(N.searches == 1 && N.searchPoint.x == 10.0f,
        "persistent native combat cannot reveal unseen player position or prevent search");
    Tick(15003); Tick(25002);
    Check(N.searches == 1 && N.wanders == 2, "native combat flag does not repeat transitions");
    N.los = true; playerPos = { 10.0f, 0, 0 }; Tick(25003);
    Check(N.combats == 3 && C.ai.state == TargetAI::State::Engaged,
        "returning into sight with a stale combat flag issues a new combat task after search");
    Reset(); N.combat = true; Tick(1); Tick(4001);
    Check(N.combats == 0 && N.draws == 0 && C.ai.state == TargetAI::State::Engaged,
        "existing native combat is adopted without replacing task or weapon");
    Check(C.combatRequestMs == 1, "standing adoption records its request once without refreshing every frame");
    Reset(); N.combat = true; Tick(0);
    N.combat = false; Tick(200); Tick(1199);
    Check(N.combats == 0, "dropped native adoption receives the full missing-task grace");
    Tick(1200);
    Check(N.combats == 1 && N.draws == 0 && C.combatRequestMs == 1200,
        "the first real task repairs a dropped adoption without a fictitious retry delay or weapon draw");
}

static void SeatedEngagementRequestsExit()
{
    using Flag = bool NativeState::*;
    for (Flag seated : { &NativeState::usingScenario, &NativeState::sitting })
    {
        for (bool adopt : { false, true })
        {
            Reset(); N.*seated = true; N.combat = adopt; C.damagedByPlayer = !adopt;
            Tick(1); C.damagedByPlayer = false;
            Check(N.scenarioExits == 1 && N.clears == 1 && N.combats == 0 && N.draws == 0,
                "seated provocation and adoption request one ordinary exit before drawing or fighting");
            Check(N.calls == std::vector<NativeCall>{ NativeCall::CombatExit, NativeCall::Clear },
                "combat scenario exit hint precedes the ordinary clear without assigning combat");
            Check(C.combatExitPending && !C.combatExitRecovery,
                "seated first engagement remembers its deferred first weapon draw");
            Check(N.exitPoint.x == playerPos.x && C.combatRequestMs == 1,
                "combat exit faces the player and every request records its time");
            Tick(2); Tick(1000);
            Check(N.scenarioExits == 1 && N.clears == 1 && N.combats == 0 && N.clearsImmediate == 0,
                "engagement grants the exit time to complete without per-frame tasking");
            N.*seated = false; N.exitingScenario = true;
            Tick(2501); Tick(3501);
            Check(N.clears == 1 && N.combats == 0 && N.draws == 0 && C.combatRequestMs == 1,
                "an active stand-up exit receives time even after sitting/scenario flags retire");
            N.exitingScenario = false; Tick(3502);
            Check(!C.combatExitPending && N.combats == 1 && N.draws == 1,
                "completed scenario exit starts the first combat task and draws exactly once");
            N.taskStatus = 1; Tick(3503); Tick(7000);
            Check(N.combats == 1 && N.draws == 1 && N.clears == 1 && N.immediateExits == 0,
                "healthy standing combat never repeats exit completion or immediate operations");
        }
    }

    Reset(); N.usingScenario = true; N.combatExitSucceeds = false; C.damagedByPlayer = true;
    Tick(1);
    Check(N.calls == std::vector<NativeCall>{ NativeCall::CombatExit, NativeCall::NormalExit, NativeCall::Clear },
        "rejected combat exit falls back to normal exit before an ordinary clear");
    Check(N.normalExits == 1 && N.immediateExits == 0 && N.clearsImmediate == 0 && N.combats == 0 && N.draws == 0,
        "fallback preserves a pending animation exit without immediate hints, clears, combat or drawing");

    Reset(); N.usingScenario = N.combat = true; Tick(0);
    N.usingScenario = N.combat = false; Tick(200); Tick(201); Tick(1201);
    Check(N.combats == 1 && C.ai.hasIssuedCombatTask,
        "a real task issued during seated adoption receives the full scripted retry interval");
    Tick(3200);
    Check(N.combats == 2 && N.draws == 1,
        "dropped seated-adoption task recovers after three seconds without redrawing the weapon");
}

static void SeatedCombatRecovery()
{
    using Flag = bool NativeState::*;
    for (Flag seated : { &NativeState::usingScenario, &NativeState::sitting })
    {
        for (int status : { 0, 1 })
        {
            Reset(); N.*seated = true; N.taskStatus = status; C.damagedByPlayer = true;
            Tick(1); C.damagedByPlayer = false; N.combat = true;
            Tick(2500);
            Check(!C.ai.taskMissing && N.combats == 0, "pending seated combat gets the full 2.5-second settle window");
            Tick(2501);
            Check(C.ai.taskMissing && N.combats == 0,
                "a persistent seat makes even engine combat and performing status unhealthy after settling");
            Tick(3500);
            Check(N.clears == 1 && N.combats == 0, "stuck seated combat also receives the full missing-task grace");
            Tick(3501);
            Check(N.clears == 2 && N.clearsImmediate == 0 && N.combats == 0 && N.draws == 0 && C.combatRequestMs == 3501,
                "stuck recovery retries the ordinary exit without snapping, combat or weapon draw");
            Check(N.calls == std::vector<NativeCall>{ NativeCall::CombatExit, NativeCall::Clear,
                NativeCall::CombatExit, NativeCall::Clear }, "stuck recovery repeats the exit hint then ordinary clear");
            Check(C.combatExitPending && !C.combatExitRecovery,
                "gentle retry preserves the first engagement's deferred weapon draw");
            N.*seated = false; N.taskStatus = 1;
            Tick(3502); Tick(7000); Tick(10000);
            Check(N.combats == 1 && N.draws == 1 && !C.ai.taskMissing && !C.combatExitPending,
                "standing completion after gentle retries draws once and ends recovery tasking");
        }
    }
    for (Flag restrained : { &NativeState::ragdoll, &NativeState::gettingUp, &NativeState::hogtied,
        &NativeState::beingHogtied, &NativeState::lassoed })
    {
        Reset(); N.usingScenario = true; N.taskStatus = 0; C.damagedByPlayer = true;
        Tick(1); C.damagedByPlayer = false; Tick(2501);
        N.*restrained = true; Tick(3501); Tick(10000);
        Check(N.clears == 1 && N.clearsImmediate == 0 && N.combats == 0 && !C.ai.taskMissing,
            "a stuck seated target is never cleared or re-tasked during restraint or getting up");
        N.*restrained = false; Tick(10001); Tick(11000);
        Check(N.clears == 1, "release from restraint starts a new missing-task grace");
        Tick(11001);
        Check(N.clears == 2 && N.clearsImmediate == 0 && N.combats == 0,
            "gentle seated recovery resumes only after restraint ends and the fresh grace expires");
        N.usingScenario = false; N.*restrained = true; Tick(11002);
        Check(N.combats == 0 && C.combatExitPending, "pending combat cannot complete while target is restrained");
        N.*restrained = false; Tick(11003);
        Check(N.combats == 1 && N.draws == 1 && !C.combatExitPending,
            "pending first engagement completes once the standing target can act");
    }

    Reset(); C.damagedByPlayer = true; Tick(1); C.damagedByPlayer = false;
    N.usingScenario = true; N.taskStatus = 0; Tick(2501); Tick(3501);
    Check(C.combatExitPending && C.combatExitRecovery && N.combats == 1 && N.draws == 1,
        "scenario recovery during an existing fight remembers that its weapon was already drawn");
    N.usingScenario = false; N.taskStatus = 1; Tick(3502);
    Check(N.combats == 2 && N.draws == 1 && !C.combatExitPending,
        "exit completion repairs an existing fight without drawing its weapon again");
}

static void ExitingAnimationReceivesBoundedProtection()
{
    Reset(); N.exitingScenario = true; C.damagedByPlayer = true;
    Tick(1); C.damagedByPlayer = false;
    Check(C.combatExitPending && N.scenarioExits == 0 && N.clears == 0 && N.combats == 0 && N.draws == 0,
        "first engagement leaves an already-running scenario exit intact");
    Tick(2501); Tick(3501); Tick(8000);
    Check(N.calls.empty() && !C.ai.taskMissing && C.combatRequestMs == 1,
        "an active exit receives the eight-second allowance from one fixed request timestamp");
    Tick(8001); Tick(9000);
    Check(C.ai.taskMissing && N.clears == 0, "a forever-exiting flag gets missing-task grace after its allowance");
    Tick(9001);
    Check(N.scenarioExits == 1 && N.clears == 1 && N.combats == 0 && N.draws == 0 && C.combatRequestMs == 9001,
        "a stuck exit is gently retried after its fixed allowance and grace");
    Check(!C.combatExitRecovery && N.immediateExits == 0 && N.clearsImmediate == 0,
        "bounded exit retry preserves the first draw and never requests immediate detachment");
    N.exitingScenario = false; Tick(9002);
    N.taskStatus = 1; Tick(9003); Tick(13000);
    Check(N.combats == 1 && N.draws == 1 && !C.combatExitPending,
        "the deferred fight starts exactly once after an initially active exit completes");

    Reset(); N.usingScenario = true; C.damagedByPlayer = true; Tick(1); C.damagedByPlayer = false;
    Tick(2501);
    Check(C.ai.taskMissing && C.combatExitPending, "late-completion probe starts with an expired pending-exit grace");
    N.usingScenario = false; Tick(20000);
    Check(N.combats == 1 && N.draws == 1 && !C.combatExitPending && !C.ai.taskMissing && N.clears == 1,
        "late exit completion retains its first draw instead of being mistaken for a failed fight recovery");
}

static void QueuedCombatRecovery()
{
    Reset(); N.taskStatus = 0; C.damagedByPlayer = true;
    Tick(1); C.damagedByPlayer = false;
    Tick(2500);
    Check(!C.ai.taskMissing, "a queued standing combat task is healthy while settling");
    Tick(2501); Tick(3500);
    Check(C.ai.taskMissing && N.combats == 1, "queued combat starts the absence grace after settling");
    Tick(3501);
    Check(N.combats == 2 && N.clearsImmediate == 0 && N.scenarioExits == 0 && N.draws == 1,
        "queued-forever standing combat recovers without scenario operations or another draw");
    Tick(3502); Tick(6000);
    Check(N.combats == 2 && !C.ai.taskMissing, "recovery grants a new settle window to its queued task");
    N.taskStatus = 1; Tick(6001); Tick(10000);
    Check(N.combats == 2, "performing combat suppresses any later queued-task recovery");
}

static void SeatedSearchRequestsExit()
{
    using Flag = bool NativeState::*;
    for (Flag seated : { &NativeState::usingScenario, &NativeState::sitting })
    {
        for (bool directedSucceeds : { false, true })
        {
            Reset(); N.*seated = true; C.damagedByPlayer = true; Tick(1); C.damagedByPlayer = false;
            N.directedExitSucceeds = directedSucceeds;
            N.combat = true; N.los = false; playerPos = { 100.0f, 0, 0 }; N.calls.clear();
            Tick(8001);
            const std::vector<NativeCall> expected = directedSucceeds
                ? std::vector<NativeCall>{ NativeCall::DirectedExit, NativeCall::Search }
                : std::vector<NativeCall>{ NativeCall::DirectedExit, NativeCall::NormalExit, NativeCall::Search };
            Check(N.calls == expected && N.directedExits == 1 && N.searches == 1,
                "seated search requests a directed exit with normal fallback before its search task");
            Check(N.exitPoint.x == 10.0f && N.searchPoint.x == 10.0f,
                "search and its exit both face the last seen position instead of the unseen player");
            Check(!N.configFlags[233] && !N.combatAttributes[5] && N.clears == 1 &&
                N.clearsImmediate == 0 && N.immediateExits == 0 && !C.combatExitPending,
                "search clears pending combat while retaining hostile flags and avoiding immediate exit operations");
            Tick(8002); Tick(10000);
            Check(N.directedExits == 1 && N.searches == 1, "seated search does not reissue its task or exit each frame");
            N.*seated = false; Tick(10001);
            Check(N.combats == 0 && N.draws == 0 && C.ai.state == TargetAI::State::Search,
                "standing after search starts never completes the cancelled pending combat request");
        }
    }
}

static void NativeAmbientOwnership()
{
    // Scenario kinds and time-of-day deliberately are not exposed by this bridge.
    // The engine may choose any ambient activity; a calm target is never filtered
    // by an allowlist, stopped after a duration, or re-tasked on an idle status.
    for (int status : { 0, 1, 7, 8 })
    {
        Reset(); N.taskStatus = status;
        for (std::uint64_t frame = 1; frame <= 10000; ++frame)
        {
            N.usingScenario = frame % 5 != 0;
            N.sitting = frame % 3 == 0;
            N.exitingScenario = frame % 17 == 0;
            N.otherCombat = frame % 19 == 0;
            N.inVehicle = frame % 23 == 0;
            Tick(frame * 1000);
            Check(C.ai.state == TargetAI::State::Wander && N.calls.empty(),
                "unprovoked native activity and its transitions remain untouched across long time intervals");
        }
        Check(N.wanders == 1 && N.wanderClears == 1 && N.combats == 0 && N.searches == 0 &&
            N.scenarioExits == 0 && N.normalExits == 0 && N.directedExits == 0,
            "idle task reports and arbitrary native scenario states never trigger scripted walk, pause, or activity recovery");
        Check(N.configFlags[211], "ambient default tasks stay enabled throughout passive observation");
    }
}

static void SearchReturnsNativeOwnership()
{
    for (bool nativeCombatLingers : { false, true })
    {
        Reset(); C.damagedByPlayer = true;
        Tick(1); C.damagedByPlayer = false;
        N.combat = nativeCombatLingers;
        N.los = false; playerPos = { 100.0f, 0, 0 };
        Tick(8001);
        Check(C.ai.state == TargetAI::State::Search && N.searches == 1 && N.searchPoint.x == 10.0f,
            "LOS loss begins one search at the last actually seen player position");
        Tick(18000);
        Check(N.wanders == 1 && N.wanderClears == 1,
            "native wander does not replace the ten-second search before it finishes");
        N.calls.clear();
        Tick(18001);
        Check(C.ai.state == TargetAI::State::Wander &&
            N.calls == std::vector<NativeCall>{ NativeCall::WanderClear, NativeCall::Wander },
            "search completion returns ownership through exactly the baseline native wander transition");
        Check(DistSq(N.wanderPoint, def.spawn) == 0 && N.wanderRadius == def.searchRadius &&
            N.wanders == 2 && N.wanderClears == 2,
            "post-search native wandering retains the original contract area without a fallback route");
        N.calls.clear();
        N.usingScenario = N.sitting = true;
        for (std::uint64_t frame = 1; frame <= 1000; ++frame) Tick(18001 + frame * 1000);
        Check(N.calls.empty() && N.wanders == 2 && !N.configFlags[233] && !N.combatAttributes[5],
            "the resumed native scenario stays uninterrupted even with a stale player-combat flag");
        N.usingScenario = N.sitting = false;
        N.los = true; playerPos = { 10.0f, 0, 0 };
        Tick(1018002);
        Check(C.ai.state == TargetAI::State::Engaged && N.combats == 2 && N.wanders == 2,
            "remembered player reacquisition re-engages from native ambient ownership without another wander task");
    }
}
int main()
{
    LoadoutsAndFleeConfiguration();
    RestrainedTransitionsWait();
    CombatTaskStatusRecoveryAndSearch();
    SeatedEngagementRequestsExit();
    SeatedCombatRecovery();
    ExitingAnimationReceivesBoundedProtection();
    QueuedCombatRecovery();
    SeatedSearchRequestsExit();
    NativeAmbientOwnership();
    SearchReturnsNativeOwnership();
    std::puts("Target AI native bridge: 10 scenario groups passed.");
}
