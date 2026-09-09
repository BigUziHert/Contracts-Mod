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
#include "../rdr2 scripting environment/samples/Pools/startup_trace.h"
#include "spawn_hash_under_test.h"
constexpr Hash kModel = 1234;
constexpr Ped kPlayer = 42;
constexpr Ped kSpawnedPed = 77;
constexpr Ped kGiver = 66;
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
    unsigned createVisibilityDelayFrames = 0;
    unsigned spawnVisibleFrame = 0;
    bool spawnedPending = false;
    int freePedSlots = 10;
    unsigned poolQueries = 0;
    unsigned poolAvailableFrame = 0;
    bool creationFillsPool = false;
    unsigned creates = 0;
    bool spawnedAlive = false;
    bool missionOwned = false;
    unsigned missionClaims = 0;
    unsigned outfits = 0;
    unsigned randomOutfits = 0, presetOutfits = 0;
    int lastPreset = -1;
    unsigned placements = 0;
    unsigned deletes = 0;
    bool deleteSucceeds = true;
    unsigned deleteDelayFrames = 0;
    unsigned deletionFrame = std::numeric_limits<unsigned>::max();
    bool interactionAllowed = true;
    bool paused = false;
    bool faded = false;
    bool ownCardTask = false;
    Hash itemState = 0;
    unsigned pauseFrame = std::numeric_limits<unsigned>::max();
    unsigned resumeFrame = std::numeric_limits<unsigned>::max();
    unsigned interactionBlockedFrame = std::numeric_limits<unsigned>::max();
    bool remoteStartSucceeds = true;
    unsigned locationFailures = 0, handoffCalls = 0;
    int forcedStartFailure = -1;
    bool cleanupAfterLocationFailure = false, losePlayerDuringStart = false;
    bool handoffSucceeds = true, giverAlive = true, giverSpotMatches = true;
    Ped handoffGiver = 0;
    ULONGLONG attemptDurationMs = 0;
    std::vector<ULONGLONG> startTimes;
    bool contractActive = false;
    unsigned playerRefreshes = 0;
    unsigned contractStarts = 0;
    unsigned failureLogs = 0;
    unsigned failureReports = 0;
    int reportedFailure = -1;
    std::vector<const char*> messages;
    std::vector<unsigned> createFrames;
    std::vector<ULONGLONG> createTimes;
    std::vector<StartupTrace::Event> startupEvents;
} world;

static void CollectStartupTrace(const StartupTrace::Event& event)
{
    world.startupEvents.push_back(event);
}

static ULONGLONG GetTickCount64() { return world.nowMs; }
static void TraceCardInspection() {} // Its production bridge has a dedicated native-shim suite.
static void MaintainPortraitAndCard() { ++world.maintenance; }
static void WAIT(DWORD delay)
{
    Check(delay == 0, "bounded polling yields a game frame");
    Check(!world.spawnedAlive || world.missionOwned,
        "a created ped receives mission ownership before any yield");
    ++world.frame;
    world.nowMs += kFrameMs;
    if (world.frame >= world.deletionFrame) world.spawnedAlive = false;
    if (world.frame >= world.playerChangeFrame) world.playerId = 99;
    if (world.frame >= world.interactionBlockedFrame) world.interactionAllowed = false;
    if (world.frame >= world.pauseFrame) world.paused = world.frame < world.resumeFrame;
    Check(world.frame < 10000, "polling remains bounded");
}

namespace PLAYER
{
static Ped PLAYER_PED_ID() { return world.playerId; }
}

