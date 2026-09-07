#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <vector>
using Hash = unsigned;
using Any = std::uint64_t;
using Ped = int;
using Interior = int;
struct Vector3 { float x = 0, y = 0, z = 0; Vector3() = default; Vector3(float a, float b, float c) : x(a), y(b), z(c) {} };
constexpr Hash Joaat(const char* value)
{
    Hash result = 0;
    while (*value) { unsigned c = static_cast<unsigned>(*value++); if (c >= 'A' && c <= 'Z') c += 'a' - 'A'; result += c; result += result << 10; result ^= result >> 6; }
    result += result << 3; result ^= result >> 11; return result + (result << 15);
}
#include "../rdr2 scripting environment/samples/Pools/routine_plan.h"
namespace Fake
{
struct Scene { int id; Hash hash; Vector3 position; bool enabled = true, compatible = true; Ped user = 0; };
std::vector<Scene> scenes;
bool loaded = true, interiorReady = true, outside = true, nav = true, occupied = false, water = false;
bool base = false, exiting = false, useTask = false, ongoing = false;
int interior = 0, atInterior = 0, approachInterior = 0, activeId = 0, starts = 0, enumerations = 0, compatibleChecks = 0;
Vector3 approachOffset;
Scene* Get(int id) { for (auto& scene : scenes) if (scene.id == id) return &scene; return nullptr; }
void Reset()
{
    scenes.clear(); loaded = interiorReady = outside = nav = true; occupied = water = base = exiting = useTask = ongoing = false;
    interior = atInterior = approachInterior = activeId = starts = enumerations = compatibleChecks = 0; approachOffset = {};
}
}
namespace RoutineSpawn { bool Loaded(const Vector3&) { return Fake::loaded; } }
namespace TASK
{
bool IS_PED_EXITING_SCENARIO(Ped, bool) { return Fake::exiting; }
bool PED_HAS_USE_SCENARIO_TASK(Ped) { return Fake::useTask; }
bool _PED_IS_IN_SCENARIO_BASE(Ped) { return Fake::base; }
bool IS_PED_ACTIVE_IN_SCENARIO(Ped, int mode) { assert(mode == 1); return Fake::ongoing; }
bool DOES_SCENARIO_POINT_EXIST(int id) { return Fake::Get(id) != nullptr; }
Hash _GET_SCENARIO_POINT_TYPE(int id) { return Fake::Get(id)->hash; }
bool _IS_SCENARIO_POINT_ACTIVE(int id) { return Fake::Get(id)->enabled; }
Ped _GET_PED_USING_SCENARIO_POINT(int id) { return Fake::Get(id)->user; }
bool _IS_SCENARIO_IN_USE(int id) { return Fake::Get(id)->user != 0; }
Vector3 _GET_SCENARIO_POINT_COORDS(int id, bool) { return Fake::Get(id)->position; }
bool IS_SCENARIO_TYPE_ENABLED(const char*) { return true; }
int GET_SCENARIO_POINTS_IN_AREA(Vector3, float radius, Any* output, int capacity)
{
    assert(radius == 35.0f && output[0] == static_cast<Any>(capacity));
    assert(reinterpret_cast<std::uintptr_t>(output) % 8 == 0);
    ++Fake::enumerations;
    const int count = (std::min)(capacity, static_cast<int>(Fake::scenes.size()));
    for (int index = 0; index < count; ++index) output[index + 1] = static_cast<Any>(Fake::scenes[index].id);
    return count;
}
void TASK_USE_SCENARIO_POINT(Ped, int id, const char* conditional, int duration, bool enter, bool warp,
    Hash, bool, float, bool)
{
    assert(Fake::Get(id) && conditional == nullptr && duration == -1 && enter && !warp);
    ++Fake::starts;
}
}
namespace PED
{
bool IS_PED_USING_ANY_SCENARIO(Ped) { return Fake::activeId != 0; }
bool IS_PED_USING_THIS_SCENARIO(Ped, int id) { return Fake::activeId == id; }
bool IS_PED_USING_SCENARIO_HASH(Ped, Hash hash) { return Fake::Get(Fake::activeId) && Fake::Get(Fake::activeId)->hash == hash; }
bool _CAN_PED_USE_SCENARIO_POINT(Ped, int id, int a, int b, int c)
{ assert(a == 0 && b == 0 && c == 1); ++Fake::compatibleChecks; return Fake::Get(id)->compatible; }
}
namespace INTERIOR
{
Interior GET_INTERIOR_FROM_COLLISION(Vector3 position) { return position.x == Fake::approachOffset.x ? Fake::approachInterior : Fake::interior; }
Interior GET_INTERIOR_AT_COORDS(Vector3) { return Fake::atInterior; }
bool IS_INTERIOR_READY(Interior) { return Fake::interiorReady; }
bool IS_COLLISION_MARKED_OUTSIDE(Vector3) { return Fake::outside; }
}
namespace PATH
{
bool GET_SAFE_COORD_FOR_PED(Vector3 position, bool, Vector3* out, int)
{ *out = Vector3(position.x + Fake::approachOffset.x, position.y + Fake::approachOffset.y, position.z + Fake::approachOffset.z); return Fake::nav; }
}
namespace MISC { bool IS_POSITION_OCCUPIED(Vector3, float, bool, bool, bool, bool, bool, Ped, bool) { return Fake::occupied; } }
namespace WATER { bool GET_WATER_HEIGHT(Vector3 position, float* water) { *water = position.z; return Fake::water; } }
#include "../rdr2 scripting environment/samples/Pools/routine_activity_bridge.h"
using namespace RoutineActivityBridge;
static unsigned checks = 0;
static void Check(bool condition) { ++checks; assert(condition); }
static RoutineData::Location Location()
{
    return {"test", "test", RoutineData::TownId::Valentine, RoutineData::PlaceKind::Work,
        {0,0,0}, 10, 2.5f, 45, 0, 0, RoutineData::AllOccupations, true, "simulated"};
}
static Point Find(RoutineActivity::Controller& controller, Routine::Phase phase, unsigned seed = 17, std::uint64_t now = 0)
{ return RoutineActivityBridge::Find(7, Location(), phase, RoutineData::Laborer, seed, controller, now); }
int main()
{
    Fake::Reset(); RoutineActivity::Controller controller;
    Fake::scenes = {{1, Joaat("WORLD_HUMAN_BROOM"), {}}, {2, Joaat("WORLD_HUMAN_DRINKING"), {1,0,0}},
        {3, Joaat("PROP_HUMAN_SEAT_CHAIR_TABLE_EATING_KNIFE_FORK"), {2,0,0}}, {4, Joaat("WORLD_HUMAN_SLEEP_GROUND_ARM"), {3,0,0}}};
    Check(Find(controller, Routine::Phase::Work).id == 1);
    const Point lunch = Find(controller, Routine::Phase::Lunch);
    Check(lunch.kind == Kind::Drink || lunch.kind == Kind::Eat);
    Check(Find(controller, Routine::Phase::Rest).id == 4);
    Check(Find(controller, Routine::Phase::Shops).id == 0);
    Start(7, lunch); Check(Fake::starts == 1);
    Fake::activeId = lunch.id; Fake::base = false; Check(!Active(7, lunch));
    Fake::base = true; Check(Active(7, lunch));
    Fake::base = false; Fake::ongoing = true;
    Check(!Active(7, lunch)); Check(Active(7, lunch, true));
    Fake::ongoing = false; Check(!Active(7, lunch, true)); Fake::base = true;
    Fake::exiting = true; Check(!Active(7, lunch)); Check(Busy(7)); Fake::exiting = false;
    Fake::activeId = 1; Check(!Active(7, lunch));

    Fake::Reset(); controller = {};
    Fake::scenes = {{1, Joaat("WORLD_HUMAN_DRINKING"), {}, true, true, 99},
        {2, Joaat("WORLD_HUMAN_DRINKING"), {1,0,0}}};
    Check(Find(controller, Routine::Phase::Lunch).id == 2);
    Check(controller.IsCoolingDown(1, 10));
    Fake::scenes[0].user = 0; Fake::scenes[1].compatible = false;
    Check(!Find(controller, Routine::Phase::Lunch, 17, 100).id);
    Check(controller.IsCoolingDown(2, 100));
    Check(Find(controller, Routine::Phase::Lunch, 17, 120101).id == 1);

    Fake::Reset(); controller = {};
    Fake::scenes = {{1, Joaat("WORLD_HUMAN_BARCUSTOMER_BEER"), {}}, {2, Joaat("WORLD_HUMAN_BARTENDER"), {}}};
    Check(!Find(controller, Routine::Phase::Lunch).id);
    Check(Find(controller, Routine::Phase::Leisure).kind == Kind::Social);
    Fake::scenes[0].hash = Joaat("AN_INVENTED_EATING_ANIMATION"); Check(!Find(controller, Routine::Phase::Leisure).id);

    Fake::Reset(); controller = {};
    Fake::scenes = {{1, Joaat("WORLD_HUMAN_DRINKING"), {}}};
    Point drink{1, Kind::Drink, Fake::scenes[0].hash};
    Check(Valid(7, drink, false));
    Fake::loaded = false; Check(!Valid(7, drink, false)); Fake::loaded = true;
    Fake::nav = false; Check(!Valid(7, drink, false)); Fake::nav = true;
    Fake::occupied = true; Check(!Valid(7, drink, false)); Fake::occupied = false;
    Fake::water = true; Check(!Valid(7, drink, false)); Fake::water = false;
    Fake::approachOffset.z = 3; Check(!Valid(7, drink, false)); Fake::approachOffset = {};
    Fake::outside = false; Check(!Valid(7, drink, false));
    Fake::interior = Fake::atInterior = Fake::approachInterior = 12;
    Fake::interiorReady = false; Check(!Valid(7, drink, false)); Fake::interiorReady = true;
    Check(Valid(7, drink, false));
    Fake::atInterior = 13; Check(!Valid(7, drink, false)); Fake::atInterior = 12;
    Fake::scenes[0].user = 7; Fake::activeId = 1; Fake::base = true;
    Fake::scenes[0].compatible = false; Fake::nav = false; Fake::occupied = true;
    const int beforeChecks = Fake::compatibleChecks;
    Check(Valid(7, drink, true)); Check(Fake::compatibleChecks == beforeChecks);
    Fake::base = false; Fake::ongoing = true;
    Check(Valid(7, drink, true)); Check(Fake::compatibleChecks == beforeChecks);
    Fake::scenes[0].enabled = false; Check(!Valid(7, drink, true));

    Fake::Reset(); controller = {};
    Fake::scenes = {{1, Joaat("WORLD_HUMAN_DRINKING"), {1,0,0}}, {2, Joaat("WORLD_HUMAN_COFFEE_DRINK"), {2,0,0}},
        {3, Joaat("WORLD_HUMAN_SIT_DRINK"), {3,0,0}}};
    const auto stable = Find(controller, Routine::Phase::Lunch).hash;
    std::reverse(Fake::scenes.begin(), Fake::scenes.end());
    for (auto& scene : Fake::scenes) scene.id += 100;
    Check(Find(controller, Routine::Phase::Lunch).hash == stable);
    bool different = false;
    for (unsigned seed = 18; seed < 40; ++seed) different |= Find(controller, Routine::Phase::Lunch, seed).hash != stable;
    Check(different);
    Fake::scenes = {{1, Joaat("WORLD_HUMAN_DRINKING"), {0,0,8}}};
    Check(!Find(controller, Routine::Phase::Lunch).id); Check(controller.IsCoolingDown(1, 1));
    std::printf("Routine activity bridge: %u checks passed (simulated natives; no in-game navigation/animation proof).\n", checks);
}
