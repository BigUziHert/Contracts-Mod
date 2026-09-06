// run-card-texture-tests.ps1 extracts the actual production functions into tmp/tests.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using DWORD = std::uint32_t;
using ULONGLONG = std::uint64_t;
using Hash = std::uint32_t;
using Object = int;
using Ped = int;

namespace Card
{
constexpr int kPhotoSlot = 0;
constexpr const char* kFlipBlackboard = "GENERIC_DOCUMENT_FLIP_AVAILABLE";
}

static struct
{
    char photoTexture[64] = "test_portrait";
    bool photoTextureValid = true;
    int photoCacheType = 1;
    char photoLookupName[64] = "test_portrait";
} C;

static struct
{
    Object obj = 7;
    bool ownsObj = true;
    bool customApplied = false;
    ULONGLONG textureRefreshStartedMs = 0;
    unsigned textureBindIndex = 0;
    bool examining = false;
    bool inHand = false;
    Ped inspectingPed = 88;
} Cd;

static ULONGLONG nowMs = 1000;
static bool objectAlive = true;
static bool inspectorAlive = true;
static bool objectTextured = false;
static bool objectVisible = true;
static bool textureAvailable = true;
static bool playerAvailable = true;
static ULONGLONG textureAvailableAtMs = 0;
static unsigned binds = 0;
static unsigned cacheRequests = 0;
static unsigned visibilityWrites = 0;
static unsigned waitCalls = 0;
static unsigned flipWrites = 0;
static unsigned checks = 0;
static unsigned photoTestBindAttempts = 0;
static unsigned photoTestBindingTransitions = 0;
static const char* publishedName = "test_portrait";
static Hash expectedTexture = 123;
static std::vector<ULONGLONG> bindingTimes;
static const char* lastPhotoStage = "none";
static bool probeOpenSucceeds = true;
static bool probeTaskRunning = false;
static ULONGLONG probePutAwayAtMs = 0;
static ULONGLONG probeObjectGoneAtMs = 0;
static ULONGLONG probeTaskEndsAtMs = 0;
static ULONGLONG probePauseUntilMs = 0;
static ULONGLONG probeCancelAtMs = 0;
static unsigned probeUpdates = 0;
static unsigned probeDestroys = 0;
struct ProbeLog { std::string phase; bool success; Object original; bool owned; };
static std::vector<ProbeLog> probeLogs;

static void Check(bool condition, const char* description)
{
    ++checks;
    if (!condition)
    {
        std::fprintf(stderr, "FAILED: %s\n", description);
        std::exit(EXIT_FAILURE);
    }
}

static ULONGLONG RuntimeNowMs() { return nowMs; }
static ULONGLONG GetTickCount64() { return nowMs; }
static bool PlayerAvailable() { return playerAvailable; }
static void MaintainOwnedPedCleanup() {}
static void WAIT(DWORD milliseconds)
{
    Check(milliseconds == 0, "readiness polling yields one game frame");
    ++waitCalls;
    nowMs += 10;
    if (textureAvailableAtMs && nowMs >= textureAvailableAtMs) textureAvailable = true;
    if (probeObjectGoneAtMs && nowMs >= probeObjectGoneAtMs) objectAlive = false;
    if (probeTaskEndsAtMs && nowMs >= probeTaskEndsAtMs) probeTaskRunning = false;
    if (probeCancelAtMs && nowMs >= probeCancelAtMs) playerAvailable = false;
}
static bool TargetPhotoReady() { return C.photoTexture[0] && C.photoTextureValid; }
static Hash joaat(const char* text) { return std::strcmp(text, "test_portrait") == 0 ? 123u : 456u; }