namespace HUD { static bool IS_PAUSE_MENU_ACTIVE() { return world.paused; } }
namespace CAMERA { static bool IS_SCREEN_FADED_OUT() { return world.faded; } }
namespace TASK { static Hash GET_ITEM_INTERACTION_STATE(Ped) { return world.itemState; } }

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
static Hash GET_ENTITY_MODEL(Ped ped) { return ped == kSpawnedPed ? kModel : 0; }
static bool DOES_ENTITY_BELONG_TO_THIS_SCRIPT(Ped ped, bool) { return ped == kSpawnedPed; }
static bool _IS_ENTITY_OWNED_BY_PERSISTENCE_SYSTEM(Ped) { return false; }
static void SET_ENTITY_LOAD_COLLISION_FLAG(Ped ped, bool flag)
{
    Check(ped == kSpawnedPed && world.spawnedAlive && !flag, "cleanup disables collision loading only on its own live ped");
}
static void SET_ENTITY_AS_NO_LONGER_NEEDED(Ped* ped)
{
    Check(*ped == kSpawnedPed && world.spawnedAlive, "release only touches the tracked ped");
    *ped = 0;
}
static bool DOES_ENTITY_EXIST(int entity)
{
    if (entity == kSpawnedPed && world.spawnedPending && world.frame >= world.spawnVisibleFrame)
    {
        world.spawnedPending = false;
        world.spawnedAlive = true;
    }
    return ((entity == kPlayer || entity == world.playerId) && world.playerAlive) ||
        (entity == kSpawnedPed && world.spawnedAlive) || (entity == kGiver && world.giverAlive);
}
static void SET_ENTITY_AS_MISSION_ENTITY(Ped ped, bool scriptHostObject, bool grabFromOtherScript)
{
    Check(ped == kSpawnedPed && world.spawnedAlive && world.modelHeld,
        "only a live created ped is claimed with its model held");
    Check(scriptHostObject && grabFromOtherScript, "mission ownership flags are preserved");
    Check(world.spawnVisibleFrame == world.frame, "mission ownership is acquired when the created ped first becomes observable");
    world.missionOwned = true;
    ++world.missionClaims;
}
static void PLACE_ENTITY_ON_GROUND_PROPERLY(Ped ped, int flags)
{
    Check(ped == kSpawnedPed && world.spawnedAlive && world.missionOwned && world.modelHeld && flags == 1,
        "ground placement only processes the live owned ped");
    Check(world.outfits == world.placements + 1,
        "each ped's appearance is initialized before placement and returning it for portrait capture");
    ++world.placements;
}
}

namespace PED
{
static int _GET_NUM_FREE_SLOTS_IN_PED_POOL()
{
    ++world.poolQueries;
    return world.frame < world.poolAvailableFrame ? 0 : world.freePedSlots;
}
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
    Check(_GET_NUM_FREE_SLOTS_IN_PED_POOL() > 0, "creation never runs while the ped pool is full");
    Check(!world.spawnedPending && !world.spawnedAlive, "a new creation never replaces an unresolved owned handle");
    Check(world.createFrames.empty() || world.createFrames.back() < world.frame,
        "failed creation attempts are separated by game frames");
    world.createFrames.push_back(world.frame);
    world.createTimes.push_back(world.nowMs);
    ++world.creates;
    if (world.creates <= world.failCreateAttempts)
        return world.failedCreateReturnsStaleHandle ? 88 : 0;
    world.spawnVisibleFrame = world.frame + world.createVisibilityDelayFrames;
    world.spawnedPending = world.createVisibilityDelayFrames != 0;
    world.spawnedAlive = !world.spawnedPending;
    if (world.creationFillsPool) world.freePedSlots = 0;
    if (world.changePlayerOnCreate) world.playerId = 99;
    return kSpawnedPed;
}
static void _SET_RANDOM_OUTFIT_VARIATION(Ped ped, bool)
{
    Check(ped == kSpawnedPed && world.spawnedAlive && world.missionOwned && world.modelHeld,
        "outfit initialization only processes the live owned ped");
    ++world.outfits;
    ++world.randomOutfits;
}
[[maybe_unused]] static void _EQUIP_META_PED_OUTFIT_PRESET(Ped ped, int preset, bool p2)
{
    Check(ped == kSpawnedPed && world.spawnedAlive && world.missionOwned && world.modelHeld && !p2,
        "routine appearance is applied once to the owned ped before model release");
    ++world.outfits; ++world.presetOutfits; world.lastPreset = preset;
}
static void DELETE_PED(Ped* ped)
{
    Check(*ped == kSpawnedPed && world.spawnedAlive, "interrupted successful creation deletes its ped");
    if (world.deleteSucceeds)
    {
        if (world.deleteDelayFrames) world.deletionFrame = world.frame + world.deleteDelayFrames;
        else world.spawnedAlive = false;
        world.spawnedPending = false;
    }
    ++world.deletes;
    *ped = 0;
}
}

static struct { bool cardOpenPending = false; } C;
static struct { int obj = 0; bool examining = false; Ped inspectingPed = 0; } Cd;
static struct { bool active = false; } handoff;
static bool OwnCardTaskRunning() { return world.ownCardTask; }
static void DisplaySubtitle(const char* message) { world.messages.push_back(message); }
static void UpdatePlayer();
static bool CanStartInteraction();
static bool ContractActive();
static bool StartContract();
static bool BeginHandoff(Ped giver, bool payout);
struct GiverSpot {};
static const GiverSpot* FindGiverSpot(Ped giver)
{
    static const GiverSpot spot;
    Check(giver == kGiver && world.giverAlive, "delayed clerk validation only examines the retained living giver");
    return world.giverSpotMatches ? &spot : nullptr;
}
static void LogContractStartFailure(Hash model, int attempt);
static void ReportContractStartFailure();
static void LogOwnedPedCleanup(const char*) {}

