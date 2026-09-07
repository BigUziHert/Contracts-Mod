// The runner extracts SpawnTargetWithPhoto and its player guards from production.
// Capture outcomes are simulated; these tests do not simulate the renderer or photo cache.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

using DWORD = std::uint32_t;
using Hash = std::uint32_t;
using Ped = int;
using ULONGLONG = unsigned long long;
struct Vector3 { float x, y, z; };
struct ContractDef { Vector3 spawn; };

constexpr Hash kModel = 1234;
constexpr Ped kPlayer = 42;
constexpr Ped kTarget = 77;
static Ped pedMe = kPlayer;
static unsigned checks = 0;

static void Check(bool condition, const char* description)
{
    ++checks;
    if (!condition)
    {
        std::fprintf(stderr, "FAILED: %s\n", description);
        std::exit(EXIT_FAILURE);
    }
}

enum class CaptureResult { Failure, Success, Interrupt, RemoveSubject, SuccessRemoveSubject, SuccessKillSubject, SuccessInterrupt };
struct FailureLog { Hash model; int attempt; int reason; };
static struct World
{
    Ped playerId = kPlayer;
    bool playerAlive = true;
    bool playerDying = false;
    bool spawnFails = false;
    bool interruptAfterSpawn = false;
    bool interruptOnYield = false;
    bool targetExists = false;
    bool targetDying = false;
    bool frozen = false;
    bool collision = true;
    bool visible = true;
    bool acceptedPhoto = false;
    bool destinationValid = true, placementValid = true, groundPlacement = true;
    bool interruptPlacement = false, losePlacementTarget = false;
    bool delayCleanup = false, cleanupRequested = false, identityMatches = true, loseOwnership = false;
    unsigned now = 1000, placementReadyAt = 0;
    unsigned frame = 0;
    unsigned spawns = 0;
    unsigned releases = 0;
    unsigned deletes = 0;
    unsigned teleports = 0;
    unsigned placements = 0;
    unsigned reveals = 0;
    std::vector<CaptureResult> results;
    std::vector<Ped> subjects;
    std::vector<unsigned> captureFrames;
    std::vector<FailureLog> failures;
} world;
static struct { Ped ped = kTarget; } ownedPed;
static bool OwnedPedIdentityMatches() { return world.identityMatches; }

static void WAIT(DWORD delay)
{
    Check(delay == 0, "portrait retries yield a game frame");
    Check(world.targetExists && world.frozen && !world.collision && !world.visible,
        "failed portrait stays parked and hidden between attempts");
    ++world.frame;
    if (world.interruptOnYield) world.playerAlive = false;
    Check(world.frame < 10, "portrait retries are bounded");
}

namespace PLAYER
{
static Ped PLAYER_PED_ID() { return world.playerId; }
}

namespace ENTITY
{
static bool DOES_ENTITY_EXIST(Ped ped)
{
    return (ped == kPlayer && world.playerAlive) || (ped == kTarget && world.targetExists);
}
static Vector3 GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(Ped ped, float, float, float)
{
    Check(ped == kPlayer, "the provisional target is staged near the current player");
    return Vector3{ 10.0f, 20.0f, 30.0f };
}
static void FREEZE_ENTITY_POSITION(Ped ped, bool frozen)
{
    Check(ped == kTarget && world.targetExists, "freezing only touches the provisional target");
    Check(world.identityMatches, "freezing never touches an actor whose ownership changed");
    if (!frozen) Check(world.acceptedPhoto, "failed capture cannot release the target into gameplay");
    world.frozen = frozen;
}
static void SET_ENTITY_COLLISION(Ped ped, bool collision, bool)
{
    Check(ped == kTarget && world.targetExists, "collision changes only touch the provisional target");
    Check(world.identityMatches, "collision changes never touch an actor whose ownership changed");
    if (collision) Check(world.acceptedPhoto, "failed capture cannot restore target collision");
    world.collision = collision;
}
static float GET_ENTITY_HEADING(Ped ped)
{
    Check(ped == kPlayer, "portrait heading uses the current player");
    return 90.0f;
}
static void SET_ENTITY_HEADING(Ped ped, float)
{
    Check(ped == kTarget && world.targetExists, "portrait heading only changes the provisional target");
}
static void SET_ENTITY_VISIBLE(Ped ped, bool visible)
{
    Check(ped == kTarget && world.targetExists, "visibility only changes the provisional target");
    if (visible)
    {
        Check(world.acceptedPhoto, "failed capture cannot expose the target");
        ++world.reveals;
    }
    world.visible = visible;
}
static void SET_ENTITY_COORDS(Ped ped, float x, float y, float z, bool, bool, bool, bool clearArea)
{
    Check(ped == kTarget && world.targetExists && world.acceptedPhoto,
        "only a photographed target is moved into its contract");
    Check(x == 100.0f && y == 200.0f && z == 300.0f, "target reaches the selected contract location");
    Check(!clearArea && !world.visible, "initial move stays hidden and never clears ambient actors");
    ++world.teleports;
}
static bool PLACE_ENTITY_ON_GROUND_PROPERLY(Ped ped, int)
{
    Check(ped == kTarget && world.targetExists && world.teleports == 1,
        "ground placement follows the successful target teleport");
    Check(world.identityMatches, "ground placement never touches an actor whose ownership changed");
    Check(!world.visible && world.collision && !world.frozen, "settling has physics and collision while the target stays hidden");
    ++world.placements;
    return world.groundPlacement;
}
}