namespace ENTITY
{
static bool DOES_ENTITY_EXIST(int entity)
{
    return (entity == 7 && objectAlive) || (entity == 88 && inspectorAlive);
}
static void SET_ENTITY_VISIBLE(Object object, bool visible)
{
    Check(object == 7 && objectAlive && Cd.ownsObj, "visibility changes only affect the live owned card");
    ++visibilityWrites;
    objectVisible = visible;
}
}
namespace OBJECT
{
static void SET_CUSTOM_TEXTURES_ON_OBJECT(Object object, Hash texture, int p2, int p3)
{
    Check(object == 7 && objectAlive, "only a live tracked card is bound");
    Check(texture == expectedTexture && p2 == 0 && p3 == 0, "portrait binding arguments are preserved");
    ++binds;
    bindingTimes.push_back(nowMs);
    objectTextured = true;
}
}
// Download ownership itself is exercised by portrait_cache_tests; this suite controls
// readiness while exercising the actual production card-maintenance functions.
static bool LookupPhotoTexture(int cacheType, char (&out)[64])
{
    Check(cacheType == C.photoCacheType, "portrait cache ownership is preserved");
    ++cacheRequests;
    strcpy_s(out, publishedName);
    return textureAvailable;
}
namespace PED
{
static void _SET_PED_BLACKBOARD_BOOL(Ped ped, const char* key, bool value, int duration)
{
    Check(ped == 88 && inspectorAlive && std::strcmp(key, Card::kFlipBlackboard) == 0 &&
        value && duration == -1, "flip maintenance addresses the tracked inspector");
    ++flipWrites;
}
}
namespace HUD
{
static bool IS_PAUSE_MENU_ACTIVE() { return nowMs < probePauseUntilMs; }
}
namespace TASK
{
static bool IS_PED_RUNNING_TASK_ITEM_INTERACTION(Ped ped)
{
    Check(ped == 88, "retirement checks the original inspector even after card state resets");
    return probeTaskRunning;
}
}
// This suite exercises the real probe's polling and retirement conditions. OpenCard/UpdateCard
// are boundary stubs so material maintenance remains covered without reproducing native tasks.
static bool OpenCard()
{
    if (!probeOpenSucceeds || !PlayerAvailable()) return false;
    Cd.obj = 7;
    Cd.ownsObj = true;
    Cd.examining = true;
    Cd.inspectingPed = 88;
    return true;
}
static void UpdateCard()
{
    Check(!HUD::IS_PAUSE_MENU_ACTIVE(), "the inspection probe does not update card input while paused");
    ++probeUpdates;
    if (probePutAwayAtMs && nowMs >= probePutAwayAtMs)
    {
        Cd.obj = 0;
        Cd.examining = false;
        Cd.inspectingPed = 0;
        Cd.ownsObj = false;
    }
}
static void DestroyCardObject(bool cancelInspection)
{
    Check(cancelInspection, "probe exits request cancellation of their own inspection");
    ++probeDestroys;
    Cd.obj = 0;
    Cd.examining = false;
    Cd.inspectingPed = 0;
    Cd.ownsObj = false;
}
static void DisplaySubtitle(const char* message) { Check(message && *message, "inspection instructions are present"); }
static void LogPhotoCacheTest(const char* phase, bool success, ULONGLONG started,
    unsigned captures, const char*, const char* observedName, Object original = 0, bool owned = false)
{
    Check(started <= nowMs && captures == 1 && std::strcmp(observedName, "test_portrait") == 0,
        "card probe logs retain the initial capture and its observed portrait name");
    probeLogs.push_back({ phase, success, original, owned });
}

#ifdef CARD_TEXTURE_OLD_ONESHOT
#include "card_texture_old_under_test.h"
#else
#include "card_texture_under_test.h"
#endif

static void Reset()
{
    C = {};
    Cd = {};
    nowMs = 1000;
    objectAlive = inspectorAlive = true;
    objectTextured = false;
    objectVisible = textureAvailable = playerAvailable = true;
    textureAvailableAtMs = 0;
    binds = cacheRequests = flipWrites = visibilityWrites = waitCalls = 0;
    photoTestBindAttempts = photoTestBindingTransitions = 0;
    publishedName = "test_portrait";
    expectedTexture = 123;
    bindingTimes.clear();
    lastPhotoStage = "none";
    probeOpenSucceeds = true;
    probeTaskRunning = false;
    probePutAwayAtMs = probeObjectGoneAtMs = probeTaskEndsAtMs = probePauseUntilMs = probeCancelAtMs = 0;
    probeUpdates = probeDestroys = 0;
    probeLogs.clear();
}