#include "spawn_under_test.h"

static bool ContractActive() { return world.contractActive; }
static void UpdatePlayer()
{
    Check(world.frame > 0, "remote start refreshes its snapshot after the fresh-frame yield");
    ++world.playerRefreshes;
    pedMe = PLAYER::PLAYER_PED_ID();
}
static bool CanStartInteraction() { return PlayerAvailable() && world.interactionAllowed; }
static bool StartContract()
{
    Check(CanStartInteraction() && !world.paused && !world.faded && !Cd.obj && !handoff.active && !ownedPed.cleanupPending,
        "queued creation starts only while interaction, card and cleanup guards permit it");
    ++world.contractStarts;
    world.startTimes.push_back(RuntimeNowMs());
    world.nowMs += world.attemptDurationMs;
    if (world.losePlayerDuringStart) world.playerAlive = false;
    if (world.locationFailures)
    {
        --world.locationFailures;
        lastStartFailure = ContractStartFailure::LocationUnavailable;
        ownedPed.cleanupPending = world.cleanupAfterLocationFailure;
        return false;
    }
    if (world.forcedStartFailure >= 0)
    {
        lastStartFailure = static_cast<ContractStartFailure>(world.forcedStartFailure);
        return false;
    }
    lastStartFailure = world.remoteStartSucceeds ? ContractStartFailure::None : ContractStartFailure::PedCreationFailed;
    if (world.remoteStartSucceeds) g_state = CONTRACT_UNKNOWN;
    return world.remoteStartSucceeds;
}
static bool BeginHandoff(Ped giver, bool payout)
{
    Check(!pendingContractStart.player && !payout, "completed request retires before its ordinary clerk handoff");
    ++world.handoffCalls; world.handoffGiver = giver;
    return world.handoffSucceeds;
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
    StartupTrace::sink = nullptr;
    world = World();
    pedMe = kPlayer;
    C = {};
    Cd = {};
    handoff = {};
    pendingContractStart = {};
    g_state = CONTRACT_NONE;
    photoSlotsBound = 0;
    pausedDurationMs = pauseStartedMs = 0;
    remoteRequestPlayer = 0;
    remoteRequestUntilMs = 0;
    remoteCardPlayer = 0;
    remoteCardUntilMs = 0;
    ownedPed = OwnedPedRuntime();
    ownedPedsCreated = ownedPedsDeleted = ownedPedsReleased = 0;
    lastStartFailure = ContractStartFailure::None;
}

static Ped Spawn() { return SpawnPed(kModel, Vector3{ 1.0f, 2.0f, 3.0f }); }

static void TestNativeAppearance()
{
    Reset();
    Check(Spawn() == kSpawnedPed && world.randomOutfits == 1 && world.presetOutfits == 0,
        "native target spawning selects one random appearance before its portrait");
    Check(world.outfits == 1 && world.placements == 1 && world.releases == 1,
        "random appearance retains ordinary ground placement and model cleanup");
}

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
    const ULONGLONG pendingStarted = world.nowMs;
    Check(Spawn() == 0 && world.creates == 1 && lastStartFailure == ContractStartFailure::PedCreationFailed,
        "an unresolved nonzero handle is never overwritten by another creation");
    Check(world.nowMs - pendingStarted >= Tune::kPedSpawnRetryMs && world.outfits == 0 && world.placements == 0,
        "unresolved handle observation stays bounded without initializing an invalid entity");
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

static void TestPendingHandleAndPoolCapacity()
{
    Reset();
    world.createVisibilityDelayFrames = 1;
    world.creationFillsPool = true;
    Check(Spawn() == kSpawnedPed && world.creates == 1 && world.frame == 1,
        "a pending handle is accepted on the next frame without creating a second ped");
    Check(world.missionClaims == 1 && world.freePedSlots == 0,
        "a pending owned ped can finish initialization even when it consumed the last slot");
    CheckReleased();

    Reset();
    world.poolAvailableFrame = 4;
    Check(Spawn() == kSpawnedPed && world.creates == 1 && world.createFrames.front() >= 4,
        "temporary pool exhaustion waits for capacity before its first creation");
    Check(world.maintenance > 4 && lastStartFailure == ContractStartFailure::None,
        "waiting for capacity maintains portrait work and preserves eventual success");
    CheckReleased();

    Reset();
    world.freePedSlots = 0;
    const ULONGLONG started = world.nowMs;
    Check(Spawn() == 0 && lastStartFailure == ContractStartFailure::PedPoolFull && world.creates == 0,
        "persistent pool exhaustion reports a distinct failure without creating peds");
    Check(world.nowMs - started >= Tune::kPedSpawnRetryMs &&
        world.nowMs - started <= Tune::kPedSpawnRetryMs + kFrameMs,
        "pool capacity waiting uses the existing bounded retry deadline");
    Check(world.deletes == 0 && world.missionClaims == 0,
        "pool exhaustion never deletes or claims another script's peds");
    CheckReleased();

    Reset();
    world.createVisibilityDelayFrames = 1;
    world.playerChangeFrame = 1;
    Check(Spawn() == 0 && lastStartFailure == ContractStartFailure::Interrupted && world.creates == 1,
        "player cancellation preserves interruption after a pending creation");
    Check(world.deletes == 1 && !world.spawnedAlive && world.missionClaims == 0,
        "a pending handle that becomes live during cancellation is deleted before release");
    CheckReleased();
}

