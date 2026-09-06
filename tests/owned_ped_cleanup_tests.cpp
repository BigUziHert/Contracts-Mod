// The runner extracts the production ownership record and cleanup functions.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <vector>

using DWORD = std::uint32_t;
using ULONGLONG = std::uint64_t;
using Hash = std::uint32_t;
using Ped = int;
constexpr Ped kPed = 77, kForeignPed = 88;
constexpr Hash kModel = 1234;
static unsigned checks = 0;
static void Check(bool condition, const char* message)
{
    ++checks;
    if (!condition) { std::fprintf(stderr, "FAILED: %s\n", message); std::exit(EXIT_FAILURE); }
}

struct World
{
    ULONGLONG nowMs = 1000;
    ULONGLONG removalMs = std::numeric_limits<ULONGLONG>::max();
    bool alive = true;
    bool foreignAlive = true;
    bool ours = true;
    bool persistence = false;
    bool dead = false;
    Hash model = kModel;
    bool deletionSucceeds = true;
    DWORD deletionDelayMs = 0;
    unsigned deletes = 0;
    unsigned releases = 0;
    unsigned collisionChanges = 0;
    std::vector<const char*> events;
};
static World world;
static ULONGLONG GetTickCount64() { return world.nowMs; }
static void Advance(DWORD milliseconds)
{
    world.nowMs += milliseconds;
    if (world.nowMs >= world.removalMs) world.alive = false;
}
namespace ENTITY
{
static bool DOES_ENTITY_EXIST(Ped ped)
{
    return (ped == kPed && world.alive) || (ped == kForeignPed && world.foreignAlive);
}
static Hash GET_ENTITY_MODEL(Ped ped)
{
    Check(ped == kPed && world.alive, "identity checks address the original live handle");
    return world.model;
}
static bool DOES_ENTITY_BELONG_TO_THIS_SCRIPT(Ped ped, bool grab)
{
    Check(ped == kPed && world.alive && !grab, "cleanup checks ownership without grabbing another script's entity");
    return world.ours;
}
static bool _IS_ENTITY_OWNED_BY_PERSISTENCE_SYSTEM(Ped ped)
{
    Check(ped == kPed && world.alive, "persistence checks address the original handle");
    return world.persistence;
}
static void SET_ENTITY_LOAD_COLLISION_FLAG(Ped ped, bool enabled)
{
    Check(ped == kPed && world.alive && world.ours && !world.persistence && !enabled,
        "collision cleanup only affects the verified owned ped");
    ++world.collisionChanges;
}
static void SET_ENTITY_AS_NO_LONGER_NEEDED(Ped* ped)
{
    Check(*ped == kPed && world.alive && world.ours && !world.persistence,
        "release only relinquishes the verified owned ped");
    ++world.releases;
    *ped = 0;
}
}
namespace PED
{
static bool IS_PED_DEAD_OR_DYING(Ped ped, bool)
{
    Check(ped == kPed && world.alive, "death checks address the tracked ped");
    return world.dead;
}
static void DELETE_PED(Ped* ped)
{
    Check(*ped == kPed && world.alive && world.ours && !world.persistence && world.model == kModel,
        "every delete request checks ownership and model identity");
    ++world.deletes;
    if (world.deletionSucceeds)
    {
        if (world.deletionDelayMs) world.removalMs = world.nowMs + world.deletionDelayMs;
        else world.alive = false;
    }
    *ped = 0; // This output alone must never be treated as confirmed removal.
}
}
static void LogOwnedPedCleanup(const char* event) { world.events.push_back(event); }
#include "owned_ped_cleanup_under_test.h"

static unsigned EventCount(const char* event)
{
    unsigned count = 0;
    for (const char* recorded : world.events) if (std::strcmp(recorded, event) == 0) ++count;
    return count;
}
static void Reset()
{
    world = World();
    ownedPed = OwnedPedRuntime();
    ownedPedsCreated = ownedPedsDeleted = ownedPedsReleased = 0;
    TrackOwnedPed(kPed, kModel);
}

