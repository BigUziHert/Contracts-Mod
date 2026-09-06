// Exercises production capture, owned-download, cleanup and maintenance functions.
// Native responses simulate ownership and asynchronous completion, not RDR2 rendering.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using DWORD = std::uint32_t;
using ULONGLONG = std::uint64_t;
using Hash = std::uint32_t;
using Ped = int;
using Object = int;
constexpr Ped kSubject = 77;
constexpr Object kCard = 7;
constexpr Ped kInspector = 88;
constexpr DWORD kFrameMs = 16;
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

struct Download
{
    int id;
    ULONGLONG readyAt;
    int finalStatus;
    std::string name;
    bool active = true;
    bool nameValid = true;
    unsigned releases = 0;
};

static struct World
{
    ULONGLONG nowMs = 1000;
    unsigned frame = 0;
    bool playerAvailable = true;
    unsigned cancelAtFrame = std::numeric_limits<unsigned>::max();
    bool cancelOnRequest = false;
    bool subjectExists = true;
    bool subjectMale = true;
    bool pedReady = true;
    unsigned pedReadyAtFrame = 0;
    bool generateSucceeds = true;
    bool writeSucceeds = true;
    bool commitReady = true;
    unsigned busyUntilFrame = 0;
    bool permanentlyBusy = false;
    unsigned previousUploadUntilFrame = 0;
    unsigned uploadFrames = 0;
    bool uploadNeverCompletes = false;
    bool activeUploadNeverCompletes = false;
    unsigned uploadCompleteFrame = 0;
    unsigned writeFrame = 0;
    bool writeQueued = false;
    bool captureActive = false;
    bool generatedDataValid = false;
    unsigned generatedFrame = 0;
    unsigned commitObservedFrame = std::numeric_limits<unsigned>::max();
    unsigned cleanupFrame = 0;
    ULONGLONG publicationDelayMs = 0;
    ULONGLONG publishAt = 0;
    unsigned previousCleanups = 0;
    unsigned generatedDataInvalidations = 0;
    unsigned busyChecks = 0;
    unsigned captureStarts = 0;
    unsigned captureCleanups = 0;
    unsigned cleanupInitializations = 0;
    unsigned generates = 0;
    unsigned writeCalls = 0;
    unsigned writes = 0;
    ULONGLONG previousCleanupCallMs = 0;
    ULONGLONG generateCallMs = 0;
    ULONGLONG writeCallMs = 0;
    ULONGLONG cleanupInitCallMs = 0;
    ULONGLONG cleanupFinishCallMs = 0;
    ULONGLONG requestCallMs = 0;
    ULONGLONG releaseCallMs = 0;
    unsigned requests = 0;
    unsigned releases = 0;
    unsigned unavailableRequests = 0;
    int unavailableResult = -1;
    unsigned failedDownloads = 0;
    ULONGLONG downloadDelayMs = 32;
    unsigned validityChecks = 0;
    unsigned binds = 0;
    Hash boundTexture = 0;
    bool cardVisible = true;
    unsigned flipWrites = 0;
    std::vector<Download> downloads;
    std::vector<ULONGLONG> requestTimes;
    std::vector<int> captureSlots; // slot written by each capture that reached its write
    char borrowedName[128] = "";
} world;

static struct CardState
{
    Object obj = 0;
    bool ownsObj = true;
    bool customApplied = false;
    ULONGLONG textureRefreshStartedMs = 0;
    unsigned textureBindIndex = 0;
    bool examining = false;
    Ped inspectingPed = kInspector;
} Cd;

static Download& FindDownload(int id)
{
    for (Download& download : world.downloads)
        if (download.id == id) return download;
    Check(false, "download operations use a returned positive handle");
    std::abort();
}
static unsigned ActiveDownloads()
{
    unsigned active = 0;
    for (const Download& download : world.downloads) if (download.active) ++active;
    return active;
}
static ULONGLONG GetTickCount64() { return world.nowMs; }
static ULONGLONG RuntimeNowMs() { return world.nowMs; }
static bool PlayerAvailable() { return world.playerAvailable; }
static void MaintainOwnedPedCleanup() {} // The independent spawn suite exercises ped ownership.
static void WAIT(DWORD delay)
{
    Check(delay == 0, "capture polling yields game frames");
    ++world.frame;
    world.nowMs += kFrameMs;
    if (world.frame >= world.cancelAtFrame) world.playerAvailable = false;
    Check(world.frame < 10000, "capture polling remains bounded");
}
static Hash joaat(const char* name)
{
    Hash hash = 2166136261u;
    while (*name) { hash ^= static_cast<unsigned char>(*name++); hash *= 16777619u; }
    return hash;
}