static void TestPendingCleanupBlocksReplacement()
{
    Reset();
    Check(Spawn() == kSpawnedPed, "cleanup test creates one owned target");
    world.deleteSucceeds = false;
    RequestOwnedPedCleanup(kSpawnedPed);
    const ULONGLONG started = world.nowMs;
    Check(ownedPed.ped == kSpawnedPed && world.spawnedAlive,
        "native pointer clearing cannot discard a still-existing target");
    Check(Spawn() == 0 && lastStartFailure == ContractStartFailure::CleanupPending,
        "unconfirmed cleanup refuses a replacement contract");
    Check(world.creates == 1 && world.requests == 1 && ownedPed.ped == kSpawnedPed,
        "pending cleanup cannot acquire another model or overwrite its tracked handle");
    Check(world.nowMs - started >= kOwnedPedCleanupWaitMs &&
        world.nowMs - started <= kOwnedPedCleanupWaitMs + kFrameMs,
        "waiting for cleanup returns within its bounded deadline");

    world.deleteSucceeds = true;
    world.deleteDelayFrames = 2;
    Check(Spawn() == kSpawnedPed && world.creates == 2 && ownedPedsDeleted == 1,
        "a later confirmed deletion allows exactly one replacement spawn");
    ReleaseOwnedPed(kSpawnedPed);
    Check(!ownedPed.ped && world.spawnedAlive && ownedPedsReleased == 1,
        "completed target release relinquishes tracking without deleting its corpse");
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
    world.contractActive = true;
    StartRemoteContract();
    Check(world.contractStarts == 1 && C.cardOpenPending && world.messages.size() == 2 &&
        std::strcmp(world.messages[0], "REPLACING CONTRACT") == 0 &&
        std::strcmp(world.messages[1], "FIND THE TARGET") == 0,
        "a remote request during an open hunt announces the replacement before the new hunt objective");

    Reset();
    world.playerChangeFrame = 1;
    StartRemoteContract();
    Check(pedMe == 99 && CanStartInteraction(), "the replacement player is otherwise eligible");
    Check(world.contractStarts == 0 && !C.cardOpenPending && !pendingContractStart.player && world.failureReports == 0,
        "a different eligible player cannot inherit the previous player's request");

    Reset();
    world.interactionBlockedFrame = 1;
    StartRemoteContract();
    Check(world.contractStarts == 0 && !C.cardOpenPending && world.failureReports == 0 && pendingContractStart.player == kPlayer,
        "losing interaction eligibility during the fresh frame keeps the original request pending");
    world.interactionAllowed = true;
    UpdatePendingContractStart();
    Check(world.contractStarts == 1 && C.cardOpenPending && !pendingContractStart.player,
        "the retained fresh-frame request completes once interaction becomes available");

    Reset();
    world.remoteStartSucceeds = false;
    StartRemoteContract();
    Check(world.contractStarts == 1 && !C.cardOpenPending && world.failureReports == 0 && pendingContractStart.player &&
        lastStartFailure == ContractStartFailure::PedCreationFailed,
        "transient creation failure keeps the request pending without claiming the card is ready");
    Check(world.messages.size() == 1, "failed remote creation never displays a successful hunt objective");
}

static void TestPauseDuringWait()
{
    Reset();
    const ULONGLONG started = RuntimeNowMs();
    world.pauseFrame = 1;
    world.resumeFrame = 5;
    Check(WaitUntil(1000, [] { return world.frame == 6; }), "wait completes across a whole pause interval");
    Check(world.nowMs == started + 6 * kFrameMs && RuntimeNowMs() == started + 2 * kFrameMs,
        "streaming uses wall time while runtime excludes a pause entirely inside the wait");

    Reset();
    world.pauseFrame = 1;
    Check(!WaitUntil(100, [] { return false; }), "a paused streaming operation still has a bounded timeout");
    Check(RuntimeNowMs() == 1000 + kFrameMs, "runtime stays frozen when a wait returns during pause");
    world.nowMs += 5000;
    SetRuntimePaused(false);
    Check(RuntimeNowMs() == 1000 + kFrameMs, "resuming after the timeout excludes the remaining pause");
}

