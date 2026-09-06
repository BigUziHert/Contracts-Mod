// run-spawn-tests.ps1 extracts the actual spawn functions and player guards.
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
struct Vector3 { float x, y, z; };

constexpr Hash kModel = 1234;
constexpr Ped kPlayer = 42;
constexpr Ped kSpawnedPed = 77;
constexpr DWORD kFrameMs = 16;
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

static struct World
{
    ULONGLONG nowMs = 1000;
    unsigned frame = 0;
    Ped playerId = kPlayer;
    bool playerAlive = true;
    bool playerDying = false;
    unsigned playerChangeFrame = std::numeric_limits<unsigned>::max();
    bool changePlayerOnCreate = false;
    bool modelValid = true;
    bool modelInImage = true;
    bool modelIsPed = true;
    bool modelHeld = false;
    unsigned modelLoadedFrame = 0;
    unsigned requests = 0;
    unsigned releases = 0;
    unsigned maintenance = 0;
    unsigned failCreateAttempts = 0;
    bool failedCreateReturnsStaleHandle = false;
    unsigned creates = 0;
    bool spawnedAlive = false;
    bool missionOwned = false;
    unsigned missionClaims = 0;
    unsigned outfits = 0;
    unsigned placements = 0;
    unsigned deletes = 0;
    bool interactionAllowed = true;
    unsigned interactionBlockedFrame = std::numeric_limits<unsigned>::max();
    bool remoteStartSucceeds = true;
    unsigned playerRefreshes = 0;
    unsigned contractStarts = 0;
    unsigned failureLogs = 0;
    unsigned failureReports = 0;
    int reportedFailure = -1;
    std::vector<const char*> messages;
    std::vector<unsigned> createFrames;
    std::vector<ULONGLONG> createTimes;
} world;

static ULONGLONG GetTickCount64() { return world.nowMs; }
static void MaintainPortraitAndCard() { ++world.maintenance; }
static void WAIT(DWORD delay)
{
    Check(delay == 0, "bounded polling yields a game frame");
    Check(!world.spawnedAlive || world.missionOwned,
        "a created ped receives mission ownership before any yield");
    ++world.frame;
    world.nowMs += kFrameMs;
    if (world.frame >= world.playerChangeFrame) world.playerId = 99;
    if (world.frame >= world.interactionBlockedFrame) world.interactionAllowed = false;
    Check(world.frame < 10000, "polling remains bounded");
}

namespace PLAYER
{
static Ped PLAYER_PED_ID() { return world.playerId; }
}

namespace STREAMING
{
static bool IS_MODEL_VALID(Hash model) { return model == kModel && world.modelValid; }
static bool IS_MODEL_IN_CDIMAGE(Hash model) { return model == kModel && world.modelInImage; }
static bool IS_MODEL_A_PED(Hash model) { return model == kModel && world.modelIsPed; }
static void REQUEST_MODEL(Hash model, bool)
{
    Check(model == kModel, "the selected model is requested");
    ++world.requests;
    world.modelHeld = true;
}
static bool HAS_MODEL_LOADED(Hash model)
{
    Check(model == kModel && world.modelHeld, "streaming is checked while holding the model");
    return world.frame >= world.modelLoadedFrame;
}
static void SET_MODEL_AS_NO_LONGER_NEEDED(Hash model)
{
    Check(model == kModel && world.modelHeld, "each acquired model is released exactly once");
    ++world.releases;
    world.modelHeld = false;
}
}

namespace ENTITY
{
static bool DOES_ENTITY_EXIST(int entity)
{
    return ((entity == kPlayer || entity == world.playerId) && world.playerAlive) ||
        (entity == kSpawnedPed && world.spawnedAlive);
}
static void SET_ENTITY_AS_MISSION_ENTITY(Ped ped, bool scriptHostObject, bool grabFromOtherScript)
{
    Check(ped == kSpawnedPed && world.spawnedAlive && world.modelHeld,
        "only a live created ped is claimed with its model held");
    Check(scriptHostObject && grabFromOtherScript, "mission ownership flags are preserved");
    Check(world.createFrames.back() == world.frame, "mission ownership is acquired in the creation frame");
    world.missionOwned = true;
    ++world.missionClaims;
}
static void PLACE_ENTITY_ON_GROUND_PROPERLY(Ped ped, int flags)
{
    Check(ped == kSpawnedPed && world.spawnedAlive && world.missionOwned && world.modelHeld && flags == 1,
        "ground placement only processes the live owned ped");
    ++world.placements;
}
}