namespace ENTITY
{
static bool DOES_ENTITY_EXIST(int entity)
{
    return (entity == kSubject && world.subjectExists) || entity == kCard || entity == kInspector;
}
static void SET_ENTITY_VISIBLE(int entity, bool visible)
{
    Check(entity == kSubject || entity == kCard, "visibility only changes the tracked subject or card");
    if (entity == kCard) world.cardVisible = visible;
}
}
namespace PED
{
static bool IS_PED_MALE(Ped ped) { Check(ped == kSubject, "capture uses the supplied subject"); return world.subjectMale; }
static bool IS_PED_READY_TO_RENDER(Ped ped)
{
    Check(ped == kSubject, "readiness uses the supplied subject");
    return world.pedReady && world.frame >= world.pedReadyAtFrame;
}
static void FORCE_PED_MOTION_STATE(Ped ped, Hash, bool, int, bool)
{
    Check(ped == kSubject, "capture maintenance addresses the supplied subject");
}
static void _SET_PED_BLACKBOARD_BOOL(Ped ped, const char*, bool value, int duration)
{
    Check(ped == kInspector && value && duration == -1, "flip availability is retained for the inspector");
    ++world.flipWrites;
}
}
namespace GRAPHICS
{
static bool PEDSHOT_IS_AVAILABLE()
{
    ++world.busyChecks;
    return world.permanentlyBusy || world.frame < world.busyUntilFrame || world.captureActive;
}
static void _PEDSHOT_SET_PERSONA_PHOTO_TYPE(int type)
{
    Check(type == 1 && !world.captureActive, "each capture starts with the verified persona photo type");
    Check(ActiveDownloads() == 0, "previous downloads are released before the next capture starts");
    Check(!world.permanentlyBusy && world.frame >= world.busyUntilFrame &&
        world.frame >= world.previousUploadUntilFrame,
        "new capture setup waits for shared capture and prior upload availability");
    Check(world.previousCleanups == world.captureStarts + 1,
        "previous capture data is cleared once before configuring each new photo");
    world.writeQueued = false;
    world.commitObservedFrame = std::numeric_limits<unsigned>::max();
    world.captureActive = true;
    ++world.captureStarts;
}
static void _PEDSHOT_PREVIOUS_PERSONA_PHOTO_DATA_CLEANUP()
{
    // Model data loss if cleanup is moved after generation; successful pipeline tests reject that order.
    ++world.previousCleanups;
    world.nowMs += world.previousCleanupCallMs;
    if (world.generatedDataValid) ++world.generatedDataInvalidations;
    world.generatedDataValid = false;
}
static bool _PEDSHOT_GENERATE_PERSONA_PHOTO(const char* name, Ped ped, int slot)
{
    Check(world.captureActive && ped == kSubject && slot == 0, "capture generation retains its verified arguments");
    Check(std::strcmp(name, world.subjectMale ? "MINIGAME_PROFILE_PHOTO" : "MINIGAME_PROFILE_PHOTO_F") == 0,
        "male and female captures preserve their respective names");
    ++world.generates;
    world.nowMs += world.generateCallMs;
    world.generatedFrame = world.frame;
    world.generatedDataValid = world.generateSucceeds;
    return world.generateSucceeds;
}
static void _PEDSHOT_INIT_CLEANUP_DATA()
{
    Check(world.captureActive, "capture cleanup starts only for owned capture resources");
    ++world.cleanupInitializations;
    world.nowMs += world.cleanupInitCallMs;
}
static void _PEDSHOT_FINISH_CLEANUP_DATA()
{
    Check(world.captureActive && world.cleanupInitializations == world.captureCleanups + 1,
        "capture cleanup start and finish remain paired");
    world.captureActive = false;
    world.nowMs += world.cleanupFinishCallMs;
    world.generatedDataValid = false;
    world.cleanupFrame = world.frame;
    world.publishAt = world.nowMs + world.publicationDelayMs;
    ++world.captureCleanups;
}
}
namespace NETWORK
{
static bool _NETWORK_IS_PREVIOUS_UPLOAD_PENDING()
{
    if (!world.writeQueued) return world.frame < world.previousUploadUntilFrame;
    const bool pending = world.activeUploadNeverCompletes || world.frame < world.uploadCompleteFrame;
    if (!pending && world.commitObservedFrame == std::numeric_limits<unsigned>::max())
    {
        Check(world.frame > world.writeFrame, "upload completion is observed on a later frame than write acceptance");
        world.commitObservedFrame = world.frame;
    }
    return pending;
}
static bool _0xCC4E72C339461ED1() { return world.commitReady; }
static bool _NETWORK_PERSONA_PHOTO_WRITE_LOCAL(const char*, int slot, int format, int cacheType)
{
    // The engine's local persona-photo cache has 32 slots (one per MP player index).
    Check(world.captureActive && slot >= 0 && slot < 32 && format == 1 && cacheType == 2,
        "write arguments address a valid local slot and preserve the verified cache type");
    if (world.captureSlots.size() < world.captureStarts)
    {
        // An inspected card can keep the previous slot's texture alive; the next capture must not rewrite it.
        Check(world.captureSlots.empty() || slot != world.captureSlots.back(),
            "a capture never rewrites the slot written by the capture before it");
        world.captureSlots.push_back(slot);
    }
    Check(slot == world.captureSlots.back(), "every write poll of one capture addresses the same slot");
    Check(ActiveDownloads() == 0, "a retained download cannot survive into the next capture's write");
    Check(world.frame > world.generatedFrame, "write starts on a later frame than generation");
    ++world.writeCalls;
    world.nowMs += world.writeCallMs;
    const bool succeeded = world.writeSucceeds && world.generatedDataValid;
    if (succeeded)
    {
        ++world.writes;
        world.writeQueued = true;
        world.writeFrame = world.frame;
        world.activeUploadNeverCompletes = world.uploadNeverCompletes;
        world.uploadCompleteFrame = world.frame + world.uploadFrames;
    }
    return succeeded;
}
static int _LOCAL_PLAYER_PEDSHOT_TEXTURE_DOWNLOAD_REQUEST(int slot, int cacheType)
{
    Check(!world.captureSlots.empty() && slot == world.captureSlots.back() && cacheType == 2,
        "download uses the exact slot and cache type written");
    Check(world.writes > 0 && !world.captureActive, "download begins after capture commit and cleanup");
    Check(world.frame > world.cleanupFrame, "download starts on a later frame than capture cleanup");
    Check(ActiveDownloads() == 0, "only one download is owned at a time");
    ++world.requests;
    world.requestTimes.push_back(world.nowMs);
    world.nowMs += world.requestCallMs;
    if (world.cancelOnRequest) world.playerAvailable = false;
    if (world.nowMs < world.publishAt) return -1;
    if (world.unavailableRequests)
    {
        --world.unavailableRequests;
        return world.unavailableResult;
    }
    const int id = 101 + static_cast<int>(world.downloads.size());
    const int status = world.failedDownloads ? 2 : 0;
    if (world.failedDownloads) --world.failedDownloads;
    world.downloads.push_back(Download{ id, world.nowMs + world.downloadDelayMs, status,
        "portrait_slot" + std::to_string(slot) + "_" + std::to_string(world.writes) + "_" + std::to_string(id) });
    return id;
}
static int GET_STATUS_OF_TEXTURE_DOWNLOAD(int id)
{
    Download& download = FindDownload(id);
    Check(download.active, "released handles are never polled");
    return world.nowMs < download.readyAt ? 1 : download.finalStatus;
}
static const char* TEXTURE_DOWNLOAD_GET_NAME(int id)
{
    Download& download = FindDownload(id);
    Check(download.active && world.nowMs >= download.readyAt && download.finalStatus == 0,
        "the texture name is read only after successful download completion");
    strcpy_s(world.borrowedName, download.name.c_str());
    return world.borrowedName;
}
static bool _TEXTURE_DOWNLOAD_TEXTURE_NAME_IS_VALID(const char* name)
{
    Check(name != world.borrowedName, "the borrowed name is copied before another native is invoked");
    const std::string copiedName(name);
    strcpy_s(world.borrowedName, "native scratch storage changed");
    ++world.validityChecks;
    for (const Download& download : world.downloads)
        if (download.active && download.name == copiedName) return download.nameValid;
    return false;
}
static void TEXTURE_DOWNLOAD_RELEASE(int id)
{
    Check(id > 0, "sentinel handles are never released");
    Download& download = FindDownload(id);
    Check(download.active && download.releases == 0, "each owned download is released exactly once");
    download.active = false;
    ++download.releases;
    ++world.releases;
    world.nowMs += world.releaseCallMs;
}
}
namespace OBJECT
{
static void SET_CUSTOM_TEXTURES_ON_OBJECT(Object obj, Hash texture, int p2, int p3)
{
    Check(obj == kCard && p2 == 0 && p3 == 0, "material updates target only the tracked contract card");
    ++world.binds;
    world.boundTexture = texture;
}
}