static void TestPendingLocationStarts()
{
    Reset();
    world.contractActive = true; g_state = CONTRACT_FOUND;
    world.locationFailures = 2; world.attemptDurationMs = 6000;
    StartRemoteContract();
    Check(world.contractStarts == 1 && pendingContractStart.player == kPlayer && !C.cardOpenPending &&
        g_state == CONTRACT_FOUND && world.contractActive, "failed location preparation preserves the existing hunt and one pending replacement");
    const unsigned frame = world.frame, refreshes = world.playerRefreshes;
    StartRemoteContract(); RequestContractStart(77); UpdatePendingContractStart();
    Check(world.frame == frame && world.playerRefreshes == refreshes && world.contractStarts == 1 &&
        pendingContractStart.giver == 0 && world.messages.size() == 1,
        "repeated U, clerk requests and the same-frame pump cannot stack or restart pending work");
    world.nowMs = pendingContractStart.nextAttemptMs - 1;
    UpdatePendingContractStart();
    Check(world.contractStarts == 1, "a failed slow attempt receives a full retry delay after it finishes");
    ++world.nowMs; UpdatePendingContractStart();
    Check(world.contractStarts == 2 && world.failureReports == 0 && world.messages.size() == 1 && !C.cardOpenPending,
        "repeated location failures remain quiet without claiming the card is ready");
    world.nowMs = pendingContractStart.nextAttemptMs; UpdatePendingContractStart();
    Check(world.contractStarts == 3 && !pendingContractStart.player && C.cardOpenPending && world.messages.size() == 2,
        "eventual target and portrait success schedules exactly one inspection and completes the request");
    Check(world.startTimes[1] - world.startTimes[0] >= 7000 && world.startTimes[2] - world.startTimes[1] >= 7000,
        "bounded location work cannot cause immediate back-to-back retries after its yields");
    UpdatePendingContractStart(); UpdatePendingContractStart();
    Check(world.contractStarts == 3 && world.messages.size() == 2, "completed work never starts another contract automatically");
}

static void TestPendingStartGuardsAndCancellation()
{
    for (int blocked = 0; blocked < 5; ++blocked)
    {
        Reset(); world.locationFailures = 1;
        RequestContractStart(0); UpdatePendingContractStart();
        world.nowMs = pendingContractStart.nextAttemptMs;
        switch (blocked)
        {
        case 0: world.paused = true; break;
        case 1: world.faded = true; break;
        case 2: world.interactionAllowed = false; break; // Foreign native item task, mounting or combat.
        case 3: Cd.obj = 123; break;
        case 4: handoff.active = true; break;
        }
        UpdatePendingContractStart();
        Check(world.contractStarts == 1 && pendingContractStart.player == kPlayer && world.failureReports == 0,
            "pause, fade, foreign interaction, card inspection and handoff suspend the existing request");
        world.paused = world.faded = false; world.interactionAllowed = true; Cd.obj = 0; handoff.active = false;
        UpdatePendingContractStart();
        Check(world.contractStarts == 2 && C.cardOpenPending && !pendingContractStart.player,
            "the same request resumes after its temporary blocker clears");
    }
    for (int cancelled = 0; cancelled < 6; ++cancelled)
    {
        Reset(); world.locationFailures = 1;
        RequestContractStart(0); UpdatePendingContractStart();
        world.nowMs = pendingContractStart.nextAttemptMs;
        switch (cancelled)
        {
        case 0: world.playerAlive = false; break;
        case 1: world.playerDying = true; break;
        case 2: world.playerId = pedMe = 99; break;
        case 3: g_state = CONTRACT_DEAD; break;
        case 4: g_state = CONTRACT_PAID; break;
        case 5: CancelPendingContractStart(); break; // Explicit End Contract routing is asserted by the runner.
        }
        UpdatePendingContractStart();
        Check(world.contractStarts == 1 && !pendingContractStart.player && !C.cardOpenPending && world.failureReports == 0,
            "death, player change, proof, payment and explicit cancellation cannot start a queued replacement");
        world.playerAlive = true; world.playerDying = false; world.playerId = pedMe = kPlayer; g_state = CONTRACT_NONE;
        UpdatePendingContractStart();
        Check(world.contractStarts == 1, "cancelled work cannot resume when the old blocker later disappears");
    }
    Reset(); world.forcedStartFailure = static_cast<int>(ContractStartFailure::Interrupted);
    RequestContractStart(0); UpdatePendingContractStart();
    world.nowMs = pendingContractStart.nextAttemptMs; world.paused = true;
    UpdatePendingContractStart();
    Check(world.contractStarts == 1 && pendingContractStart.player && world.failureReports == 0,
        "an interruption during a yielding startup keeps the living player's request for later");
    world.paused = false; world.forcedStartFailure = -1; UpdatePendingContractStart();
    Check(C.cardOpenPending && world.contractStarts == 2, "interrupted startup completes after its same-player pause ends");

    Reset(); world.losePlayerDuringStart = true; world.locationFailures = 1;
    RequestContractStart(0); UpdatePendingContractStart();
    Check(!pendingContractStart.player && !C.cardOpenPending, "a player lost during startup cannot retain its request");
}