namespace PED
{
static bool IS_PED_DEAD_OR_DYING(Ped ped, bool)
{
    return ped == kPlayer && world.playerDying;
}
static Ped CREATE_PED(Hash model, const Vector3& pos, float heading, bool network,
    bool scriptHost, bool p7, bool p8)
{
    Check(model == kModel && world.modelHeld && world.frame >= world.modelLoadedFrame,
        "creation holds a loaded model");
    Check(pos.x == 1.0f && pos.y == 2.0f && pos.z == 3.0f && heading == 0.0f,
        "retries preserve the requested spawn transform");
    Check(!network && scriptHost && p7 && p8, "single-player creation flags remain unchanged");
    Check(world.createFrames.empty() || world.createFrames.back() < world.frame,
        "failed creation attempts are separated by game frames");
    world.createFrames.push_back(world.frame);
    world.createTimes.push_back(world.nowMs);
    ++world.creates;
    if (world.creates <= world.failCreateAttempts)
        return world.failedCreateReturnsStaleHandle ? 88 : 0;
    world.spawnedAlive = true;
    if (world.changePlayerOnCreate) world.playerId = 99;
    return kSpawnedPed;
}
static void _SET_RANDOM_OUTFIT_VARIATION(Ped ped, bool)
{
    Check(ped == kSpawnedPed && world.spawnedAlive && world.missionOwned && world.modelHeld,
        "outfit initialization only processes the live owned ped");
    ++world.outfits;
}
static void DELETE_PED(Ped* ped)
{
    Check(*ped == kSpawnedPed && world.spawnedAlive, "interrupted successful creation deletes its ped");
    world.spawnedAlive = false;
    ++world.deletes;
    *ped = 0;
}
}

static struct { bool cardOpenPending = false; } C;
static void DisplaySubtitle(const char* message) { world.messages.push_back(message); }
static void UpdatePlayer();
static bool CanStartInteraction();
static bool StartContract();
static void LogContractStartFailure(Hash model, int attempt);
static void ReportContractStartFailure();

#include "spawn_under_test.h"

static void UpdatePlayer()
{
    Check(world.frame > 0, "remote start refreshes its snapshot after the fresh-frame yield");
    ++world.playerRefreshes;
    pedMe = PLAYER::PLAYER_PED_ID();
}
static bool CanStartInteraction() { return PlayerAvailable() && world.interactionAllowed; }
static bool StartContract()
{
    Check(world.frame > 0 && world.playerRefreshes == 1 && CanStartInteraction(),
        "remote creation runs after refreshing and validating the player");
    ++world.contractStarts;
    lastStartFailure = world.remoteStartSucceeds ? ContractStartFailure::None : ContractStartFailure::PedCreationFailed;
    return world.remoteStartSucceeds;
}
static void LogContractStartFailure(Hash model, int attempt)
{
    Check(model == 0 && attempt == 0, "remote preflight failure is identified before selecting a model");
    ++world.failureLogs;
}
static void ReportContractStartFailure()
{
    ++world.failureReports;
    world.reportedFailure = static_cast<int>(lastStartFailure);
}

static void Reset()
{
    world = World();
    pedMe = kPlayer;
    C = {};
    lastStartFailure = ContractStartFailure::None;
}

static Ped Spawn() { return SpawnPed(kModel, Vector3{ 1.0f, 2.0f, 3.0f }); }

static void CheckReleased()
{
    Check(world.requests == 1 && world.releases == 1 && !world.modelHeld,
        "the model is released once after the complete operation");
}

static void TestWaitPredicate()
{
    Reset();
    unsigned calls = 0;
    Check(WaitUntil(100, [&] { return ++calls == 1; }), "an immediate successful predicate completes");
    Check(calls == 1 && world.frame == 0, "a successful side-effect predicate is never repeated");
    Check(WaitUntil(100, [&] { return ++calls == 4; }), "a delayed predicate completes");
    Check(calls == 4 && world.frame == 2, "each delayed predicate is evaluated once per iteration");
}

static void TestDelayedCreation()
{
    Reset();
    world.modelLoadedFrame = 2;
    world.failCreateAttempts = 3;
    Check(Spawn() == kSpawnedPed, "creation recovers after three transient engine failures");
    Check(world.creates == 4 && world.createFrames.front() >= 2, "creation waits for the model and retries");
    for (std::size_t i = 1; i < world.createTimes.size(); ++i)
        Check(world.createTimes[i] - world.createTimes[i - 1] >= Tune::kPedSpawnRetryDelayMs,
            "creation retries respect the configured delay");
    Check(world.missionClaims == 1 && world.outfits == 1 && world.placements == 1,
        "successful creation initializes the ped once");
    Check(lastStartFailure == ContractStartFailure::None && world.maintenance > 3,
        "success clears failure state while maintaining portrait work during waits");
    CheckReleased();
}

static void TestStaleHandleAndBoundedFailure()
{
    Reset();
    world.failCreateAttempts = 1;
    world.failedCreateReturnsStaleHandle = true;
    Check(Spawn() == kSpawnedPed && world.creates == 2,
        "a nonzero nonexistent handle is retried without outfit or placement calls");
    CheckReleased();

    Reset();
    world.failCreateAttempts = std::numeric_limits<unsigned>::max();
    const ULONGLONG started = world.nowMs;
    Check(Spawn() == 0 && lastStartFailure == ContractStartFailure::PedCreationFailed,
        "exhausted retries report ped creation failure");
    Check(world.nowMs - started >= Tune::kPedSpawnRetryMs &&
        world.nowMs - started <= Tune::kPedSpawnRetryMs + kFrameMs,
        "creation retries stop within one frame of the configured deadline");
    Check(world.creates > 1 && world.outfits == 0 && world.placements == 0 && world.missionClaims == 0,
        "failed creation never initializes invalid handles");
    CheckReleased();
}