static void MaintainPortraitAndCard();
static void ApplyCardCustomTexture();
static void RefreshCardTextureAfterTransition();
static bool TargetPhotoReady();
static void ReleaseTargetPhoto();
#include "portrait_cache_under_test.h"

static void Reset()
{
    world = World();
    Cd = CardState();
    C = PortraitState();
    lastPhotoStage = "none";
    photoSlotCursor = 0;
}
static void CheckFullyReleased()
{
    Check(ActiveDownloads() == 0 && C.photoDownload == -1 && !TargetPhotoReady() && !C.photoTexture[0],
        "cleanup clears download ownership and accepted texture state");
    Check(!world.captureActive && !C.photoTaken && world.captureStarts == world.captureCleanups,
        "all owned capture resources have paired cleanup");
}

static void TestRepeatedCapture()
{
    Reset();
    for (unsigned capture = 1; capture <= 3; ++capture)
    {
        world.subjectMale = capture != 2;
        Check(PhotographPed(kSubject), "successive captures complete on rotated slots");
        Check(C.photoSlot == Card::kPhotoSlot + static_cast<int>((capture - 1) % static_cast<unsigned>(Card::kPhotoSlotCount)) &&
            world.captureSlots.size() == capture && world.captureSlots.back() == C.photoSlot,
            "each capture writes the next configured slot and records it for its downloads");
        Check(world.writes == capture && world.writeCalls == capture,
            "every successful capture writes once without re-evaluating the success predicate");
        Check(world.requests == capture && ActiveDownloads() == 1 && world.releases == capture - 1,
            "each accepted capture replaces the preceding retained handle");
        Check(world.previousCleanups == capture && world.generatedDataInvalidations == 0,
            "repeated captures preserve generated data until the write is accepted");
        Check(world.cleanupFrame > world.commitObservedFrame,
            "capture cleanup occurs on a later frame than upload completion");
        Check(TargetPhotoReady() && C.photoDownloadStatus == 0 &&
            std::strcmp(C.photoTexture, world.downloads.back().name.c_str()) == 0 &&
            std::strcmp(C.photoLookupName, C.photoTexture) == 0,
            "accepted texture names survive mutation of native scratch storage");
    }
    ReleaseTargetPhoto();
    ReleaseTargetPhoto();
    Check(world.releases == 3, "repeated cleanup never releases a handle twice");
    CheckFullyReleased();
}