static void TestPendingCleanupAndGiverDelivery()
{
    for (bool giverAvailable : {false, true})
    {
        Reset(); world.locationFailures = 1; world.cleanupAfterLocationFailure = true;
        world.handoffSucceeds = giverAvailable;
        RequestContractStart(kGiver); UpdatePendingContractStart();
        Check(ownedPed.cleanupPending && pendingContractStart.player == kPlayer && pendingContractStart.giver == kGiver,
            "a post-deployment location failure retains its clerk and provisional cleanup");
        world.nowMs = pendingContractStart.nextAttemptMs + 5000;
        UpdatePendingContractStart();
        Check(world.contractStarts == 1 && world.failureReports == 0,
            "pending provisional cleanup delays the next pool preflight instead of ending the request");
        ownedPed.cleanupPending = false; UpdatePendingContractStart();
        Check(world.contractStarts == 2 && world.handoffCalls == 1 && world.handoffGiver == kGiver &&
            !pendingContractStart.player && C.cardOpenPending == !giverAvailable,
            "delayed success uses the original clerk or existing inspection fallback if handoff is unavailable");
    }
    for (bool living : {false, true})
    {
        Reset(); world.locationFailures = 1;
        RequestContractStart(kGiver); UpdatePendingContractStart();
        world.giverAlive = living; world.giverSpotMatches = false;
        world.nowMs = pendingContractStart.nextAttemptMs; UpdatePendingContractStart();
        Check(world.contractStarts == 2 && !pendingContractStart.player && C.cardOpenPending && world.handoffCalls == 0,
            "a missing clerk or a handle that no longer matches its station falls back to the prepared card");
    }
    Reset(); world.forcedStartFailure = static_cast<int>(ContractStartFailure::CleanupPending);
    RequestContractStart(0); UpdatePendingContractStart();
    Check(pendingContractStart.player && world.failureReports == 0, "cleanup discovered inside startup also retains the pending request");
    world.forcedStartFailure = -1; world.nowMs = pendingContractStart.nextAttemptMs; UpdatePendingContractStart();
    Check(C.cardOpenPending && !pendingContractStart.player, "delayed cleanup can eventually finish the original request");

    for (ContractStartFailure failure : {ContractStartFailure::InvalidModel, ContractStartFailure::ModelLoadTimeout,
        ContractStartFailure::PedCreationFailed, ContractStartFailure::PortraitFailed, ContractStartFailure::PedPoolFull})
    {
        Reset(); world.forcedStartFailure = static_cast<int>(failure);
        RequestContractStart(0); UpdatePendingContractStart();
        Check(pendingContractStart.player && world.failureReports == 0 && !C.cardOpenPending,
            "model, pool, creation and portrait failures retain the original request quietly");
        world.nowMs += 2999; UpdatePendingContractStart();
        Check(world.contractStarts == 1, "resource failures receive a longer three-second backoff");
        ++world.nowMs; UpdatePendingContractStart();
        Check(world.contractStarts == 2 && pendingContractStart.player && world.failureReports == 0 && world.messages.size() == 1,
            "repeated resource failures do not stack requests or failure subtitles");
        world.forcedStartFailure = -1; world.nowMs = pendingContractStart.nextAttemptMs;
        ownedPed.cleanupPending = true; UpdatePendingContractStart();
        Check(world.contractStarts == 2, "resource retries still wait for owned provisional cleanup");
        ownedPed.cleanupPending = false; UpdatePendingContractStart(); UpdatePendingContractStart();
        Check(world.contractStarts == 3 && !pendingContractStart.player && C.cardOpenPending && world.messages.size() == 2,
            "transient resource failure eventually yields exactly one prepared card");
    }
    for (ContractStartFailure failure : {ContractStartFailure::PhotoCacheExhausted,
        ContractStartFailure::PhotoDiagnosticComplete, ContractStartFailure::None})
    {
        Reset(); world.forcedStartFailure = static_cast<int>(failure);
        world.contractActive = true; g_state = CONTRACT_FOUND;
        RequestContractStart(0); UpdatePendingContractStart(); UpdatePendingContractStart();
        Check(!pendingContractStart.player && world.contractStarts == 1 && world.failureReports == 1 &&
            world.reportedFailure == static_cast<int>(failure) && !C.cardOpenPending && g_state == CONTRACT_FOUND && world.contractActive,
            "cache exhaustion, diagnostic completion and an unclassified failure report once without replacing the old hunt");
    }
}