static void TestHandoffToInspection()
{
    Reset();
    Cd.inHand = true;
    RefreshCardTextureAfterTransition();
    Check(binds == 1 && objectTextured && Cd.customApplied, "handoff card receives its first binding");

    Cd.inHand = false;
    Cd.examining = false; // The native task has not yet reported its primary item.
    nowMs += 100;
    objectTextured = false; // Native inspection initializes the reused object's material.
    RefreshCardTextureAfterTransition();
    Check(Cd.obj == 7 && binds == 2 && objectTextured, "transition repairs the same handoff object");

    ++nowMs;
    objectTextured = false; // A later startup frame resets it again.
    MaintainPortraitAndCard();
    Check(!objectTextured && binds == 2, "early startup polls wait for the scheduled retry");
    nowMs += 98;
    MaintainPortraitAndCard();
    Check(!objectTextured && binds == 2, "retry is not issued before its scheduled time");
    ++nowMs;
    MaintainPortraitAndCard();
    Check(objectTextured && binds == 3,
        "same-handle native reset is repaired during startup maintenance");
    Check(cacheRequests == 3 && flipWrites == 0,
        "startup maintains the portrait without requiring examining or inHand");

    nowMs = Cd.textureRefreshStartedMs + Card::kTextureSettleMs;
    objectTextured = false;
    MaintainPortraitAndCard();
    Check(objectTextured && binds == 4, "final scheduled retry repairs a reset at the settling deadline");
    MaintainPortraitAndCard();
    Check(binds == 4, "repeated callers at the deadline cannot issue duplicate binds");
    nowMs += 10000;
    MaintainPortraitAndCard();
    Check(binds == 4, "settled frames do not repeatedly bind the texture");

    objectTextured = false;
    RefreshCardTextureAfterTransition();
    Check(objectTextured && binds == 5 && Cd.textureRefreshStartedMs == nowMs,
        "a later transition invalidates the previous successful attempt");
    Check(photoTestBindingTransitions == 3 && photoTestBindAttempts == binds,
        "diagnostics count transition restarts separately from actual native binds");
}

static void TestScheduleAcrossFrameRates()
{
    const unsigned frameDurations[] = { 1, 10, 17, 100, 250, 1000 };
    for (unsigned frameMs : frameDurations)
    {
        Reset();
        const ULONGLONG started = nowMs;
        RefreshCardTextureAfterTransition();
        for (unsigned elapsed = 0; elapsed <= 3000; elapsed += frameMs)
        {
            nowMs = started + elapsed;
            const unsigned before = binds;
            MaintainPortraitAndCard();
            const unsigned after = binds;
            ApplyCardCustomTexture();
            MaintainPortraitAndCard();
            Check(after - before <= 1 && binds == after,
                "multiple maintenance and draw callers share one attempt per scheduled frame");
        }
        Check(binds <= 5 && photoTestBindingTransitions == 1,
            "a transition never exceeds five binds regardless of polling frequency");
        Check(binds == (frameMs == 1000 ? 3u : 5u),
            "normal frames reach every retry while slow frames skip missed times");
        if (frameMs == 10)
        {
            const std::vector<ULONGLONG> expected = { started, started + 100, started + 300,
                started + 750, started + 1500 };
            Check(bindingTimes == expected, "binding follows the requested five retry times exactly");
        }
    }
}

static void TestLateFrameAndFlipTransition()
{
    Reset();
    RefreshCardTextureAfterTransition();
    nowMs += 900;
    objectTextured = false;
    MaintainPortraitAndCard();
    Check(binds == 2 && objectTextured, "a late frame repairs once instead of replaying three missed retries");
    for (unsigned i = 0; i < 10; ++i) ApplyCardCustomTexture();
    Check(binds == 2, "missed retries cannot burst across callers in the same late frame");
    nowMs += 800; // Jump over the final deadline as well.
    objectTextured = false;
    MaintainPortraitAndCard();
    Check(binds == 3 && objectTextured, "a frame past the deadline still performs the final due retry");
    nowMs += 5000;
    MaintainPortraitAndCard();
    Check(binds == 3, "an exhausted schedule stays silent on later frames");

    Cd.examining = true;
    objectTextured = false; // The flip animation resets the same material.
    RefreshCardTextureAfterTransition();
    Check(binds == 4 && objectTextured && photoTestBindingTransitions == 2,
        "a flip transition starts a fresh schedule on the already inspected object");
    ApplyCardCustomTexture();
    Check(binds == 4, "the draw following a flip shares its immediate transition attempt");
}

static void TestDelayedReadiness()
{
    Reset();
    C.photoTextureValid = false;
    textureAvailable = false;
    RefreshCardTextureAfterTransition();
    MaintainPortraitAndCard();
    Check(binds == 0 && !Cd.customApplied && cacheRequests == 1 && !objectVisible,
        "unready portrait stays requested while its owned card is hidden");
    nowMs = Cd.textureRefreshStartedMs + Card::kTextureSettleMs + 1;
    textureAvailable = true;
    MaintainPortraitAndCard();
    Check(binds == 1 && objectTextured && objectVisible && C.photoTextureValid,
        "recovered portrait is bound and shown even after the old settling window expired");
    Check(Cd.textureRefreshStartedMs == nowMs && photoTestBindingTransitions == 2,
        "residency recovery starts a fresh shared retry schedule");
    nowMs += Card::kTextureSettleMs;
    MaintainPortraitAndCard();
    Check(binds == 2, "recovery performs a final retry when a frame jumps to its deadline");
    nowMs += 10000;
    MaintainPortraitAndCard();
    Check(binds == 2, "recovery settling expires without an unbounded retry loop");

    Reset();
    C.photoTexture[0] = '\0';
    RefreshCardTextureAfterTransition();
    MaintainPortraitAndCard();
    Check(binds == 0 && !Cd.customApplied && cacheRequests == 0 && !objectVisible,
        "an empty texture name is never requested or shown as a ready card");
}

