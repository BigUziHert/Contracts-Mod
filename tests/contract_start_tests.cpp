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
struct ActiveContract
{
    const ContractDef* def = nullptr;
    Ped target = 0;
    Vector3 targetPos;
    ULONGLONG startMs = 0;
    ULONGLONG photoMs = 0;
    bool cardOpenPending = false;
    int photoIdentity = 0;
    bool paymentCredited = false;
    int payoutCents = 0;
};
struct GiverSpot {};
static Ped pedMe = kPlayer;
static void SetupTarget(Ped ped, const ContractDef& def);
static void OnSpawned(const ContractDef& def);
static const TargetBehavior behavior{SetupTarget};
static constexpr Hash models[] = {kModel};
static const ContractDef kContracts[] = {{{10, 20, 30}, {models, 1}, &behavior, OnSpawned}};
static constexpr int kContractCount = 1;
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
    bool ownCardTask = false, changePlayerOnCardClose = false, reenterOnCardClose = false;
    int spawnFailures = 0;
    int preflightFailure = -1, spawnFailure = -1;
    unsigned preflights = 0, clears = 0, oldHuntsCleared = 0;
    unsigned spawns = 0, setups = 0, spawnedHooks = 0, blips = 0;
    unsigned cleanupRequests = 0, photoReleases = 0, handoffs = 0, reports = 0, readyMessages = 0;
    unsigned cardCloses = 0, settlements = 0, paidContractClears = 0;
    int paidCents = 0;
} world;
namespace StartupTrace { static void Record(const char*, unsigned = 0, int = 0, const Vector3* = nullptr) {} }
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
static void ClearContract(bool deleteTarget);
static Ped SpawnTargetWithPhoto(Hash model, const ContractDef& def);
static void RequestOwnedPedCleanup(Ped ped);
static void ReleaseTargetPhoto();
static void AddSearchBlip();
static bool BeginHandoff(Ped giver, bool payout);
static bool OwnCardTaskRunning() { return world.ownCardTask; }
static void DestroyCardObject(bool cancel);
static void SettlePayment();
static void LogContractStartFailure(Hash, int) {}
static void ReportContractStartFailure() { ++world.reports; }

#include "contract_start_under_test.h"

