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
} C;
static Ped pedMe = 1;
static int me = 0;
static Vector3 playerPos;
static struct NativeState {
    std::uint64_t now = 0;
    bool los = true, looking = false, aiming = false, intimidated = false, combat = false;
    bool ragdoll = false, gettingUp = false, hogtied = false, beingHogtied = false, lassoed = false;
    int taskStatus = 7;
    int removes = 0, gives = 0, draws = 0, combats = 0, searches = 0, wanders = 0;
    std::vector<Hash> weapons = { 99u };
    std::map<int, bool> combatAttributes, configFlags, fleeAttributes;
    Vector3 searchPoint;
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
static bool IS_PED_IN_COMBAT(Ped, Ped) { return N.combat; }
static bool IS_PED_RAGDOLL(Ped) { return N.ragdoll; }
static bool IS_PED_HOGTIED(Ped) { return N.hogtied; }
static bool IS_PED_BEING_HOGTIED(Ped) { return N.beingHogtied; }
static bool IS_PED_LASSOED(Ped) { return N.lassoed; }
}
namespace TASK {
static void SET_PED_PATH_PREFER_TO_AVOID_WATER(Ped, bool, float) {}
static void SET_PED_PATH_MAY_ENTER_WATER(Ped, bool) {}
static void TASK_WANDER_IN_AREA(Ped, Vector3, float, float, float, int) { ++N.wanders; }
static void TASK_COMBAT_PED(Ped, Ped, int, int) { ++N.combats; }
static int GET_SCRIPT_TASK_STATUS(Ped, Hash, bool) { return N.taskStatus; }
static bool IS_PED_GETTING_UP(Ped) { return N.gettingUp; }
static void TASK_GO_TO_COORD_ANY_MEANS(Ped, Vector3 point, float, int, bool, int, float) { ++N.searches; N.searchPoint = point; }
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
static const ContractDef def;
static void Reset()
{
    C = {}; N = {}; playerPos = { 10.0f, 0, 0 };
    SetupHumanTarget(target, def);
}
static void Tick(std::uint64_t now) { N.now = now; UpdateHumanTarget(target, def); }

static void LoadoutsAndFleeConfiguration()
{
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
    N.taskStatus = 0; Tick(3001); N.taskStatus = 1; Tick(6001);
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
    Reset(); N.combat = true; Tick(1); Tick(4001);
    Check(N.combats == 0 && N.draws == 0 && C.ai.state == TargetAI::State::Engaged,
        "existing native combat is adopted without replacing task or weapon");
}

int main()
{
    LoadoutsAndFleeConfiguration();
    RestrainedTransitionsWait();
    CombatTaskStatusRecoveryAndSearch();
    std::puts("Target AI native bridge: 3 scenario groups passed.");
}