static void TestContractPreflight()
{
    Reset();
    world.freePedSlots = 0;
    photoSlotsBound = 0xFFFFFFFFu;
    Check(!CanPrepareContract() && lastStartFailure == ContractStartFailure::PhotoCacheExhausted,
        "all inspected slots refuse preparation before the caller clears its active contract");
    Check(photoSlotsBound == 0xFFFFFFFFu && world.creates == 0 && world.releases == 0,
        "exhaustion neither resets slot ownership nor starts capture");
    Check(world.poolQueries == 0 && std::strcmp(lastPhotoStage, "photo_slots_exhausted") == 0,
        "exhausted portrait slots retain priority over a full ped pool without querying it");
    world.freePedSlots = 1;
    for (int slot = 0; slot < Card::kPhotoSlotCount; ++slot)
    {
        photoSlotsBound = 0xFFFFFFFFu & ~(1u << slot);
        Check(CanPrepareContract(), "any single remaining slot permits preparation");
    }
    Check(world.poolQueries == Card::kPhotoSlotCount && world.requests == 0 && world.frame == 0,
        "one free ped slot is sufficient and each eligible preflight queries the pool once without loading or waiting");
    Reset();
    world.playerDying = true;
    world.freePedSlots = 0;
    photoSlotsBound = 0xFFFFFFFFu;
    Check(!CanPrepareContract() && lastStartFailure == ContractStartFailure::Interrupted,
        "an unavailable player is rejected before contract reset");
    Check(world.poolQueries == 0 && world.failureLogs == 1,
        "player interruption retains priority over portrait exhaustion and a full pool");

    for (const int capacity : {0, -1})
    {
        Reset();
        world.freePedSlots = capacity;
        world.contractActive = true;
        world.spawnedAlive = world.missionOwned = true;
        g_state = CONTRACT_FOUND;
        C.cardOpenPending = true;
        Cd = { 123, true, kPlayer };
        photoSlotsBound = 5u;
        ownedPed.ped = kSpawnedPed;
        ownedPed.model = kModel;
        StartupTrace::sink = CollectStartupTrace;
        const ULONGLONG started = world.nowMs;
        Check(!CanPrepareContract() && lastStartFailure == ContractStartFailure::PedPoolFull,
            "zero or negative available ped capacity refuses preparation before replacing the hunt");
        Check(world.contractActive && g_state == CONTRACT_FOUND && C.cardOpenPending &&
            Cd.obj == 123 && Cd.examining && Cd.inspectingPed == kPlayer && photoSlotsBound == 5u,
            "pool rejection preserves the active hunt, card state and bound portrait slots");
        Check(ownedPed.ped == kSpawnedPed && ownedPed.model == kModel && !ownedPed.cleanupPending &&
            world.spawnedAlive && world.missionOwned && world.deletes == 0 && ownedPedsReleased == 0,
            "pool rejection leaves the existing owned target intact without cleanup");
        Check(world.poolQueries == 1 && world.failureLogs == 1 && world.requests == 0 &&
            world.releases == 0 && world.creates == 0 && world.placements == 0 &&
            world.frame == 0 && world.nowMs == started && world.maintenance == 0,
            "pool preflight rejects synchronously without model loading, spawning or yielding");
        Check(world.startupEvents.size() == 2 &&
            std::strcmp(world.startupEvents[0].stage, "preflight_pool_query") == 0 &&
            std::strcmp(world.startupEvents[1].stage, "preflight_pool_result") == 0,
            "synchronous breadcrumbs bracket the native pool query in order");
        const auto& result = world.startupEvents[1];
        Check(result.freePeds == capacity && result.model == 0 && result.ped == 0 && !result.hasPoint,
            "pool breadcrumb records the exact capacity before selecting a model or spawn point");
    }

    Reset();
    world.poolAvailableFrame = 1;
    Check(!CanPrepareContract() && lastStartFailure == ContractStartFailure::PedPoolFull &&
        world.frame == 0 && world.poolQueries == 1 && world.requests == 0 && world.creates == 0,
        "preflight does not wait for a temporarily full pool to gain capacity");

    Reset();
    world.freePedSlots = 1;
    Check(CanPrepareContract() && world.failureLogs == 0 &&
        lastStartFailure == ContractStartFailure::None && world.startupEvents.empty(),
        "one free slot permits preparation with the optional breadcrumb sink disabled");
}