static void SetupTarget(Ped ped, const ContractDef& def)
{
    Check(ped == kNewTarget && C.target == ped && C.def == &kContracts[0] && &def == C.def && C.photoIdentity == 200,
        "setup follows target publication using stable static contract definition and its completed portrait");
    ++world.setups;
}
static void OnSpawned(const ContractDef& def)
{
    Check(&def == &kContracts[0] && world.setups == 1, "spawn callback receives the published definition after setup");
    ++world.spawnedHooks;
}
static bool ContractActive() { return g_state == CONTRACT_UNKNOWN || g_state == CONTRACT_FOUND || g_state == CONTRACT_DEAD; }
static bool CanPrepareContract()
{
    ++world.preflights;
    if (world.interruptPreparation) world.canInteract = false;
    if (world.preflightFailure < 0) return true;
    lastStartFailure = static_cast<ContractStartFailure>(world.preflightFailure);
    return false;
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
    Check(deleteTarget || (g_state == CONTRACT_PAID && C.paymentCredited),
        "replacement uses cancellation cleanup while earned-payment settlement releases its target");
    ++world.clears;
    if (!deleteTarget) ++world.paidContractClears;
    if (C.target == kOldTarget) ++world.oldHuntsCleared;
    if (C.photoIdentity) ReleaseTargetPhoto();
    C = {};

    g_state = CONTRACT_NONE;
}
static void DestroyCardObject(bool cancel)
{
    Check(cancel && (Cd.obj || world.ownCardTask), "remote replacement closes only its old card or inspect task");
    ++world.cardCloses;
    if (world.reenterOnCardClose)
    {
        world.reenterOnCardClose = false;
        UpdatePendingContractStart();
    }
    Cd.obj = 0;
    world.ownCardTask = false;
    if (world.changePlayerOnCardClose) world.player = 99;
}
static void SettlePayment()
{
    if (g_state != CONTRACT_PAID || C.paymentCredited) return;
    C.paymentCredited = true;
    ++world.settlements;
    world.paidCents += C.payoutCents;
    ClearContract(false);
}
static Ped SpawnTargetWithPhoto(Hash model, const ContractDef& def)
{
    Check(model == kModel && &def == &kContracts[0] && !C.target,
        "target preparation starts only after committing the new contract and clearing the previous hunt");
    ++world.spawns;
    if (world.spawnFailures)
    {
        --world.spawnFailures;
        lastStartFailure = static_cast<ContractStartFailure>(world.spawnFailure);
        if (lastStartFailure == ContractStartFailure::PortraitFailed)
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
    Check(C.def == &kContracts[0] && world.setups == 1 && world.spawnedHooks == 1,
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
    C = {}; Cd = {}; handoff = {}; ownedPed = {};
    pedMe = kPlayer;
    CancelPendingContractStart();
    lastStartFailure = ContractStartFailure::None;
    g_state = oldHunt ? CONTRACT_FOUND : CONTRACT_NONE;
    if (oldHunt)
    {
        C.def = &kContracts[0]; C.target = kOldTarget; C.photoIdentity = 100;
    }
}
static void CheckOldHunt()
{
    Check(g_state == CONTRACT_FOUND && C.target == kOldTarget && C.def == &kContracts[0] && C.photoIdentity == 100 &&
        world.clears == 0 && world.spawns == 0,
        "pre-commit failure preserves the original target, portrait, contract information");
}
static void PreparationFailureAndCacheExhaustion()
{
    Reset(); world.preflightFailure = static_cast<int>(ContractStartFailure::PedPoolFull);
    RequestContractStart(0); UpdatePendingContractStart();
    CheckOldHunt();
    Check(pendingContractStart.player == kPlayer && world.reports == 0 && !C.cardOpenPending,
        "pool exhaustion preserves the current hunt and a quiet pending request");
    world.preflightFailure = static_cast<int>(ContractStartFailure::PhotoCacheExhausted);
    world.now = pendingContractStart.nextAttemptMs;
    UpdatePendingContractStart();
    CheckOldHunt();
    Check(!pendingContractStart.player && world.preflights == 2 && world.reports == 1,
        "exhausted portrait capacity stops before replacement");
}
static void PortraitFailureCleanupAndSuccess()
{
    Reset(); world.spawnFailures = 1; world.spawnFailure = static_cast<int>(ContractStartFailure::PortraitFailed);
    RequestContractStart(kGiver); UpdatePendingContractStart();
    Check(world.clears == 1 && world.oldHuntsCleared == 1 && world.spawns == 1 && world.cleanupRequests == 1 &&
        world.photoReleases == 2 && !C.target && !C.def && g_state == CONTRACT_NONE,
        "portrait failure clears the old hunt once, retires provisional resources and withholds unpublished contract data");
    Check(pendingContractStart.player == kPlayer && ownedPed.cleanupPending && world.setups == 0 && world.blips == 0 && !C.cardOpenPending,
        "failed deployment retains the request while withholding setup, search blips and delivery");
    world.now = pendingContractStart.nextAttemptMs + 100;
    UpdatePendingContractStart();
    Check(world.preflights == 1 && world.clears == 1, "pending cleanup prevents another real startup preflight");
    ownedPed.cleanupPending = false;
    UpdatePendingContractStart();
    Check(C.def == &kContracts[0] && C.target == kNewTarget && C.targetPos.x == 10 &&
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
        "interruption after successful preflight stops before replacement and retains the living player's request");

    Reset(); world.changePlayerAfterSpawn = true;
    RequestContractStart(0); UpdatePendingContractStart();
    Check(lastStartFailure == ContractStartFailure::Interrupted && !pendingContractStart.player &&
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
        Check(!pendingContractStart.player && C.target == kNewTarget && C.def == &kContracts[0] && C.cardOpenPending &&
            world.setups == 1 && world.blips == 1 && world.readyMessages == 1,
            "the retained engine-failure request eventually publishes one target and schedules its remote card");
    }
}
static void RemoteReplacementAfterCorpseProof()
{
    Reset();
    g_state = CONTRACT_DEAD;
    C.photoMs = 500;
    world.preflightFailure = static_cast<int>(ContractStartFailure::PedPoolFull);
    RequestContractStart(0);
    UpdatePendingContractStart();
    Check(pendingContractStart.player == kPlayer && g_state == CONTRACT_DEAD && C.target == kOldTarget &&
        C.photoMs == 500 && C.photoIdentity == 100 && !world.clears && !world.spawns && !world.settlements,
        "a queued remote replacement preserves corpse proof and portrait when startup preflight fails");
    world.preflightFailure = -1;
    world.now = pendingContractStart.nextAttemptMs;
    UpdatePendingContractStart();
    Check(!pendingContractStart.player && g_state == CONTRACT_UNKNOWN && C.target == kNewTarget &&
        C.photoIdentity == 200 && !C.photoMs && C.cardOpenPending && world.oldHuntsCleared == 1 && !world.settlements,
        "remote replacement commits one fresh hunt after corpse proof once resources are available");

    for (const auto state : {CONTRACT_DEAD, CONTRACT_PAID})
    {
        Reset();
        g_state = state;
        RequestContractStart(kGiver);
        UpdatePendingContractStart();
        Check(!pendingContractStart.player && g_state == state && C.target == kOldTarget && !world.preflights && !world.clears,
            "clerk starts cannot bypass corpse submission or earned-payment collection");
    }
}

static void EarnedPaymentSettlesBeforeRetryingNewHunt()
{
    Reset();
    g_state = CONTRACT_PAID;
    C.payoutCents = 10000;
    world.preflightFailure = static_cast<int>(ContractStartFailure::PedPoolFull);
    RequestContractStart(0);
    UpdatePendingContractStart();
    Check(pendingContractStart.player == kPlayer && g_state == CONTRACT_NONE && !C.target &&
        world.settlements == 1 && world.paidCents == 10000 && world.paidContractClears == 1 && !world.spawns,
        "remote replacement settles an already-earned reward before a new hunt that cannot yet start");
    world.now = pendingContractStart.nextAttemptMs;
    UpdatePendingContractStart();
    Check(pendingContractStart.player == kPlayer && world.settlements == 1 && world.paidCents == 10000 &&
        world.paidContractClears == 1 && world.clears == 1,
        "a retained startup retry cannot settle the old reward or clean its contract twice");
    world.preflightFailure = -1;
    world.now = pendingContractStart.nextAttemptMs;
    UpdatePendingContractStart();
    Check(!pendingContractStart.player && g_state == CONTRACT_UNKNOWN && C.target == kNewTarget && C.cardOpenPending &&
        world.settlements == 1 && world.paidCents == 10000 && world.oldHuntsCleared == 1 && world.spawns == 1,
        "eventual startup publishes one new hunt while preserving exactly one credit for the previous reward");
}

static void RemoteReplacementClosesCardBeforeStarting()
{
    for (int oldCard = 0; oldCard < 2; ++oldCard)
    {
        Reset();
        Cd.obj = oldCard == 0 ? 99 : 0;
        world.ownCardTask = oldCard == 1;
        C.cardOpenPending = true;
        world.reenterOnCardClose = true;
        RequestContractStart(0);
        UpdatePendingContractStart();
        Check(pendingContractStart.player == kPlayer && !pendingContractStart.attempting && world.cardCloses == 1 &&
            !Cd.obj && !world.ownCardTask && !C.cardOpenPending && !world.preflights && !world.clears,
            "remote replacement retires its old card and guards yielding cleanup against recursive startup");
        UpdatePendingContractStart();
        Check(!pendingContractStart.player && C.target == kNewTarget && C.cardOpenPending && world.spawns == 1 && world.cardCloses == 1,
            "the next playable frame creates one bounty and schedules the new card");
    }

    Reset();
    Cd.obj = 99;
    world.changePlayerOnCardClose = true;
    RequestContractStart(0);
    UpdatePendingContractStart();
    Check(!pendingContractStart.player && C.target == kOldTarget && !world.preflights && !world.clears,
        "player replacement during yielding card cleanup cancels startup without erasing the current hunt");
}

int main()
{
    PreparationFailureAndCacheExhaustion();
    PortraitFailureCleanupAndSuccess();
    InterruptedPreparationAndPublication();
    TransientEngineFailuresRetry();
    RemoteReplacementAfterCorpseProof();
    EarnedPaymentSettlesBeforeRetryingNewHunt();
    RemoteReplacementClosesCardBeforeStarting();
    std::printf("Contract start integration: %u checks passed.\n", checks);
}