static void TestResidencyLossAfterSuccessfulBinding()
{
    Reset();
    RefreshCardTextureAfterTransition();
    nowMs = Cd.textureRefreshStartedMs + Card::kTextureSettleMs + 1;
    textureAvailable = objectTextured = false;
    MaintainPortraitAndCard();
    Check(!C.photoTextureValid && !objectVisible && binds == 1 && cacheRequests == 1,
        "lost residency hides a previously settled card without losing its accepted name");
    MaintainPortraitAndCard();
    Check(cacheRequests == 2 && std::strcmp(C.photoTexture, "test_portrait") == 0,
        "invalid residency keeps requesting the same accepted slot");
    textureAvailable = true;
    MaintainPortraitAndCard();
    Check(C.photoTextureValid && objectTextured && objectVisible && binds == 2,
        "false-to-true residency invalidates an old applied latch and restores the card");

    Cd.ownsObj = false;
    textureAvailable = false;
    const unsigned previousVisibilityWrites = visibilityWrites;
    MaintainPortraitAndCard();
    Check(visibilityWrites == previousVisibilityWrites && objectVisible,
        "residency loss never hides a foreign game-owned prop");
}

static void TestChangedAcceptedName()
{
    Reset();
    RefreshCardTextureAfterTransition();
    nowMs += 2000;
    MaintainPortraitAndCard(); // Exhaust the previous portrait's schedule.
    Check(binds == 2, "old accepted portrait has finished its bounded retries");
    publishedName = "replacement_portrait";
    expectedTexture = 456;
    objectTextured = false;
    MaintainPortraitAndCard();
    Check(std::strcmp(C.photoTexture, publishedName) == 0 && objectTextured && binds == 3 &&
        photoTestBindingTransitions == 2,
        "a changed accepted name replaces the material and restarts the shared schedule");
    MaintainPortraitAndCard();
    Check(binds == 3 && photoTestBindingTransitions == 2,
        "a stable accepted replacement neither restarts nor duplicates the new schedule");
    nowMs += 100;
    objectTextured = false;
    MaintainPortraitAndCard();
    Check(objectTextured && binds == 4, "the replacement portrait retains delayed-reset recovery");
}

static void TestReadinessGate()
{
    Reset();
    C.photoTexture[0] = '\0';
    Check(!EnsureTargetPhotoReady() && cacheRequests == 0 && waitCalls == 0,
        "readiness gate rejects a portrait that was never accepted without polling");

    Reset();
    textureAvailable = false; // The remembered flag is stale until maintenance rechecks it.
    textureAvailableAtMs = nowMs + 30;
    Check(EnsureTargetPhotoReady() && waitCalls == 3 && C.photoTextureValid && objectVisible && objectTextured,
        "readiness gate revalidates stale readiness and waits for residency recovery");

    Reset();
    textureAvailable = false;
    const ULONGLONG startedMs = nowMs;
    Check(!EnsureTargetPhotoReady() && !objectVisible && binds == 0,
        "readiness gate fails cleanly while an unavailable portrait remains hidden");
    Check(nowMs - startedMs >= Card::kPhotoNameMs && nowMs - startedMs < Card::kPhotoNameMs + 10,
        "readiness gate stops at the production timeout");

    Reset();
    playerAvailable = false;
    Check(!EnsureTargetPhotoReady() && cacheRequests == 0 && waitCalls == 0,
        "player cancellation stops readiness polling immediately");
}