namespace PED
{
static bool IS_PED_DEAD_OR_DYING(Ped ped, bool)
{
    return (ped == kPlayer && world.playerDying) || (ped == kTarget && world.targetDying);
}
static void SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(Ped ped, bool)
{
    Check(ped == kTarget && world.targetExists, "event setup only touches the provisional target");
}
static void DELETE_PED(Ped* ped)
{
    Check(*ped == kTarget && world.targetExists, "failure deletes its own provisional target exactly once");
    world.targetExists = false;
    ++world.deletes;
    *ped = 0;
}
}

namespace TASK
{
static void CLEAR_PED_TASKS_IMMEDIATELY(Ped ped, bool, bool)
{
    Check(ped == kTarget && world.targetExists, "task setup only touches the provisional target");
}
}

static Ped SpawnPed(Hash model, const Vector3& pos)
{
    Check(model == kModel && pos.x == 10.0f && pos.y == 20.0f && pos.z == 30.0f,
        "the selected target model is spawned at its portrait staging location");
    ++world.spawns;
    if (world.spawnFails) return 0;
    world.targetExists = true;
    if (world.interruptAfterSpawn) world.playerId = 99;
    return kTarget;
}

static bool PhotographPed(Ped subject)
{
    Check(subject == kTarget && world.targetExists && world.frozen && !world.collision,
        "each portrait attempt uses the same parked provisional target");
    Check(world.subjects.size() < world.results.size(), "capture never exceeds its planned attempt count");
    const CaptureResult result = world.results[world.subjects.size()];
    world.subjects.push_back(subject);
    world.captureFrames.push_back(world.frame);
    world.visible = false; // PhotographPed hides its subject before entering the capture pipeline.
    world.acceptedPhoto = result == CaptureResult::Success || result == CaptureResult::SuccessRemoveSubject ||
        result == CaptureResult::SuccessKillSubject || result == CaptureResult::SuccessInterrupt;
    if (result == CaptureResult::Interrupt || result == CaptureResult::SuccessInterrupt) world.playerId = 99;
    if (result == CaptureResult::RemoveSubject || result == CaptureResult::SuccessRemoveSubject) world.targetExists = false;
    if (result == CaptureResult::SuccessKillSubject) world.targetDying = true;
    return world.acceptedPhoto;
}

static void ReleaseTargetPhoto()
{
    Check(!world.targetExists || world.cleanupRequested, "failed preparation requests target cleanup before releasing the portrait");
    world.acceptedPhoto = false;
    ++world.releases;
}

// Cleanup's delayed/failed native behavior is exercised by owned_ped_cleanup_tests.
static void RequestOwnedPedCleanup(Ped ped)
{
    world.cleanupRequested = true;
    if (ENTITY::DOES_ENTITY_EXIST(ped) && world.identityMatches && !world.delayCleanup) PED::DELETE_PED(&ped);
}