static void TestCaptureAvailabilityAndPublication()
{
    Reset();
    world.busyUntilFrame = 4;
    world.previousUploadUntilFrame = 7;
    world.uploadFrames = 6;
    world.publicationDelayMs = Card::kPhotoRequestRetryMs + kFrameMs;
    Check(PhotographPed(kSubject), "capture waits through busy state, prior upload and delayed publication");
    Check(world.generatedFrame >= world.previousUploadUntilFrame && world.captureStarts == 1,
        "shared readiness polling does not repeatedly initialize the capture");
    Check(world.commitObservedFrame >= world.uploadCompleteFrame &&
        world.cleanupFrame > world.commitObservedFrame,
        "queued upload completion is observed before cleanup on a separate frame");
    Check(world.requests > 1 && world.downloads.size() == 1 &&
        world.requestTimes.back() >= world.publishAt,
        "publication lag retries allocation without phantom handle ownership");
    Check(C.photoBusyBefore && !C.photoBusyAfterCleanup && !C.photoBusyAtRequest &&
        C.photoWriteComplete && C.photoRequestAttempts == world.requests,
        "capture and request diagnostics describe the completed delayed pipeline");
    ReleaseTargetPhoto();
    CheckFullyReleased();

    Reset();
    world.permanentlyBusy = true;
    Check(!PhotographPed(kSubject) && world.captureStarts == 0 && world.generates == 0 &&
        world.previousCleanups == 0 && world.requests == 0,
        "permanent shared capture busy state times out before touching capture data");
    Check(C.photoBusyBefore && !C.photoTaken && std::strcmp(lastPhotoStage, "capture_busy") == 0,
        "a busy timeout preserves its diagnostic without claiming capture ownership");
    Check(world.nowMs <= 1000 + Card::kPhotoUploadMs + kFrameMs,
        "the initial busy wait remains bounded by production timing");
    ReleaseTargetPhoto();
    CheckFullyReleased();

    Reset();
    world.previousUploadUntilFrame = std::numeric_limits<unsigned>::max();
    Check(!PhotographPed(kSubject) && world.captureStarts == 0 && world.previousCleanups == 0,
        "a prior upload that never completes prevents capture ownership and cleanup");
    ReleaseTargetPhoto();
    CheckFullyReleased();

    Reset();
    world.commitReady = false;
    Check(PhotographPed(kSubject) && !C.photoCommitBefore && !C.photoCommitReady &&
        C.photoWriteComplete && world.writes == 1,
        "CC4 remains diagnostic and cannot reject a completed MP upload and texture");
    ReleaseTargetPhoto();
    CheckFullyReleased();
}