static void TestDelayedAndImmediateRemoval()
{
    Reset();
    world.deletionDelayMs = 16;
    RequestOwnedPedCleanup(kPed);
    Check(ownedPed.ped == kPed && ownedPed.cleanupPending && world.alive && ownedPedsDeleted == 0,
        "a zeroed native argument cannot erase an existing pending target");
    RequestOwnedPedCleanup(kPed);
    MaintainOwnedPedCleanup();
    Check(world.deletes == 1 && EventCount("requested") == 1,
        "duplicate cleanup requests neither repeat the event nor bypass retry cadence");
    Advance(16);
    MaintainOwnedPedCleanup();
    Check(!ownedPed.ped && ownedPedsDeleted == 1 && EventCount("confirmed") == 1,
        "a later disappearance confirms removal and frees the record");
    MaintainOwnedPedCleanup();
    Check(ownedPedsDeleted == 1 && EventCount("confirmed") == 1 && world.foreignAlive,
        "confirmation is counted once and unrelated peds remain alive");

    Reset();
    RequestOwnedPedCleanup(kPed);
    Check(!world.alive && ownedPed.ped == kPed,
        "even immediate deletion preserves the original until it is checked");
    MaintainOwnedPedCleanup();
    Check(!ownedPed.ped && ownedPedsCreated == 1 && ownedPedsDeleted == 1,
        "immediate removal is confirmed by original-handle existence");
}

static void TestPermanentFailureAndRecovery()
{
    Reset();
    world.deletionSucceeds = false;
    RequestOwnedPedCleanup(kPed);
    for (unsigned i = 0; i < 20; ++i)
    {
        Advance(kOwnedPedDeleteRetryMs);
        MaintainOwnedPedCleanup();
    }
    Check(ownedPed.ped == kPed && ownedPed.cleanupPending && world.alive && ownedPedsDeleted == 0,
        "permanent failure retains the owned handle without false confirmation");
    Check(EventCount("requested") == 1 && EventCount("still_exists") == 1 && world.deletes == 21,
        "persistent cleanup retries at a bounded cadence and logs blockage once");
    TrackOwnedPed(kForeignPed, 9999);
    RequestOwnedPedCleanup(kForeignPed);
    Check(ownedPed.ped == kPed && ownedPedsCreated == 1 && world.foreignAlive,
        "a pending cleanup cannot be overwritten by or act on another ped");
    world.deletionSucceeds = true;
    Advance(kOwnedPedDeleteRetryMs);
    MaintainOwnedPedCleanup();
    MaintainOwnedPedCleanup();
    Check(!ownedPed.ped && EventCount("confirmed") == 1,
        "cleanup can recover after its initial bounded waiting period");
}

static void TestIdentityAndOwnershipProtection()
{
    for (int mismatch = 0; mismatch < 3; ++mismatch)
    {
        Reset();
        if (mismatch == 0) world.model = 9999;
        if (mismatch == 1) world.ours = false;
        if (mismatch == 2) world.persistence = true;
        RequestOwnedPedCleanup(kPed);
        Advance(kOwnedPedDeleteRetryMs);
        MaintainOwnedPedCleanup();
        Check(world.deletes == 0 && world.collisionChanges == 0 && world.releases == 0 && world.alive,
            "model, script ownership or persistence changes prevent all destructive actions");
        Check(ownedPed.ped == kPed && EventCount("ownership_blocked") == 1 && world.foreignAlive,
            "a protected entity stays tracked and emits only one blocked event");
        world.alive = false;
        MaintainOwnedPedCleanup();
        Check(!ownedPed.ped && EventCount("confirmed") == 1,
            "external removal resolves a blocked record without grabbing ownership");
    }
}

static void TestReleaseAndAlreadyGone()
{
    Reset();
    ReleaseOwnedPed(kForeignPed);
    Check(ownedPed.ped == kPed && world.releases == 0, "releasing a foreign handle cannot discard our record");
    ReleaseOwnedPed(kPed);
    Check(!ownedPed.ped && world.alive && world.releases == 1 && ownedPedsReleased == 1 && world.deletes == 0,
        "normal corpse release relinquishes ownership without forced deletion");
    Check(EventCount("released") == 1, "normal release is included in lifecycle accounting");
    Reset();
    world.alive = false;
    RequestOwnedPedCleanup(kPed);
    Check(!ownedPed.ped && world.deletes == 0 && ownedPedsDeleted == 1,
        "an already removed owned ped needs no delete native");
    Reset();
    world.dead = true;
    RequestOwnedPedCleanup(kPed);
    MaintainOwnedPedCleanup();
    Check(world.deletes == 1 && world.collisionChanges == 0 && !ownedPed.ped,
        "dead-ped cleanup does not alter live-ped collision loading");
}

int main()
{
    TestDelayedAndImmediateRemoval();
    TestPermanentFailureAndRecovery();
    TestIdentityAndOwnershipProtection();
    TestReleaseAndAlreadyGone();
    std::printf("Owned ped cleanup tests passed (%u checks, actual production functions).\n", checks);
}