static void TestDeferredRemoteInput()
{
    Reset();
    Check(ConsumeRemoteContractRequest(true), "idle U is consumed immediately");
    Check(!ConsumeRemoteContractRequest(false), "one release cannot start twice");

    Cd = { 123, true, kPlayer };
    world.ownCardTask = true;
    world.itemState = Card::kStateOutro;
    world.interactionAllowed = false;
    Check(!ConsumeRemoteContractRequest(true) && remoteRequestPlayer == kPlayer,
        "U during owned card outro is retained while its prop exists");
    world.nowMs += 400; // well beyond keyboard.cpp's 100 ms release window
    Check(!ConsumeRemoteContractRequest(false), "pending request waits for the outro");
    Cd = {};
    Check(!ConsumeRemoteContractRequest(false) && remoteRequestPlayer == kPlayer,
        "prop retirement does not discard U while the native outro still runs");
    world.itemState = 0;
    world.interactionAllowed = true;
    Check(ConsumeRemoteContractRequest(false), "the stored release runs after prop and task retirement");
    Check(!ConsumeRemoteContractRequest(false), "the stored release runs exactly once");

    Reset();
    Cd = { 123, true, kPlayer };
    world.ownCardTask = true;
    world.interactionAllowed = false;
    Check(!ConsumeRemoteContractRequest(false), "own inspection is observed before prop retirement");
    Cd = {};
    world.itemState = Card::kStateOutro;
    Check(!ConsumeRemoteContractRequest(true) && remoteRequestPlayer == kPlayer,
        "a first U release after prop retirement still queues during our remembered outro");
    world.itemState = 0;
    world.interactionAllowed = true;
    Check(ConsumeRemoteContractRequest(false), "post-retirement first release runs when the native outro finishes");

    for (int cancel = 0; cancel < 6; ++cancel)
    {
        Reset();
        Cd = { 123, true, kPlayer };
        world.itemState = Card::kStateOutro;
        world.ownCardTask = true;
        world.interactionAllowed = false;
        Check(!ConsumeRemoteContractRequest(true) && remoteRequestPlayer, "cancellation test queues one outro release");
        if (cancel == 0) world.paused = true;
        if (cancel == 1) world.faded = true;
        if (cancel == 2) { world.playerId = 99; pedMe = 99; }
        if (cancel == 3) world.nowMs += 3000;
        if (cancel == 4) handoff.active = true;
        if (cancel == 5) g_state = CONTRACT_PAID;
        Check(!ConsumeRemoteContractRequest(false) && !remoteRequestPlayer,
            "pause, fade, player change, expiry, handoff and payment cancel a queued request");
    }
    Reset();
    world.interactionAllowed = false;
    Check(!ConsumeRemoteContractRequest(true) && !remoteRequestPlayer, "other blocked interactions do not queue U");
    Reset();
    Cd = { 123, true, kPlayer };
    world.ownCardTask = true;
    Check(!ConsumeRemoteContractRequest(true) && !remoteRequestPlayer, "ordinary inspection requires put-away before replacing");
    Reset();
    g_state = CONTRACT_DEAD;
    Check(ConsumeRemoteContractRequest(true), "a photographed contract retains the existing U reopen path");
}

int main()
{
    Check(Tune::kPedSpawnRetryMs > Tune::kPedSpawnRetryDelayMs && Tune::kPedSpawnRetryDelayMs > 0,
        "production retry timing allows multiple delayed attempts");
    Check(static_cast<int>(ContractStartFailure::PortraitFailed) == 5 &&
        static_cast<int>(ContractStartFailure::PedPoolFull) == 6,
        "pool exhaustion is appended without changing existing diagnostic codes");
    TestWaitPredicate();
    TestNativeAppearance();
    TestDelayedCreation();
    TestStaleHandleAndBoundedFailure();
    TestPendingHandleAndPoolCapacity();
    TestPendingCleanupBlocksReplacement();
    TestModelFailures();
    TestPlayerInterruption();
    TestRemoteStart();
    TestPendingLocationStarts();
    TestPendingStartGuardsAndCancellation();
    TestPendingCleanupAndGiverDelivery();
    TestPauseDuringWait();
    TestContractPreflight();
    TestDeferredRemoteInput();
    std::printf("All %u spawn checks passed (actual production functions).\n", checks);
}