static void TestPendingAndFailedDownloads()
{
    Reset();
    world.downloadDelayMs = 512;
    Check(PhotographPed(kSubject) && world.requests == 1,
        "pending download frames retain a single request handle");
    ReleaseTargetPhoto();

    Reset();
    world.failedDownloads = 1;
    Check(PhotographPed(kSubject) && world.requests == 2 && world.releases == 1,
        "a failed download is released before a successful replacement request");
    Check(world.requestTimes[1] - world.requestTimes[0] >= world.downloadDelayMs + Card::kPhotoRequestRetryMs,
        "failed download retry observes the configured backoff");
    ReleaseTargetPhoto();
    CheckFullyReleased();

    for (int sentinel : { -1, 0 })
    {
        Reset();
        world.unavailableResult = sentinel;
        world.unavailableRequests = 2;
        Check(PhotographPed(kSubject) && world.requests == 3 && world.downloads.size() == 1,
            "unavailable request sentinels can recover without creating phantom ownership");
        Check(world.requestTimes[1] - world.requestTimes[0] >= Card::kPhotoRequestRetryMs &&
            world.requestTimes[2] - world.requestTimes[1] >= Card::kPhotoRequestRetryMs,
            "unavailable requests are throttled between attempts");
        ReleaseTargetPhoto();
        CheckFullyReleased();
    }
}