static void LogContractStartFailure(Hash model, int attempt);
static bool ValidateRoutineDeployment(Ped, const ContractDef&) { return world.destinationValid; }
static bool ValidateRoutinePlacement(Ped, const ContractDef&) { return world.placementValid && world.now >= world.placementReadyAt; }
static bool CanStartInteraction() { return world.playerAlive && world.playerId == pedMe; }
static ULONGLONG GetTickCount64() { return world.now; }
namespace RoutineSpawn { static bool Reject(const char*) { return false; } }
template<typename Predicate> static bool WaitUntil(DWORD timeout, Predicate predicate)
{
    const unsigned start = world.now;
    while (world.playerAlive)
    {
        if (predicate()) return true;
        if (world.now - start >= timeout) return false;
        world.now += 16;
        if (world.interruptPlacement) world.playerAlive = false;
        if (world.losePlacementTarget) world.targetExists = false;
        if (world.loseOwnership) world.identityMatches = false;
    }
    return false;
}
#include "portrait_start_under_test.h"

static void LogContractStartFailure(Hash model, int attempt)
{
    world.failures.push_back(FailureLog{ model, attempt, static_cast<int>(lastStartFailure) });
}

static void Reset(std::vector<CaptureResult> results)
{
    world = World();
    routineStartDiagnostic = {};
    world.results = results;
    pedMe = kPlayer;
    lastStartFailure = ContractStartFailure::None;
}

static Ped Prepare()
{
    return SpawnTargetWithPhoto(kModel, ContractDef{ Vector3{ 100.0f, 200.0f, 300.0f } });
}

static void CheckReady()
{
    Check(world.spawns == 1 && world.targetExists && world.acceptedPhoto,
        "successful preparation retains one photographed target");
    Check(world.visible && world.collision && !world.frozen,
        "successful preparation restores visibility, collision and movement");
    Check(world.teleports == 1 && world.placements == 1 && world.reveals == 1,
        "successful preparation deploys its target exactly once");
    Check(world.deletes == 0 && world.releases == 0, "successful preparation retains its ped and portrait");
}

static void CheckFailed(unsigned expectedDeletes)
{
    Check(!world.targetExists && !world.acceptedPhoto && world.releases == 1,
        "failed preparation leaves no provisional ped or accepted portrait");
    Check(world.deletes == expectedDeletes, "failure deletes only a still-existing provisional ped");
    Check(world.teleports == 0 && world.placements == 0 && world.reveals == 0,
        "failed preparation never reveals or deploys its target");
}

