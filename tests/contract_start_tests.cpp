// Real startup orchestration and retry pump; individual helper internals have their own suites.
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using Ped = int;
using Hash = unsigned;
using ULONGLONG = std::uint64_t;
constexpr Ped kPlayer = 42, kOldTarget = 70, kNewTarget = 77, kGiver = 88;
constexpr Hash kModel = 123;
static unsigned checks = 0;
static void Check(bool value, const char* message)
{
    ++checks;
    if (!value) { std::fprintf(stderr, "FAILED: %s\n", message); std::exit(EXIT_FAILURE); }
}

struct Vector3 { float x = 0, y = 0, z = 0; };
struct ModelSet { const Hash* list = nullptr; int count = 0; };
struct ContractDef;
struct TargetBehavior { void (*setup)(Ped, const ContractDef&); };
struct ContractDef
{
    Vector3 spawn;
    ModelSet models;
    const TargetBehavior* behavior = nullptr;
    void (*onSpawned)(const ContractDef&) = nullptr;
};
struct RoutineRuntime
{
    bool enabled = false;
    int planIdentity = 0;
    std::array<std::string, 6> cardLines;
    ContractDef definition;
};
struct ActiveContract
{
    const ContractDef* def = nullptr;
    Ped target = 0;
    Vector3 targetPos;
    ULONGLONG startMs = 0;
    bool cardOpenPending = false;
    int photoIdentity = 0;
};
struct GiverSpot {};
static Ped pedMe = kPlayer;
static RoutineRuntime R;
static ActiveContract C;
static struct { int obj = 0; } Cd;
static struct { bool active = false; } handoff;
static struct { bool cleanupPending = false; } ownedPed;
static struct World
{
    ULONGLONG now = 1000;
    Ped player = kPlayer;
    bool playerAlive = true, canInteract = true, paused = false, faded = false;
    bool interruptPreparation = false, changePlayerAfterSpawn = false;
    bool giverAlive = true, giverAtSpot = true, handoffSucceeds = true;
    int prepareFailures = 0, spawnFailures = 0;
    int preflightFailure = -1, spawnFailure = -1;
    unsigned preflights = 0, preparations = 0, clears = 0, oldHuntsCleared = 0;
    unsigned spawns = 0, setups = 0, spawnedHooks = 0, blips = 0;
    unsigned cleanupRequests = 0, photoReleases = 0, handoffs = 0, reports = 0, readyMessages = 0;
} world;
namespace StartupTrace { static void Record(const char*, unsigned = 0, int = 0, const Vector3* = nullptr) {} }
namespace RoutineSpawn { static struct { const char* check = "none"; } diagnostic; }
namespace HUD { static bool IS_PAUSE_MENU_ACTIVE() { return world.paused; } }
namespace CAMERA { static bool IS_SCREEN_FADED_OUT() { return world.faded; } }
static bool PlayerAvailable() { return world.playerAlive && world.player == pedMe; }
static bool CanStartInteraction() { return PlayerAvailable() && world.canInteract && !world.paused && !world.faded; }
static ULONGLONG RuntimeNowMs() { return world.now; }
static bool LivingPed(Ped ped) { return ped == kGiver && world.giverAlive; }
static const GiverSpot* FindGiverSpot(Ped ped)
{
    static const GiverSpot spot;
    Check(ped == kGiver && world.giverAlive, "delayed delivery validates only the living requested clerk");
    return world.giverAtSpot ? &spot : nullptr;
}
static void WAIT(int delay) { Check(delay == 0, "model retries yield one frame"); world.now += 16; }
static void DisplaySubtitle(const char* text)
{
    if (std::strcmp(text, "FIND THE TARGET") == 0) ++world.readyMessages;
}
static bool ContractActive();
static bool CanPrepareContract();
static bool PrepareRoutineContract(RoutineRuntime& prepared);
static void ClearContract(bool deleteTarget);
static void ResetRoutine();
static Ped SpawnTargetWithPhoto(Hash model, const ContractDef& def);
static void RequestOwnedPedCleanup(Ped ped);
static void ReleaseTargetPhoto();
static void AddSearchBlip();
static bool BeginHandoff(Ped giver, bool payout);
static void LogContractStartFailure(Hash, int) {}
static void ReportContractStartFailure() { ++world.reports; }

#include "contract_start_under_test.h"