static void TestCaptureTiming()
{
    Reset();
    world.pedReadyAtFrame = 4;
    world.busyUntilFrame = 7;
    world.uploadFrames = 6;
    world.downloadDelayMs = 96;
    world.previousCleanupCallMs = 3;
    world.generateCallMs = 7;
    world.writeCallMs = 11;
    world.cleanupInitCallMs = 5;
    world.cleanupFinishCallMs = 8;
    world.requestCallMs = 17;
    Check(PhotographPed(kSubject), "native and frame delays still complete a single capture");
    Check(C.photoAssetsMs == 4 * kFrameMs && C.photoIdleMs == 3 * kFrameMs &&
        C.photoUploadMs == 5 * kFrameMs,
        "asset, idle and upload timings measure their respective frame waits");
    Check(C.photoPreviousCleanupMs == 3 && C.photoGenerateMs == 7 && C.photoCleanupMs == 13,
        "generation and cleanup timings include only their scoped native calls");
    Check(C.photoWriteMs == 11 && C.photoWriteCallMaxMs == 11 && C.photoRequestCallMaxMs == 17 &&
        C.photoReadbackMs == 17 + world.downloadDelayMs,
        "native maximum durations exclude pending frames while readback includes them");
    const ULONGLONG measuredStages = C.photoReleaseMs + C.photoAssetsMs + C.photoIdleMs +
        C.photoPreviousCleanupMs + C.photoGenerateMs + C.photoWriteMs + C.photoUploadMs +
        C.photoCleanupMs + C.photoReadbackMs;
    Check(C.photoTimingStartedMs == 1000 && C.photoTotalMs == world.nowMs - 1000 &&
        C.photoTotalMs == measuredStages + 4 * kFrameMs,
        "total timing also includes the four explicit inter-stage yields");
    Check(world.generates == 1 && world.writeCalls == 1 && world.requests == 1 &&
        C.photoRequestAttempts == world.requests,
        "timing observation does not repeat generation, write or request calls");

    world.previousCleanupCallMs = world.generateCallMs = world.writeCallMs = 0;
    world.cleanupInitCallMs = world.cleanupFinishCallMs = world.requestCallMs = 0;
    world.releaseCallMs = 19;
    world.uploadFrames = 0;
    world.downloadDelayMs = 0;
    Check(PhotographPed(kSubject), "the timed capture can be replaced on the next slot");
    Check(C.photoReleaseMs == 19 && C.photoAssetsMs == 0 && C.photoIdleMs == 0 &&
        C.photoPreviousCleanupMs == 0 && C.photoGenerateMs == 0 && C.photoWriteMs == 0 &&
        C.photoWriteCallMaxMs == 0 && C.photoUploadMs == 0 && C.photoCleanupMs == 0 &&
        C.photoReadbackMs == 0 && C.photoRequestCallMaxMs == 0 &&
        C.photoTotalMs == 19 + 4 * kFrameMs,
        "each attempt resets all timings while separately recording the preceding handle release");
    Check(world.generates == 2 && world.writeCalls == 2 && world.requests == 2 && C.photoRequestAttempts == 1,
        "per-attempt request diagnostics reset without changing native call counts");
    ReleaseTargetPhoto();
    CheckFullyReleased();

    Reset();
    // WaitUntil cannot preempt a blocking native. This models a slow response,
    // not an assertion that the game native actually blocks for this duration.
    world.writeCallMs = 30000;
    world.downloadDelayMs = 0;
    Check(PhotographPed(kSubject) && world.writeCalls == 1 && C.photoWritten,
        "a slow successful write result is consumed once even after the polling deadline");
    Check(C.photoWriteMs == 30000 && C.photoWriteCallMaxMs == 30000 &&
        C.photoTotalMs == 30000 + 4 * kFrameMs,
        "a blocking native is visible in stage and total timing without appearing as frame polling");
    ReleaseTargetPhoto();
    CheckFullyReleased();
}

