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
    bool generateSucceeds = true;
    bool writeSucceeds = true;
    bool commitReady = true;
    bool captureActive = false;
    unsigned captureStarts = 0;
    unsigned captureCleanups = 0;
    unsigned cleanupInitializations = 0;
    unsigned generates = 0;
    unsigned writeCalls = 0;
    unsigned writes = 0;
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
    char borrowedName[128] = "";
} world;

static struct CardState
{
    Object obj = 0;
    bool ownsObj = true;
    bool customApplied = false;
    ULONGLONG textureRefreshUntilMs = 0;
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
static bool IS_PED_READY_TO_RENDER(Ped ped) { Check(ped == kSubject, "readiness uses the supplied subject"); return world.pedReady; }
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
static void _PEDSHOT_SET_PERSONA_PHOTO_TYPE(int type)
{
    Check(type == 1 && !world.captureActive, "each capture starts with the verified SP photo type");
    Check(ActiveDownloads() == 0, "previous downloads are released before the next capture starts");
    world.captureActive = true;
    ++world.captureStarts;
}
static void _0xA1A86055792FB249(int type) { Check(type == 0, "the independent SP capture argument remains zero"); }
static void _PEDSHOT_PREVIOUS_PERSONA_PHOTO_DATA_CLEANUP() {}
static bool _PEDSHOT_GENERATE_PERSONA_PHOTO(const char* name, Ped ped, int slot)
{
    Check(world.captureActive && ped == kSubject && slot == 0, "capture generation retains its verified arguments");
    Check(std::strcmp(name, world.subjectMale ? "MINIGAME_PROFILE_PHOTO" : "MINIGAME_PROFILE_PHOTO_F") == 0,
        "male and female captures preserve their respective names");
    ++world.generates;
    return world.generateSucceeds;
}
static void _PEDSHOT_INIT_CLEANUP_DATA()
{
    Check(world.captureActive, "capture cleanup starts only for owned capture resources");
    ++world.cleanupInitializations;
}
static void _PEDSHOT_FINISH_CLEANUP_DATA()
{
    Check(world.captureActive && world.cleanupInitializations == world.captureCleanups + 1,
        "capture cleanup start and finish remain paired");
    world.captureActive = false;
    ++world.captureCleanups;
}
}
namespace NETWORK
{
static bool _NETWORK_IS_PREVIOUS_UPLOAD_PENDING() { return false; }
static bool _0xCC4E72C339461ED1() { return world.commitReady; }
static bool _NETWORK_PERSONA_PHOTO_WRITE_LOCAL(const char*, int slot, int format, int cacheType)
{
    Check(world.captureActive && slot == 0 && format == 1 && cacheType == 2,
        "write arguments preserve the verified local slot and cache type");
    Check(ActiveDownloads() == 0, "a retained download cannot survive into a same-slot overwrite");
    ++world.writeCalls;
    if (world.writeSucceeds) ++world.writes;
    return world.writeSucceeds;
}
static int _LOCAL_PLAYER_PEDSHOT_TEXTURE_DOWNLOAD_REQUEST(int slot, int cacheType)
{
    Check(slot == 0 && cacheType == 2, "download uses the exact slot and cache type written");
    Check(world.writes > 0 && !world.captureActive, "download begins after capture commit and cleanup");
    Check(ActiveDownloads() == 0, "only one download is owned at a time");
    ++world.requests;
    world.requestTimes.push_back(world.nowMs);
    if (world.cancelOnRequest) world.playerAvailable = false;
    if (world.unavailableRequests)
    {
        --world.unavailableRequests;
        return world.unavailableResult;
    }
    const int id = 101 + static_cast<int>(world.downloads.size());
    const int status = world.failedDownloads ? 2 : 0;
    if (world.failedDownloads) --world.failedDownloads;
    world.downloads.push_back(Download{ id, world.nowMs + world.downloadDelayMs, status,
        "portrait_" + std::to_string(world.writes) + "_" + std::to_string(id) });
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
static bool TargetPhotoReady();
static void ReleaseTargetPhoto();
#include "portrait_cache_under_test.h"

static void Reset()
{
    world = World();
    Cd = CardState();
    C = PortraitState();
    lastPhotoStage = "none";
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
        Check(PhotographPed(kSubject), "successive same-slot captures complete");
        Check(world.writes == capture && world.writeCalls == capture,
            "every successful capture writes once without re-evaluating the success predicate");
        Check(world.requests == capture && ActiveDownloads() == 1 && world.releases == capture - 1,
            "each accepted capture replaces the preceding retained handle");
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
        "a retake releases a failed attempt's handle before overwriting the slot");
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
        if (stage == 2) world.commitReady = false;
        Check(!PhotographPed(kSubject) && world.releases == 1 && ActiveDownloads() == 0,
            "generation, write and commit failures cannot retain the preceding portrait");
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
    Check(world.binds == 1 && world.requests == 1, "settled maintenance neither rebinds nor reallocates");

    Download& retained = FindDownload(C.photoDownload);
    retained.nameValid = false;
    MaintainPortraitAndCard();
    Check(world.cardVisible && C.photoTextureValid && !C.photoLookupValid && world.binds == 1,
        "a false backup-name diagnostic cannot hide a completed explicit download");
    const std::string originalName = retained.name;
    retained.name.clear();
    MaintainPortraitAndCard();
    Check(!world.cardVisible && !C.photoTextureValid && world.binds == 1,
        "a temporarily missing completed name hides the owned card without binding an empty texture");
    retained.name = originalName + "_renewed";
    MaintainPortraitAndCard();
    Check(world.cardVisible && TargetPhotoReady() && world.binds == 2 &&
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

int main()
{
    Check(Card::kPhotoRequestRetryMs > 0 && Card::kPhotoNameMs > Card::kPhotoRequestRetryMs,
        "production timing permits bounded request recovery");
    TestRepeatedCapture();
    TestPendingAndFailedDownloads();
    TestFailureAndCancellationCleanup();
    TestNameValidationAndRecovery();
    TestMaintenanceRebinding();
    std::printf("All %u portrait cache checks passed (actual production functions).\n", checks);
}
