// run-card-texture-tests.ps1 extracts the actual production functions into tmp/tests.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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
} C;

static struct
{
    Object obj = 7;
    bool ownsObj = true;
    bool customApplied = false;
    ULONGLONG textureRefreshUntilMs = 0;
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
}
static bool TargetPhotoReady() { return C.photoTexture[0] && C.photoTextureValid; }
static Hash joaat(const char* text) { return std::strcmp(text, "test_portrait") == 0 ? 123u : 0u; }

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
    Check(texture == 123 && p2 == 0 && p3 == 0, "portrait binding arguments are preserved");
    ++binds;
    objectTextured = true;
}
}
// Download ownership itself is exercised by portrait_cache_tests; this suite controls
// readiness while exercising the actual production card-maintenance functions.
static bool LookupPhotoTexture(int cacheType, char (&out)[64])
{
    Check(cacheType == C.photoCacheType, "portrait cache ownership is preserved");
    ++cacheRequests;
    strcpy_s(out, "test_portrait");
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
}

static void TestHandoffToInspection()
{
    Reset();
    Cd.inHand = true;
    ApplyCardCustomTexture();
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
    Check(objectTextured && binds == 3,
        "same-handle native reset is repaired during startup maintenance");
    Check(cacheRequests == 1 && flipWrites == 0,
        "startup maintains the portrait without requiring examining or inHand");

    nowMs = Cd.textureRefreshUntilMs - 1;
    objectTextured = false;
    MaintainPortraitAndCard();
    Check(objectTextured && binds == 4, "delayed reset is repaired until the settling deadline");
    nowMs = Cd.textureRefreshUntilMs;
    MaintainPortraitAndCard();
    Check(binds == 4, "binding stops exactly at the settling deadline");
    nowMs += 10000;
    MaintainPortraitAndCard();
    Check(binds == 4, "settled frames do not repeatedly bind the texture");

    objectTextured = false;
    RefreshCardTextureAfterTransition();
    Check(objectTextured && binds == 5 && Cd.textureRefreshUntilMs > nowMs,
        "a later transition invalidates the previous successful attempt");
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
    nowMs = Cd.textureRefreshUntilMs + 1;
    textureAvailable = true;
    MaintainPortraitAndCard();
    Check(binds == 1 && objectTextured && objectVisible && C.photoTextureValid,
        "recovered portrait is bound and shown even after the old settling window expired");
    Check(Cd.textureRefreshUntilMs == nowMs + Card::kTextureSettleMs,
        "residency recovery starts a fresh bounded settling window");
    nowMs = Cd.textureRefreshUntilMs;
    MaintainPortraitAndCard();
    Check(binds == 1, "recovery settling expires without an unbounded retry loop");

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
    nowMs = Cd.textureRefreshUntilMs + 1;
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
    ++nowMs;
    MaintainPortraitAndCard();
    Check(binds == 1, "deletion during settling does not bind a stale handle");

    Cd.examining = true;
    MaintainPortraitAndCard();
    Check(flipWrites == 1, "maintenance preserves the live inspector flip flag");
    inspectorAlive = false;
    MaintainPortraitAndCard();
    Check(flipWrites == 1, "deleted inspectors do not receive blackboard writes");
}

int main()
{
    Check(Card::kTextureSettleMs > 0, "production settling interval is positive");
    TestHandoffToInspection();
    TestDelayedReadiness();
    TestResidencyLossAfterSuccessfulBinding();
    TestReadinessGate();
    TestMissingObjectsAndInspector();
    std::printf("All %u card texture checks passed (actual production functions).\n", checks);
}