static void TestFailureAndCancellationCleanup()
{
    Reset();
    world.downloadDelayMs = Card::kPhotoNameMs * 4;
    Check(!PhotographPed(kSubject) && std::strcmp(lastPhotoStage, "texture_download") == 0,
        "a pending texture download times out at the diagnostic download stage");
    Check(C.photoDownload > 0 && C.photoDownloadStatus == 1 && ActiveDownloads() == 1,
        "a failed attempt preserves its pending handle and status for the caller's failure log");
    // SpawnTargetWithPhoto logs the failed attempt, then releases it on final failure.
    ReleaseTargetPhoto();
    CheckFullyReleased();

    Reset();
    world.downloadDelayMs = Card::kPhotoNameMs * 4;
    Check(!PhotographPed(kSubject), "first attempt can fail while owning a pending download");
    world.downloadDelayMs = 0;
    Check(PhotographPed(kSubject) && world.requests == 2 && world.releases == 1,
        "a retake releases a failed attempt's handle before writing the next slot");
    ReleaseTargetPhoto();
    CheckFullyReleased();

    Reset();
    world.cancelOnRequest = true;
    Check(!PhotographPed(kSubject) && C.photoDownload > 0,
        "player interruption exits download polling with diagnostics available");
    ReleaseTargetPhoto();
    CheckFullyReleased();

    for (int stage = 0; stage < 3; ++stage)
    {
        Reset();
        Check(PhotographPed(kSubject), "establish an accepted portrait before a failed retake");
        if (stage == 0) world.generateSucceeds = false;
        if (stage == 1) world.writeSucceeds = false;
        if (stage == 2) world.uploadNeverCompletes = true;
        Check(!PhotographPed(kSubject) && world.releases == 1 && ActiveDownloads() == 0,
            "generation, write and pending upload failures cannot retain the preceding portrait");
        if (stage == 2)
            Check(C.photoWritten && C.photoUploadPending && !C.photoWriteComplete &&
                C.photoRequestAttempts == 0 && std::strcmp(lastPhotoStage, "upload") == 0,
                "an upload timeout retains accepted-write diagnostics without requesting an incomplete photo");
        ReleaseTargetPhoto();
        CheckFullyReleased();
    }
}

static void TestNameValidationAndRecovery()
{
    Reset();
    Check(PhotographPed(kSubject), "establish a texture for name validation");
    Download& download = FindDownload(C.photoDownload);
    const std::string originalName = download.name;
    char name[64] = "";
    const unsigned validityChecks = world.validityChecks;
    download.name = std::string(80, 'x');
    Check(!LookupPhotoTexture(Card::kPhotoCacheType, name) && world.validityChecks == validityChecks,
        "oversized borrowed names are rejected before copying or validity natives");
    download.name.clear();
    Check(!LookupPhotoTexture(Card::kPhotoCacheType, name), "empty names cannot become accepted textures");
    download.name = originalName;
    download.nameValid = false;
    Check(LookupPhotoTexture(Card::kPhotoCacheType, name) && C.photoTextureValid && !C.photoLookupValid,
        "a completed nonempty explicit download is accepted even when the backup-name diagnostic is false");
    download.nameValid = true;
    Check(LookupPhotoTexture(Card::kPhotoCacheType, name) && C.photoLookupValid && world.requests == 1,
        "the name diagnostic can change on the same retained handle without another request");
    ReleaseTargetPhoto();
    CheckFullyReleased();
}

static void TestMaintenanceRebinding()
{
    Reset();
    Check(PhotographPed(kSubject), "establish a portrait for material maintenance");
    Cd.obj = kCard;
    Cd.examining = true;
    MaintainPortraitAndCard();
    Check(world.binds == 1 && world.cardVisible && world.flipWrites == 1,
        "maintenance binds the accepted texture and preserves card visibility and flip");
    world.nowMs += Card::kTextureSettleMs + 1;
    MaintainPortraitAndCard();
    Check(world.binds == 2 && world.requests == 1,
        "late maintenance makes one final scheduled bind without reallocating the download");
    MaintainPortraitAndCard();
    Check(world.binds == 2, "completed binding schedules do not repeat on later maintenance calls");

    Download& retained = FindDownload(C.photoDownload);
    retained.nameValid = false;
    MaintainPortraitAndCard();
    Check(world.cardVisible && C.photoTextureValid && !C.photoLookupValid && world.binds == 2,
        "a false backup-name diagnostic cannot hide a completed explicit download");
    const std::string originalName = retained.name;
    retained.name.clear();
    MaintainPortraitAndCard();
    Check(!world.cardVisible && !C.photoTextureValid && world.binds == 2,
        "a temporarily missing completed name hides the owned card without binding an empty texture");
    retained.name = originalName + "_renewed";
    MaintainPortraitAndCard();
    Check(world.cardVisible && TargetPhotoReady() && world.binds == 3 &&
        world.boundTexture == joaat(retained.name.c_str()) && C.photoTexture == retained.name,
        "name recovery updates the accepted name and restarts material binding");
    retained.finalStatus = 2;
    MaintainPortraitAndCard();
    Check(C.photoDownload == -1 && world.releases == 1 && !world.cardVisible,
        "maintenance releases a failed retained handle and hides its card");
    world.nowMs += Card::kPhotoRequestRetryMs;
    Check(EnsureTargetPhotoReady() && world.requests == 2 && world.cardVisible,
        "an inspection readiness wait can reacquire the accepted slot after failure");
    ReleaseTargetPhoto();
    CheckFullyReleased();
}

