// run-card-texture-tests.ps1 extracts the actual production functions into tmp/tests.
#include <cstdint>
#include <cmath>
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
struct Vector3 { float x, y, z; };

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
static bool photoTestPlainCard = false;
static const char* publishedName = "test_portrait";
static Hash expectedTexture = 123;
static std::vector<ULONGLONG> bindingTimes;
static const char* lastPhotoStage = "none";
static bool probeOpenSucceeds = true;
static bool probeCanStart = true;
static bool probeCreateSucceeds = true;
static bool probeTaskRunning = false;
static Ped pedMe = 88;
static ULONGLONG probePutAwayAtMs = 0;
static ULONGLONG probeObjectGoneAtMs = 0;
static ULONGLONG probeTaskStartsAtMs = 0;
static ULONGLONG probeTaskEndsAtMs = 0;
static ULONGLONG probePauseUntilMs = 0;
static ULONGLONG probePauseStartsAtMs = 0;
static ULONGLONG probeFadeStartsAtMs = 0;
static bool probeOnScreen = true;
static ULONGLONG probeOnScreenAtMs = 0;
static ULONGLONG probeOffScreenAtMs = 0;
static ULONGLONG probeCancelAtMs = 0;
static unsigned probeUpdates = 0;
static unsigned probeDestroys = 0;
static unsigned probeCreates = 0;
static unsigned probeFreezes = 0;
static bool probeDeleteSucceeds = true;
static unsigned probeDeleteSucceedsOnCall = 1;
static ULONGLONG probeDeleteDelayMs = 0;
static std::vector<ULONGLONG> probeDeleteTimes;
static Vector3 probeCamera = { 10.0f, 20.0f, 30.0f };
static Vector3 probeCameraRotation = {};
static Vector3 probePlacedAt = {};
static Vector3 probePlacedRotation = {};
static unsigned probePositions = 0;
static unsigned probeRotations = 0;
static std::vector<bool> probeCancellationRequests;
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
static void SetRuntimePaused(bool) {} // Runtime pause accounting is covered by spawn_tests.
static ULONGLONG GetTickCount64() { return nowMs; }
static bool PlayerAvailable() { return playerAvailable; }
static bool CanStartInteraction() { return probeCanStart && PlayerAvailable() && !probeTaskRunning; }
static void MaintainOwnedPedCleanup() {}
static void WAIT(DWORD milliseconds)
{
    Check(milliseconds == 0, "readiness polling yields one game frame");
    ++waitCalls;
    nowMs += 10;
    if (textureAvailableAtMs && nowMs >= textureAvailableAtMs) textureAvailable = true;
    if (probeObjectGoneAtMs && nowMs >= probeObjectGoneAtMs) objectAlive = false;
    if (probeTaskStartsAtMs && nowMs >= probeTaskStartsAtMs) probeTaskRunning = true;
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
static void FREEZE_ENTITY_POSITION(Object object, bool frozen)
{
    Check(object == 7 && objectAlive && frozen, "object probe freezes its original live card");
    ++probeFreezes;
}
static bool IS_ENTITY_ON_SCREEN(Object object)
{
    Check(object == 7, "screen visibility checks the original card");
    return objectAlive && objectVisible && probeOnScreen && nowMs >= probeOnScreenAtMs &&
        (!probeOffScreenAtMs || nowMs < probeOffScreenAtMs);
}
static void SET_ENTITY_COORDS(Object object, float x, float y, float z, bool p4, bool p5, bool p6, bool p7)
{
    Check(object == 7 && objectAlive && !p4 && !p5 && !p6 && !p7,
        "camera placement changes only the original card with the requested coordinate flags");
    probePlacedAt = { x, y, z };
    ++probePositions;
}
static void SET_ENTITY_ROTATION(Object object, float x, float y, float z, int order, bool p5)
{
    Check(object == 7 && objectAlive && order == 2 && p5,
        "object orientation preserves the camera rotation order and native flag");
    probePlacedRotation = { x, y, z };
    ++probeRotations;
}
}
namespace OBJECT
{
static void DELETE_OBJECT(Object* object)
{
    Check(object && object != &Cd.obj && *object == 7 && Cd.obj == 7 && Cd.ownsObj,
        "deletion uses a temporary original handle while Cd retains ownership");
    probeDeleteTimes.push_back(nowMs);
    *object = 0; // Model the native clearing its argument before existence confirms deletion.
    Check(Cd.obj == 7 && Cd.ownsObj, "native argument clearing cannot lose the tracked card");
    if (objectAlive && probeDeleteSucceeds && probeDeleteTimes.size() >= probeDeleteSucceedsOnCall &&
        !probeObjectGoneAtMs)
    {
        if (probeDeleteDelayMs) probeObjectGoneAtMs = nowMs + probeDeleteDelayMs;
        else objectAlive = false;
    }
}
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
static bool IS_PAUSE_MENU_ACTIVE()
{
    return nowMs < probePauseUntilMs || (probePauseStartsAtMs && nowMs >= probePauseStartsAtMs);
}
}
namespace CAMERA
{
static bool IS_SCREEN_FADED_OUT() { return false; }
static Vector3 GET_GAMEPLAY_CAM_COORD() { return probeCamera; }
static Vector3 GET_GAMEPLAY_CAM_ROT(int order)
{
    Check(order == 2, "object probe reads camera rotation using the placement rotation order");
    return probeCameraRotation;
}
static bool IS_SCREEN_FADED_IN() { return !probeFadeStartsAtMs || nowMs < probeFadeStartsAtMs; }
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
static void RefreshCardTextureAfterTransition();
static unsigned photoSlotMarks = 0;
static void MarkPhotoSlotBound() { ++photoSlotMarks; } // the portrait cache suite covers the real slot bookkeeping
static bool CreateCardObject()
{
    ++probeCreates;
    if (!probeCreateSucceeds) return false;
    Cd.obj = 7;
    Cd.ownsObj = true;
    objectAlive = true;
    RefreshCardTextureAfterTransition();
    return true;
}
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
static void DestroyCardObject(bool cancelInspection = false)
{
    probeCancellationRequests.push_back(cancelInspection);
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
#elif defined(CARD_TEXTURE_NO_PLAIN_GUARD)
#include "card_texture_no_plain_guard_under_test.h"
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
    photoTestPlainCard = false;
    publishedName = "test_portrait";
    expectedTexture = 123;
    bindingTimes.clear();
    lastPhotoStage = "none";
    probeOpenSucceeds = true;
    probeCanStart = probeCreateSucceeds = true;
    probeTaskRunning = false;
    probePutAwayAtMs = probeObjectGoneAtMs = probeTaskStartsAtMs = probeTaskEndsAtMs = probePauseUntilMs = probeCancelAtMs = 0;
    probeUpdates = probeDestroys = probeCreates = probeFreezes = 0;
    probeDeleteSucceeds = true;
    probeDeleteSucceedsOnCall = 1;
    probeDeleteDelayMs = 0;
    probeDeleteTimes.clear();
    probePauseStartsAtMs = probeFadeStartsAtMs = probeOnScreenAtMs = probeOffScreenAtMs = 0;
    probeOnScreen = true;
    probeCamera = { 10.0f, 20.0f, 30.0f };
    probeCameraRotation = probePlacedAt = probePlacedRotation = {};
    probePositions = probeRotations = 0;
    probeCancellationRequests.clear();
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

static void TestPlainCardDiagnostic()
{
    Reset();
    photoTestPlainCard = true;
    Cd.examining = true;
    RefreshCardTextureAfterTransition();
    Check(binds == 0 && photoTestBindAttempts == 0 && !Cd.customApplied && !objectTextured,
        "plain-card transition suppresses material binding");

    for (unsigned elapsed : { 0u, 100u, 300u, 750u, 1500u, 10000u })
    {
        nowMs = 1000 + elapsed;
        ApplyCardCustomTexture();
        MaintainPortraitAndCard();
    }
    Check(binds == 0 && photoTestBindAttempts == 0 && !Cd.customApplied,
        "plain-card direct and scheduled maintenance paths never bind the material");
    Check(cacheRequests == 6 && visibilityWrites == 6 && flipWrites == 6 &&
        TargetPhotoReady() && objectVisible && std::strcmp(C.photoTexture, "test_portrait") == 0,
        "plain-card maintenance preserves accepted portrait polling, visibility and inspection");

    textureAvailable = false;
    MaintainPortraitAndCard();
    Check(cacheRequests == 7 && !TargetPhotoReady() && !objectVisible &&
        std::strcmp(C.photoTexture, "test_portrait") == 0 && binds == 0,
        "plain-card residency loss hides the card while retaining its accepted portrait name");

    publishedName = "replacement_portrait";
    expectedTexture = 456;
    textureAvailable = true;
    MaintainPortraitAndCard();
    Check(cacheRequests == 8 && TargetPhotoReady() && objectVisible &&
        std::strcmp(C.photoTexture, publishedName) == 0 && photoTestBindingTransitions == 2 && binds == 0,
        "plain-card residency recovery adopts the accepted replacement without material binding");
    Check(EnsureTargetPhotoReady() && cacheRequests == 9 && binds == 0,
        "plain-card readiness waits still poll the accepted cache without binding");

    photoTestPlainCard = false;
    RefreshCardTextureAfterTransition();
    Check(binds == 1 && photoTestBindAttempts == 1 && objectTextured && Cd.customApplied &&
        photoTestBindingTransitions == 3,
        "restoring textured mode and refreshing resumes binding the accepted replacement");
    MaintainPortraitAndCard();
    Check(binds == 1, "restored texture binding retains the shared immediate-attempt guard");
    nowMs += 100;
    objectTextured = false;
    MaintainPortraitAndCard();
    Check(binds == 2 && objectTextured,
        "restored textured mode resumes scheduled material-reset recovery");

    photoTestPlainCard = true;
    nowMs += 200;
    objectTextured = false;
    MaintainPortraitAndCard();
    Check(binds == 2 && !objectTextured && TargetPhotoReady() && objectVisible,
        "plain-card mode also suppresses a due retry from an existing textured schedule");
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
    Check(probeCancellationRequests == std::vector<bool>{ true },
        "inspection retirement still requests cancellation of its own task");
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

static void PrepareObjectProbe()
{
    Reset();
    Cd.obj = 0;
    Cd.ownsObj = false;
    probeDeleteDelayMs = 50;
}

static void TestObjectProbeLifetime()
{
    for (bool plain : { false, true })
    {
        PrepareObjectProbe();
        photoTestPlainCard = plain;
        const ULONGLONG started = nowMs;
        Check(ProbePhotoObject(started, 1) && nowMs == started + 2060,
            "object probe holds for two seconds then waits for actual deletion and one final frame");
        Check(probeCreates == 1 && probeFreezes == 1 && probeDestroys == 1 && !Cd.obj && !objectAlive &&
            probeDeleteTimes == std::vector<ULONGLONG>{ started + 2000 },
            "object probe retains ownership through temporary-handle deletion until retirement is confirmed");
        Check(probeCancellationRequests == std::vector<bool>{ false } && !probeTaskRunning && probeUpdates == 0,
            "object-only control never starts, updates or cancels an inspection task");
        Check(probeLogs.size() == 4 && probeLogs[0].phase == "object_created" &&
            probeLogs[1].phase == "object_on_screen" && probeLogs[2].phase == "object_held" &&
            probeLogs[3].phase == "object_removed",
            "object probe logs creation, screen visibility, completed hold and confirmed removal");
        for (const ProbeLog& entry : probeLogs)
            Check(entry.success && entry.original == 7 && entry.owned,
                "object logs preserve original identity and ownership after Cd reset");
        Check(cacheRequests >= 200 && visibilityWrites >= 200 && TargetPhotoReady(),
            "object hold keeps accepted portrait polling and owned-card visibility active");
        Check(binds == (plain ? 0u : Card::kTextureBindCount),
            "object-only controls distinguish an unbound card from the complete textured binding schedule");
        Check(probePositions == 1 && probeRotations == 1 && std::fabs(probePlacedAt.x - 10.0f) < 0.0001f &&
            std::fabs(probePlacedAt.y - 20.75f) < 0.0001f && std::fabs(probePlacedAt.z - 30.0f) < 0.0001f,
            "a level north-facing camera places the card three quarters of a metre directly ahead");
    }
}

static void TestObjectProbeGuards()
{
    PrepareObjectProbe();
    probeCanStart = false;
    Check(!ProbePhotoObject(nowMs, 1) && !probeCreates && !probeDestroys && probeLogs.empty(),
        "unavailable interactions reject object creation without mutating card state");

    PrepareObjectProbe();
    Cd.obj = 7;
    Check(!ProbePhotoObject(nowMs, 1) && Cd.obj == 7 && !probeCreates && !probeDestroys,
        "an existing card prevents the object probe from replacing or deleting it");

    PrepareObjectProbe();
    probeTaskRunning = true;
    Check(!ProbePhotoObject(nowMs, 1) && probeTaskRunning && !probeCreates && probeCancellationRequests.empty(),
        "an already-running foreign item task is left untouched");

    PrepareObjectProbe();
    probeCreateSucceeds = false;
    Check(!ProbePhotoObject(nowMs, 1) && probeCreates == 1 && probeDestroys == 1 &&
        !probeFreezes && !waitCalls && probeLogs.size() == 1 && !probeLogs[0].success,
        "failed object creation requests cleanup without freezing or entering the hold");
    Check(probeCancellationRequests == std::vector<bool>{ false },
        "failed object creation cannot cancel an unrelated inspection");
}

static void TestObjectProbeStopping()
{
    PrepareObjectProbe();
    probeTaskStartsAtMs = nowMs + 40;
    const ULONGLONG taskStarted = nowMs;
    Check(!ProbePhotoObject(taskStarted, 1) && nowMs == taskStarted + 100 && probeTaskRunning,
        "a foreign item task arriving during the hold stops the object control promptly");
    Check(probeCancellationRequests == std::vector<bool>{ false } && !probeLogs[2].success &&
        probeLogs[3].success && !objectAlive,
        "foreign-task interruption retires only the owned object without cancelling that task");

    PrepareObjectProbe();
    probeDeleteSucceeds = false;
    const ULONGLONG timeoutStarted = nowMs;
    Check(!ProbePhotoObject(timeoutStarted, 1) && nowMs == timeoutStarted + 3510 &&
        Cd.obj == 7 && Cd.ownsObj && objectAlive && probeLogs[2].success && !probeLogs[3].success,
        "retirement timeout preserves the original tracked ownership despite cleared native arguments");
    Check(probeDestroys == 0 && probeCancellationRequests.empty() && probeDeleteTimes.size() == 7,
        "object retirement retries are bounded and timeout never resets ownership or cancels tasks");
    for (std::size_t i = 0; i < probeDeleteTimes.size(); ++i)
        Check(probeDeleteTimes[i] == timeoutStarted + 2000 + i * 250,
            "pending deletion retries use the original object once per 250 milliseconds");

    PrepareObjectProbe();
    probeCancelAtMs = nowMs + 40;
    const ULONGLONG cancelledStarted = nowMs;
    Check(!ProbePhotoObject(cancelledStarted, 1) && nowMs == cancelledStarted + 50 && !playerAvailable &&
        probeDestroys == 0 && probeDeleteTimes.size() == 1 && Cd.obj == 7 && Cd.ownsObj &&
        !probeLogs[2].success && !probeLogs[3].success,
        "player interruption requests deletion and retains unconfirmed ownership without continuing the hold");

    PrepareObjectProbe();
    probeObjectGoneAtMs = nowMs + 30;
    const ULONGLONG vanishedStarted = nowMs;
    Check(!ProbePhotoObject(vanishedStarted, 1) && nowMs == vanishedStarted + 40 &&
        !probeLogs[2].success && probeLogs[3].success && probeDestroys == 1,
        "early object disappearance fails the hold even though retirement is already confirmed");
}

static void TestObjectProbePlacementAndVisibility()
{
    PrepareObjectProbe();
    probeCameraRotation = { 30.0f, 5.0f, 90.0f };
    probeOnScreenAtMs = nowMs + 100;
    probeOffScreenAtMs = nowMs + 200;
    Check(ProbePhotoObject(nowMs, 1) && probeLogs[1].success,
        "an object observed on screen for part of the hold satisfies the visibility requirement");
    Check(std::fabs(probePlacedAt.x - (10.0f - 0.649519f)) < 0.0001f &&
        std::fabs(probePlacedAt.y - 20.0f) < 0.0001f && std::fabs(probePlacedAt.z - 30.375f) < 0.0001f &&
        probePlacedRotation.x == 30.0f && probePlacedRotation.y == 5.0f && probePlacedRotation.z == 90.0f,
        "pitched east-facing camera placement preserves distance, height and the full camera orientation");

    PrepareObjectProbe();
    probeOnScreen = false;
    Check(!ProbePhotoObject(nowMs, 1) && !probeLogs[1].success && !probeLogs[2].success &&
        probeLogs[3].success && !Cd.obj,
        "an object never observed on screen fails the control but is still retired");

    for (bool paused : { false, true })
    {
        PrepareObjectProbe();
        if (paused) probePauseStartsAtMs = nowMs + 40;
        else probeFadeStartsAtMs = nowMs + 40;
        const ULONGLONG started = nowMs;
        Check(!ProbePhotoObject(started, 1) && nowMs == started + 100 &&
            !probeLogs[2].success && probeLogs[3].success && !objectAlive,
            "pause or screen fade stops the visual control and still confirms object retirement");
        Check(probeCancellationRequests == std::vector<bool>{ false },
            "pause and fade cleanup do not cancel item-interaction tasks");
    }
}

static void TestObjectProbeDeletionRetry()
{
    PrepareObjectProbe();
    probeDeleteSucceedsOnCall = 3;
    probeDeleteDelayMs = 20;
    const ULONGLONG started = nowMs;
    Check(ProbePhotoObject(started, 1) && nowMs == started + 2530 && !objectAlive && !Cd.obj &&
        probeDestroys == 1 && probeLogs[3].success,
        "a later successful deletion retry confirms retirement before tracked ownership resets");
    const std::vector<ULONGLONG> expected = { started + 2000, started + 2250, started + 2500 };
    Check(probeDeleteTimes == expected,
        "cleared deletion arguments never replace the original handle used by retries");
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
    TestPlainCardDiagnostic();
    TestMissingObjectsAndInspector();
    TestInspectionProbeRetirement();
    TestInspectionProbeStopping();
    TestObjectProbeLifetime();
    TestObjectProbeGuards();
    TestObjectProbeStopping();
    TestObjectProbePlacementAndVisibility();
    TestObjectProbeDeletionRetry();
    std::printf("All %u card texture checks passed (actual production functions).\n", checks);
}
