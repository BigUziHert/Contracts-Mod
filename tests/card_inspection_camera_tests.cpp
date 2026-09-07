#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using Ped = int;
using Entity = int;
using Object = int;
using Hash = unsigned;
using DWORD = std::uint32_t;
using ULONGLONG = unsigned long long;
constexpr Ped kPlayer = 42;
constexpr Object kCard = 100;
static Ped pedMe = kPlayer;
static unsigned checks = 0;
static void Check(bool condition, const char* message)
{
    ++checks;
    if (!condition) { std::fprintf(stderr, "FAILED: %s\n", message); std::exit(EXIT_FAILURE); }
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
static struct World
{
    Ped currentPlayer = kPlayer;
    Object card = kCard, primaryItem = 0;
    bool playerExists = true, playerDead = false, cardExists = true, itemRunning = false;
    bool paused = false, faded = false;
    ULONGLONG now = 1000;
    unsigned collisionCalls = 0, cameraCalls = 0, taskReads = 0, slotReads = 0, waits = 0;
    unsigned startTaskOnFrame = 0;
    std::vector<std::string> events;
} w;
namespace ENTITY
{
static bool DOES_ENTITY_EXIST(Entity entity)
{
    return (entity == kPlayer && w.playerExists) || (entity == w.card && w.cardExists);
}
static void SET_ENTITY_COLLISION(Entity entity, bool enabled, bool keepPhysics)
{
    Check(entity == w.card && w.cardExists && !enabled && !keepPhysics,
        "collision prevention only disables collision on the admitted existing card");
    ++w.collisionCalls; w.events.push_back("collision");
}
}
namespace PED
{
static bool IS_PED_DEAD_OR_DYING(Ped ped, bool includeDying)
{
    Check(ped == kPlayer && w.playerExists && includeDying, "player readiness checks its living inspector");
    return w.playerDead;
}
}
namespace PLAYER { static Ped PLAYER_PED_ID() { return w.currentPlayer; } }
namespace TASK
{
static bool IS_PED_RUNNING_TASK_ITEM_INTERACTION(Ped ped)
{
    Check(ped == kPlayer && w.playerExists && !w.playerDead, "task ownership reads only the living current inspector");
    ++w.taskReads; return w.itemRunning;
}
static Entity _GET_ITEM_INTERACTION_ENTITY_FROM_PED(Ped ped, Hash slot)
{
    Check(ped == kPlayer && slot == Joaat("primaryItem") && w.itemRunning,
        "ownership checks the native primary item of the running task");
    ++w.slotReads; return w.primaryItem;
}
}
namespace CAMERA
{
static bool IS_SCREEN_FADED_OUT() { return w.faded; }
static void SET_GAMEPLAY_CAM_IGNORE_ENTITY_COLLISION_THIS_UPDATE(Entity entity)
{
    Check(entity == w.card && w.cardExists, "camera collision exclusion addresses only the admitted card");
    ++w.cameraCalls; w.events.push_back("camera");
}
}
namespace HUD { static bool IS_PAUSE_MENU_ACTIVE() { return w.paused; } }
static ULONGLONG GetTickCount64() { return w.now; }
static void MaintainOwnedPedCleanup() { w.events.push_back("cleanup"); }
static void SetRuntimePaused(bool) {}
static void MaintainPortraitAndCard() { w.events.push_back("portrait"); }
static void UpdateRoutineDebug() { w.events.push_back("debug"); }
static void UpdateCard() { w.events.push_back("card_update"); }
static void WAIT(int delay)
{
    Check(delay == 0, "inspection maintenance keeps the normal per-frame yield");
    ++w.waits; w.now += 16; w.events.push_back("wait");
    if (w.startTaskOnFrame && w.waits >= w.startTaskOnFrame)
    {
        w.itemRunning = true; w.primaryItem = w.card;
    }
}
#include "card_inspection_camera_under_test.h"

static void Reset()
{
    w = {}; Cd = {}; pedMe = kPlayer;
    Cd.obj = w.card; Cd.ownsObj = true; Cd.inspectingPed = kPlayer;
}
static void PendingOwnedAndMatchingTasks()
{
    Reset();
    MaintainCardInspectionCamera();
    Check(w.collisionCalls == 1 && w.cameraCalls == 1 && w.slotReads == 0,
        "fresh remote or I card is protected while its task is still starting");
    Check(w.events == std::vector<std::string>{"collision", "camera"},
        "pending card disables its physical collision before camera exclusion");
    for (bool owned : {true, false})
    {
        Reset(); Cd.ownsObj = owned; Cd.examining = true;
        w.itemRunning = true; w.primaryItem = w.card;
        Check(OwnCardTaskRunning(), "production ownership recognizes the matching held prop");
        for (int frame = 0; frame < 3; ++frame) MaintainCardInspectionCamera();
        Check(w.collisionCalls == 3 && w.cameraCalls == 3,
            "owned and native fallback cards receive camera exclusion on every admitted frame");
    }
    Reset(); Cd.inHand = true;
    MaintainCardInspectionCamera();
    Check(w.cameraCalls == 1, "reused handoff card is protected when it becomes the pending inspection");
    Cd = {}; // Normal put-away resets card tracking before a fresh I/replacement open.
    MaintainCardInspectionCamera();
    Check(w.cameraCalls == 1, "put-away retirement stops touching the previous prop");
    w.card = kCard + 1; Cd.obj = w.card; Cd.ownsObj = true; Cd.inspectingPed = kPlayer;
    MaintainCardInspectionCamera();
    Check(w.cameraCalls == 2, "fresh I or replacement card is protected without caching the previous entity handle");
}
static void RefusedOwnershipAndReadiness()
{
    for (int invalid = 0; invalid < 11; ++invalid)
    {
        Reset();
        switch (invalid)
        {
        case 0: Cd.obj = 0; break;
        case 1: w.cardExists = false; break;
        case 2: Cd.inspectingPed = 0; Cd.inHand = true; break;
        case 3: Cd.inspectingPed = kPlayer + 1; break;
        case 4: w.playerExists = false; break;
        case 5: w.playerDead = true; break;
        case 6: w.currentPlayer = kPlayer + 1; break;
        case 7: Cd.ownsObj = false; break;
        case 8: Cd.examining = true; break; // An ended inspection is not a pending owned prop.
        case 9: w.itemRunning = true; w.primaryItem = kCard + 1; break;
        case 10: Cd.ownsObj = false; w.itemRunning = true; w.primaryItem = kCard + 1; break;
        }
        MaintainCardInspectionCamera();
        Check(w.collisionCalls == 0 && w.cameraCalls == 0 && w.events.empty(),
            "missing, retired, handoff-only, foreign-task or invalid-player state cannot change collision or camera");
    }
    Reset(); w.itemRunning = true; w.primaryItem = kCard + 1;
    Check(!OwnCardTaskRunning(), "real ownership rejects a running foreign item task");
    w.primaryItem = kCard;
    Check(OwnCardTaskRunning(), "exact primary-item identity restores admission");
}
static void ProductionWaitAndFrameIntegration()
{
    Reset(); w.startTaskOnFrame = 2;
    Check(WaitUntil(200, [] { return OwnCardTaskRunning(); }), "real startup wait accepts the delayed matching task");
    Check(w.waits == 2 && w.cameraCalls == 3 && w.collisionCalls == 3,
        "camera protection covers every pending startup yield and the matching-task completion iteration");
    Check(w.events == std::vector<std::string>{
        "cleanup", "collision", "camera", "portrait", "wait",
        "cleanup", "collision", "camera", "portrait", "wait",
        "cleanup", "collision", "camera", "portrait"},
        "real WaitUntil keeps card collision protection before portrait maintenance and predicate evaluation");
    Reset();
    RunProductionCardFrameTail();
    Check(w.events == std::vector<std::string>{"collision", "camera", "debug", "card_update", "wait"},
        "actual frame integration preserves final debug, card update and yield order");
}
int main()
{
    PendingOwnedAndMatchingTasks(); RefusedOwnershipAndReadiness(); ProductionWaitAndFrameIntegration();
    std::printf("Card inspection camera: %u checks passed.\n", checks);
}