static void TestMissingObjectsAndInspector()
{
    Reset();
    Cd.obj = 0;
    RefreshCardTextureAfterTransition();
    MaintainPortraitAndCard();
    Check(binds == 0 && !Cd.customApplied, "missing card handles are never bound");
    Cd.obj = 7;
    objectAlive = false;
    RefreshCardTextureAfterTransition();
    MaintainPortraitAndCard();
    Check(binds == 0 && !Cd.customApplied, "deleted card handles are never bound");
    objectAlive = true;
    RefreshCardTextureAfterTransition();
    Check(binds == 1, "a new live card transition can bind after a missing object");
    objectAlive = false;
    nowMs += 100; // A retry is due, but its original object has disappeared.
    MaintainPortraitAndCard();
    Check(binds == 1, "deletion during settling does not bind a stale handle");

    Cd.examining = true;
    MaintainPortraitAndCard();
    Check(flipWrites == 1, "maintenance preserves the live inspector flip flag");
    inspectorAlive = false;
    MaintainPortraitAndCard();
    Check(flipWrites == 1, "deleted inspectors do not receive blackboard writes");
}

static void TestInspectionProbeRetirement()
{
    Reset();
    probePutAwayAtMs = nowMs + 30;
    probeObjectGoneAtMs = nowMs + 50;
    probeTaskEndsAtMs = nowMs + 80;
    probeTaskRunning = true;
    const ULONGLONG started = nowMs;
    Check(ProbePhotoCard(started, 1) && nowMs == started + 80 && probeDestroys == 1,
        "inspection waits for both the original object and original task after the tracked handle clears");
    Check(probeLogs.size() == 3 && probeLogs[0].phase == "card_opened" &&
        probeLogs[1].phase == "card_put_away" && probeLogs[2].phase == "card_removed",
        "inspection logs opening, put-away and confirmed retirement in order");
    for (const ProbeLog& entry : probeLogs)
        Check(entry.success && entry.original == 7 && entry.owned,
            "logs preserve original card identity and ownership across Cd reset");

    Reset();
    probePutAwayAtMs = nowMs + 10;
    probeTaskEndsAtMs = nowMs + 20;
    probeTaskRunning = true;
    const ULONGLONG ghostStarted = nowMs;
    Check(!ProbePhotoCard(ghostStarted, 1) && !Cd.obj && objectAlive &&
        nowMs == ghostStarted + 10 + 1500,
        "a cleared tracked handle cannot hide an original object that outlives retirement timeout");
    Check(probeLogs.back().phase == "card_removed" && !probeLogs.back().success,
        "unconfirmed object retirement is reported as a failed card probe");

    Reset();
    probePutAwayAtMs = nowMs + 10;
    probeObjectGoneAtMs = nowMs + 20;
    probeTaskRunning = true;
    Check(!ProbePhotoCard(nowMs, 1) && !objectAlive && probeTaskRunning,
        "object deletion alone cannot establish retirement while the original inspector task remains");
}

static void TestInspectionProbeStopping()
{
    Reset();
    probeOpenSucceeds = false;
    Check(!ProbePhotoCard(nowMs, 1) && probeDestroys == 1 && !probeUpdates && !waitCalls &&
        probeLogs.size() == 1 && !probeLogs[0].success,
        "failed card opening stops before inspection polling and still requests cleanup");

    Reset();
    probeObjectGoneAtMs = nowMs + 60000;
    const ULONGLONG started = nowMs;
    Check(!ProbePhotoCard(started, 1) && nowMs == started + 60000 && probeDestroys == 1,
        "a user who never puts the card away reaches the bounded inspection timeout");
    Check(!probeLogs[1].success && probeLogs[2].success,
        "later object retirement does not convert a timed-out inspection into success");

    Reset();
    probeCancelAtMs = nowMs + 20;
    Check(!ProbePhotoCard(nowMs, 1) && probeDestroys == 1 && !playerAvailable &&
        !probeLogs[1].success && !probeLogs[2].success,
        "player interruption stops polling and cancels the owned inspection");

    Reset();
    probePauseUntilMs = nowMs + 40;
    probePutAwayAtMs = nowMs + 10;
    probeObjectGoneAtMs = nowMs + 50;
    Check(ProbePhotoCard(nowMs, 1) && probeUpdates == 1,
        "inspection polling resumes after pause before detecting put-away");
}

int main()
{
    Check(Card::kTextureSettleMs == 1500 && Card::kTextureBindCount == 5,
        "production binding policy keeps the requested five attempts through 1500 milliseconds");
    TestHandoffToInspection();
    TestScheduleAcrossFrameRates();
    TestLateFrameAndFlipTransition();
    TestDelayedReadiness();
    TestResidencyLossAfterSuccessfulBinding();
    TestChangedAcceptedName();
    TestReadinessGate();
    TestMissingObjectsAndInspector();
    TestInspectionProbeRetirement();
    TestInspectionProbeStopping();
    std::printf("All %u card texture checks passed (actual production functions).\n", checks);
}