static void TestModelFailures()
{
    for (int invalid = 0; invalid < 3; ++invalid)
    {
        Reset();
        if (invalid == 0) world.modelValid = false;
        if (invalid == 1) world.modelInImage = false;
        if (invalid == 2) world.modelIsPed = false;
        Check(Spawn() == 0 && lastStartFailure == ContractStartFailure::InvalidModel,
            "invalid, missing, and non-ped models report invalid model");
        Check(world.requests == 0 && world.releases == 0 && world.creates == 0,
            "invalid models are rejected before acquiring streaming resources");
    }
    Reset();
    world.modelLoadedFrame = std::numeric_limits<unsigned>::max();
    const ULONGLONG started = world.nowMs;
    Check(Spawn() == 0 && lastStartFailure == ContractStartFailure::ModelLoadTimeout,
        "an unavailable model reports streaming timeout");
    Check(world.creates == 0 && world.nowMs - started >= Tune::kStreamTimeoutMs &&
        world.nowMs - started <= Tune::kStreamTimeoutMs + kFrameMs,
        "streaming timeout is bounded and never creates an unloaded model");
    CheckReleased();
}

static void TestPlayerInterruption()
{
    Reset();
    world.playerDying = true;
    Check(Spawn() == 0 && lastStartFailure == ContractStartFailure::Interrupted,
        "an unavailable player aborts before streaming");
    Check(world.requests == 0 && world.creates == 0, "preflight interruption acquires no resources");

    Reset();
    world.modelLoadedFrame = 10;
    world.playerChangeFrame = 1;
    Check(Spawn() == 0 && lastStartFailure == ContractStartFailure::Interrupted && world.creates == 0,
        "a player identity change during model loading aborts before creation");
    CheckReleased();

    Reset();
    world.failCreateAttempts = 3;
    world.playerChangeFrame = 1;
    Check(Spawn() == 0 && lastStartFailure == ContractStartFailure::Interrupted && world.creates == 1,
        "a player identity change during creation retries prevents further attempts");
    CheckReleased();

    Reset();
    world.changePlayerOnCreate = true;
    Check(Spawn() == 0 && lastStartFailure == ContractStartFailure::Interrupted,
        "a player identity change at successful creation aborts the operation");
    Check(world.deletes == 1 && !world.spawnedAlive && world.outfits == 0 && world.placements == 0,
        "interruption deletes the newly created ped before initialization");
    CheckReleased();
}

static void TestRemoteStart()
{
    Reset();
    StartRemoteContract();
    Check(world.frame == 1 && world.playerRefreshes == 1 && world.contractStarts == 1,
        "remote start defers exactly one frame before invoking normal contract creation");
    Check(C.cardOpenPending && world.failureLogs == 0 && world.failureReports == 0,
        "successful remote creation queues inspection without failure diagnostics");
    Check(world.messages.size() == 2 && std::strcmp(world.messages[0], "PREPARING CONTRACT") == 0 &&
        std::strcmp(world.messages[1], "FIND THE TARGET") == 0,
        "successful remote creation reports preparation followed by the hunt objective");

    Reset();
    world.playerChangeFrame = 1;
    StartRemoteContract();
    Check(pedMe == 99 && CanStartInteraction(), "the replacement player is otherwise eligible");
    Check(world.contractStarts == 0 && !C.cardOpenPending && world.failureLogs == 1 &&
        world.reportedFailure == static_cast<int>(ContractStartFailure::Interrupted),
        "a different eligible player cannot inherit the previous player's request");

    Reset();
    world.interactionBlockedFrame = 1;
    StartRemoteContract();
    Check(world.contractStarts == 0 && !C.cardOpenPending && world.failureReports == 1 &&
        world.reportedFailure == static_cast<int>(ContractStartFailure::Interrupted),
        "losing interaction eligibility during the fresh frame cancels creation");

    Reset();
    world.remoteStartSucceeds = false;
    StartRemoteContract();
    Check(world.contractStarts == 1 && !C.cardOpenPending && world.failureReports == 1 &&
        world.reportedFailure == static_cast<int>(ContractStartFailure::PedCreationFailed),
        "normal creation failure is preserved and never queues a card");
    Check(world.messages.size() == 1, "failed remote creation never displays a successful hunt objective");
}

int main()
{
    Check(Tune::kPedSpawnRetryMs > Tune::kPedSpawnRetryDelayMs && Tune::kPedSpawnRetryDelayMs > 0,
        "production retry timing allows multiple delayed attempts");
    TestWaitPredicate();
    TestDelayedCreation();
    TestStaleHandleAndBoundedFailure();
    TestModelFailures();
    TestPlayerInterruption();
    TestRemoteStart();
    std::printf("All %u spawn checks passed (actual production functions).\n", checks);
}