int main()
{
    Check(Card::kPhotoAttempts == 2, "production allows exactly one portrait retry");

    Reset({ CaptureResult::Success });
    Check(Prepare() == kTarget, "first-attempt success returns the original target");
    Check(world.subjects.size() == 1 && world.frame == 0 && world.failures.empty(),
        "first-attempt success needs no retry or failure report");
    CheckReady();

    Reset({ CaptureResult::Success });
    world.destinationValid = false;
    Check(Prepare() == 0 && lastStartFailure == ContractStartFailure::LocationUnavailable,
        "destination disappearing during portrait refuses deployment");
    CheckFailed(1);
    Reset({ CaptureResult::Success });
    world.placementValid = false;
    Check(Prepare() == 0 && world.teleports == 1 && world.reveals == 0 && world.deletes == 1 && world.releases == 1,
        "failed placement cleans up while hidden without exposing a broken target");
    Check(world.now < 2516 && world.placements <= 15, "settling timeout and native retries stay bounded");

    Reset({ CaptureResult::Success });
    world.placementValid = false; world.delayCleanup = true;
    Check(Prepare() == 0 && world.targetExists && world.cleanupRequested && world.deletes == 0 && world.releases == 1,
        "deferred placement-failure deletion remains tracked while the portrait is released");
    Check(world.frozen && !world.collision && !world.visible,
        "failed placement makes the owned hidden target inert before deferred cleanup");

    Reset({ CaptureResult::Success });
    world.placementValid = false; world.loseOwnership = true;
    Check(Prepare() == 0 && world.targetExists && world.cleanupRequested && world.deletes == 0 && world.releases == 1,
        "changed ownership prevents deletion or physics changes during placement-failure cleanup");
    Check(world.placements == 1, "changed ownership also stops further placement attempts");

    Reset({ CaptureResult::Success });
    world.groundPlacement = false;
    Check(Prepare() == kTarget && !routineStartDiagnostic.placementResult,
        "a false placement return alone cannot reject verified actual geometry");
    CheckReady();

    Reset({ CaptureResult::Success });
    world.placementReadyAt = 1120;
    Check(Prepare() == kTarget && world.now >= 1120 && world.teleports == 1 && world.placements == 3 && world.reveals == 1,
        "delayed physics settles across bounded native retries without another teleport or early reveal");

    Reset({ CaptureResult::Success });
    world.placementReadyAt = 1120; world.interruptPlacement = true;
    Check(Prepare() == 0 && lastStartFailure == ContractStartFailure::Interrupted && world.reveals == 0 && world.deletes == 1 && world.releases == 1,
        "player loss during settling cleans up rather than exposing the provisional target");

    Reset({ CaptureResult::Success });
    world.placementReadyAt = 1120; world.losePlacementTarget = true;
    Check(Prepare() == 0 && world.reveals == 0 && world.deletes == 0 && world.releases == 1,
        "subject loss during settling never sends another ground task or reveals a vanished ped");

    Reset({ CaptureResult::Failure, CaptureResult::Success });
    Check(Prepare() == kTarget, "retry success returns the original target");
    Check(world.subjects.size() == 2 && world.subjects[0] == world.subjects[1] && world.spawns == 1,
        "retry photographs the same target without respawning");
    Check(world.frame == 1 && world.captureFrames[1] > world.captureFrames[0],
        "failed capture yields before the next attempt");
    Check(world.failures.size() == 1 && world.failures[0].model == kModel && world.failures[0].attempt == 1,
        "retry records the failed first attempt");
    CheckReady();

    Reset({ CaptureResult::Failure, CaptureResult::Failure });
    Check(Prepare() == 0, "two failed portraits refuse the contract");
    Check(world.subjects.size() == 2 && world.frame == 1 && world.failures.size() == 2,
        "permanent failure uses only the bounded retry");
    Check(lastStartFailure == ContractStartFailure::PortraitFailed,
        "permanent portrait failure preserves its specific reason");
    CheckFailed(1);

    Reset({ CaptureResult::Interrupt });
    Check(Prepare() == 0, "cancellation during capture refuses the contract");
    Check(world.subjects.size() == 1 && world.frame == 0 && world.failures.size() == 1,
        "cancellation during capture does not retry");
    Check(lastStartFailure == ContractStartFailure::Interrupted &&
        world.failures[0].reason == static_cast<int>(ContractStartFailure::Interrupted),
        "cancellation records interruption rather than portrait failure");
    CheckFailed(1);

    Reset({});
    world.interruptAfterSpawn = true;
    Check(Prepare() == 0 && world.subjects.empty() && world.frame == 0,
        "player replacement before capture never enters the photo pipeline");
    CheckFailed(1);

    Reset({ CaptureResult::Failure });
    world.interruptOnYield = true;
    Check(Prepare() == 0 && world.subjects.size() == 1 && world.frame == 1,
        "cancellation between attempts prevents a second capture");
    Check(lastStartFailure == ContractStartFailure::Interrupted,
        "cancellation between attempts preserves its reason");
    CheckFailed(1);

    Reset({ CaptureResult::RemoveSubject });
    Check(Prepare() == 0 && world.subjects.size() == 1 && world.frame == 0,
        "a vanished target is not retried");
    CheckFailed(0);

    Reset({ CaptureResult::SuccessRemoveSubject });
    Check(Prepare() == 0 && world.subjects.size() == 1 && world.frame == 0,
        "a successful texture cannot deploy a subject removed during capture");
    CheckFailed(0);

    Reset({ CaptureResult::SuccessKillSubject });
    Check(Prepare() == 0 && world.subjects.size() == 1 && world.frame == 0,
        "a subject killed during successful capture is cleaned up without retry or deployment");
    CheckFailed(1);

    Reset({ CaptureResult::SuccessInterrupt });
    Check(Prepare() == 0 && lastStartFailure == ContractStartFailure::Interrupted,
        "player replacement during successful capture aborts before exposing the subject");
    CheckFailed(1);

    Reset({});
    world.spawnFails = true;
    Check(Prepare() == 0 && world.spawns == 1 && world.subjects.empty(),
        "spawn failure never enters the photo pipeline");
    Check(world.deletes == 0 && world.releases == 0 && world.frame == 0 && world.failures.empty(),
        "spawn failure leaves no portrait resources to clean up");

    std::printf("Portrait preparation tests passed (%u checks).\n", checks);
    return EXIT_SUCCESS;
}