static void TestSlotRotation()
{
    Reset();
    char name[64] = "";
    Check(!LookupPhotoTexture(Card::kPhotoCacheType, name) && world.requests == 0 && C.photoDownload == -1,
        "no download is requested before any capture has written a slot");
    const unsigned rounds = static_cast<unsigned>(Card::kPhotoSlotCount) + 2;
    for (unsigned capture = 0; capture < rounds; ++capture)
    {
        Check(PhotographPed(kSubject), "captures beyond the slot count keep completing");
        Check(C.photoSlot == Card::kPhotoSlot + static_cast<int>(capture % static_cast<unsigned>(Card::kPhotoSlotCount)) &&
            world.captureSlots.back() == C.photoSlot,
            "captures rotate through every configured slot before reusing the first one");
        Check(std::strcmp(C.photoTexture, world.downloads.back().name.c_str()) == 0,
            "each rotated slot yields its own accepted texture name");
    }
    Check(world.captureSlots.size() == rounds, "every completed capture wrote exactly one slot");
    ReleaseTargetPhoto();
    CheckFullyReleased();

    // A capture that fails before its write still consumed its slot: the retake on the same
    // subject never returns to the slot it just touched, and a later contract keeps rotating.
    Reset();
    world.generateSucceeds = false;
    Check(!PhotographPed(kSubject) && C.photoSlot == Card::kPhotoSlot && world.captureSlots.empty(),
        "a failed first capture used the first slot without writing it");
    world.generateSucceeds = true;
    Check(PhotographPed(kSubject) && C.photoSlot == Card::kPhotoSlot + 1 &&
        world.captureSlots.size() == 1 && world.captureSlots[0] == Card::kPhotoSlot + 1,
        "the retry after a failed capture writes the next slot");

    // The diagnostic reopen releases the handle but keeps the slot, so it re-requests what was written.
    ReleaseTargetPhoto();
    Check(C.photoSlot == Card::kPhotoSlot + 1 && !C.photoTexture[0] && C.photoDownload == -1,
        "release keeps the last written slot for a read-only reopen");
    Check(WaitUntil(Card::kPhotoNameMs, [&] { return LookupPhotoTexture(Card::kPhotoCacheType, name); }) &&
        world.requests == 2 && C.photoDownload > 0 && C.photoTextureValid,
        "a read-only reopen requests the last written slot");
    ReleaseTargetPhoto();
    CheckFullyReleased();

    C = PortraitState(); // contract reset does not restart the rotation
    Check(C.photoSlot == -1 && !LookupPhotoTexture(Card::kPhotoCacheType, name) && world.requests == 2,
        "a reset contract owns no slot until its own capture writes one");
    Check(PhotographPed(kSubject) && C.photoSlot == Card::kPhotoSlot + 2,
        "the next contract continues the rotation instead of rewriting the previous card's slot");
    ReleaseTargetPhoto();
    CheckFullyReleased();
}

int main()
{
    Check(Card::kPhotoRequestRetryMs > 0 && Card::kPhotoNameMs > Card::kPhotoRequestRetryMs,
        "production timing permits bounded request recovery");
    Check(Card::kPhotoSlot >= 0 && Card::kPhotoSlotCount >= 2 && Card::kPhotoSlot + Card::kPhotoSlotCount <= 32,
        "production rotates through at least two of the engine's 32 local persona-photo slots");
    TestSlotRotation();
    TestRepeatedCapture();
    TestCaptureAvailabilityAndPublication();
    TestPendingAndFailedDownloads();
    TestCaptureTiming();
    TestFailureAndCancellationCleanup();
    TestNameValidationAndRecovery();
    TestMaintenanceRebinding();
    std::printf("All %u portrait cache checks passed (actual production functions).\n", checks);
}