static void SetupTarget(Ped ped, const ContractDef& def)
{
    Check(ped == kNewTarget && C.target == ped && C.def == &R.definition && &def == C.def && C.photoIdentity == 200,
        "setup follows target publication using stable runtime definition and its completed portrait");
    ++world.setups;
}
static void OnSpawned(const ContractDef& def)
{
    Check(&def == &R.definition && world.setups == 1, "spawn callback receives the published definition after setup");
    ++world.spawnedHooks;
}
static const TargetBehavior behavior{SetupTarget};
static bool ContractActive() { return g_state == CONTRACT_UNKNOWN || g_state == CONTRACT_FOUND || g_state == CONTRACT_DEAD; }
static void ResetRoutine() { R = {}; }
static bool CanPrepareContract()
{
    ++world.preflights;
    if (world.preflightFailure < 0) return true;
    lastStartFailure = static_cast<ContractStartFailure>(world.preflightFailure);
    return false;
}
static bool PrepareRoutineContract(RoutineRuntime& prepared)
{
    ++world.preparations;
    world.now += 100;
    if (world.prepareFailures)
    {
        --world.prepareFailures;
        RoutineSpawn::diagnostic.check = "candidate_budget_exhausted";
        return false;
    }
    static constexpr Hash models[] = {kModel};
    prepared.enabled = true;
    prepared.planIdentity = 200;
    prepared.cardLines[0] = "Prepared target habit";
    prepared.definition = {{10, 20, 30}, {models, 1}, &behavior, OnSpawned};
    if (world.interruptPreparation) world.canInteract = false;
    return true;
}
static void ReleaseTargetPhoto()
{
    ++world.photoReleases;
    C.photoIdentity = 0;
}
static void RequestOwnedPedCleanup(Ped ped)
{
    Check(ped == kNewTarget, "failed provisional deployment retains its own target for cleanup");
    ++world.cleanupRequests;
    ownedPed.cleanupPending = true;
}
static void ClearContract(bool deleteTarget)
{
    Check(deleteTarget, "replacement requests cleanup through the existing contract boundary");
    ++world.clears;
    if (C.target == kOldTarget) ++world.oldHuntsCleared;
    if (C.photoIdentity) ReleaseTargetPhoto();
    C = {};
    ResetRoutine();
    g_state = CONTRACT_NONE;
}
static Ped SpawnTargetWithPhoto(Hash model, const ContractDef& def)
{
    Check(model == kModel && &def == &R.definition && R.enabled && R.planIdentity == 200 && !C.target,
        "target preparation starts only after committing the new runtime and clearing the previous hunt");
    ++world.spawns;
    if (world.spawnFailures)
    {
        --world.spawnFailures;
        lastStartFailure = static_cast<ContractStartFailure>(world.spawnFailure);
        if (lastStartFailure == ContractStartFailure::LocationUnavailable || lastStartFailure == ContractStartFailure::PortraitFailed)
        {
            // This boundary represents the separately tested capture/deployment cleanup.
            C.photoIdentity = 200;
            RequestOwnedPedCleanup(kNewTarget);
            ReleaseTargetPhoto();
        }
        return 0;
    }
    C.photoIdentity = 200;
    if (world.changePlayerAfterSpawn) world.player = 99;
    return kNewTarget;
}
static void AddSearchBlip()
{
    Check(C.def == &R.definition && world.setups == 1 && world.spawnedHooks == 1,
        "search blip follows completed setup and the spawn callback");
    ++world.blips;
}
static bool BeginHandoff(Ped giver, bool payout)
{
    Check(giver == kGiver && !payout && !pendingContractStart.player && C.target == kNewTarget && world.blips == 1,
        "delivery begins once the request is retired and its target, portrait and search blip are ready");
    ++world.handoffs;
    return world.handoffSucceeds;
}
static void Reset(bool oldHunt = true)
{
    world = {};
    R = {}; C = {}; Cd = {}; handoff = {}; ownedPed = {};
    pedMe = kPlayer;
    CancelPendingContractStart();
    lastStartFailure = ContractStartFailure::None;
    g_state = oldHunt ? CONTRACT_FOUND : CONTRACT_NONE;
    if (oldHunt)
    {
        R.enabled = true; R.planIdentity = 100; R.cardLines[0] = "Existing target habit";
        C.def = &R.definition; C.target = kOldTarget; C.photoIdentity = 100;
    }
}
static void CheckOldHunt()
{
    Check(g_state == CONTRACT_FOUND && C.target == kOldTarget && C.def == &R.definition && C.photoIdentity == 100 &&
        R.enabled && R.planIdentity == 100 && R.cardLines[0] == "Existing target habit" && world.clears == 0 && world.spawns == 0,
        "pre-commit failure preserves the original target, portrait, runtime and card clues");
}
static void PreparationFailureAndCacheExhaustion()
{
    Reset(); world.prepareFailures = 1;
    RequestContractStart(0); UpdatePendingContractStart();
    CheckOldHunt();
    Check(pendingContractStart.player == kPlayer && world.reports == 0 && !C.cardOpenPending,
        "location preparation failure retains one quiet request without claiming delivery");
    world.preflightFailure = static_cast<int>(ContractStartFailure::PhotoCacheExhausted);
    world.now = pendingContractStart.nextAttemptMs;
    UpdatePendingContractStart();
    CheckOldHunt();
    Check(!pendingContractStart.player && world.preparations == 1 && world.reports == 1,
        "exhausted portrait capacity stops before another preparation or replacement");
}
static void DeploymentFailureCleanupAndSuccess()
{
    Reset(); world.spawnFailures = 1; world.spawnFailure = static_cast<int>(ContractStartFailure::LocationUnavailable);
    RequestContractStart(kGiver); UpdatePendingContractStart();
    Check(world.clears == 1 && world.oldHuntsCleared == 1 && world.spawns == 1 && world.cleanupRequests == 1 &&
        world.photoReleases == 2 && !R.enabled && !C.target && !C.def && g_state == CONTRACT_NONE,
        "post-capture location failure clears the old hunt once, retires the provisional portrait and resets unpublished runtime");
    Check(pendingContractStart.player == kPlayer && ownedPed.cleanupPending && world.setups == 0 && world.blips == 0 && !C.cardOpenPending,
        "failed deployment retains the request while withholding setup, search blips and delivery");
    world.now = pendingContractStart.nextAttemptMs + 100;
    UpdatePendingContractStart();
    Check(world.preflights == 1 && world.clears == 1, "pending cleanup prevents another real startup preflight");
    ownedPed.cleanupPending = false;
    UpdatePendingContractStart();
    Check(C.def == &R.definition && C.target == kNewTarget && C.targetPos.x == 10 && R.cardLines[0] == "Prepared target habit" &&
        C.startMs == world.now && world.oldHuntsCleared == 1 && g_state == CONTRACT_UNKNOWN,
        "eventual success publishes a stable new definition without replacing the old hunt twice");
    Check(!pendingContractStart.player && world.setups == 1 && world.blips == 1 && world.handoffs == 1 &&
        world.readyMessages == 1 && !C.cardOpenPending, "successful retry delivers exactly one prepared clerk contract");
    UpdatePendingContractStart(); UpdatePendingContractStart();
    Check(world.spawns == 2 && world.setups == 1 && world.handoffs == 1 && world.readyMessages == 1,
        "completed orchestration cannot create or deliver another contract");
}
static void InterruptedPreparationAndPublication()
{
    Reset(); world.interruptPreparation = true;
    RequestContractStart(0); UpdatePendingContractStart();
    CheckOldHunt();
    Check(lastStartFailure == ContractStartFailure::Interrupted && pendingContractStart.player == kPlayer,
        "interruption after successful destination preparation stops before replacement and retains the living player's request");

    Reset(); world.changePlayerAfterSpawn = true;
    RequestContractStart(0); UpdatePendingContractStart();
    Check(lastStartFailure == ContractStartFailure::Interrupted && !pendingContractStart.player && !R.enabled &&
        !C.target && !C.def && world.cleanupRequests == 1 && world.photoReleases == 2 && world.setups == 0 && world.blips == 0,
        "a player change after the yielding spawn boundary cleans the provisional target without publishing it");
}
static void TransientEngineFailuresRetry()
{
    for (const auto failure : {ContractStartFailure::InvalidModel, ContractStartFailure::ModelLoadTimeout,
        ContractStartFailure::PedCreationFailed, ContractStartFailure::PortraitFailed, ContractStartFailure::PedPoolFull})
    {
        Reset(false);
        if (failure == ContractStartFailure::PedPoolFull) world.preflightFailure = static_cast<int>(failure);
        else { world.spawnFailure = static_cast<int>(failure); world.spawnFailures = Tune::kSpawnAttempts; }
        RequestContractStart(0); UpdatePendingContractStart();
        Check(pendingContractStart.player == kPlayer && pendingContractStart.nextAttemptMs > world.now && world.reports == 0 &&
            !C.target && !C.def && world.setups == 0 && !C.cardOpenPending,
            "a nonterminal engine failure retains the request and withholds unpublished contract data");
        world.preflightFailure = -1; world.spawnFailures = 0; ownedPed.cleanupPending = false;
        world.now = pendingContractStart.nextAttemptMs;
        UpdatePendingContractStart();
        Check(!pendingContractStart.player && C.target == kNewTarget && C.def == &R.definition && C.cardOpenPending &&
            world.setups == 1 && world.blips == 1 && world.readyMessages == 1,
            "the retained engine-failure request eventually publishes one target and schedules its remote card");
    }
}
int main()
{
    PreparationFailureAndCacheExhaustion();
    DeploymentFailureCleanupAndSuccess();
    InterruptedPreparationAndPublication();
    TransientEngineFailuresRetry();
    std::printf("Contract start integration: %u checks passed.\n", checks);
}
