// Exercises the production diagnostic driver and probe order with deterministic phase outcomes.
// Capture/rendering and handle allocation internals are covered separately by portrait_cache_tests.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

using DWORD = std::uint32_t;
using ULONGLONG = std::uint64_t;
using Ped = int;
constexpr Ped kSubject = 77;
static unsigned checks = 0;
static void Check(bool value, const char* description)
{
    ++checks;
    if (!value) { std::fprintf(stderr, "FAILED: %s\n", description); std::exit(EXIT_FAILURE); }
}

struct CaptureOutcome
{
    bool success = true;
    bool written = true;
    bool complete = true;
    bool pendingHandle = false;
};
enum class LookupOutcome { Success, Unavailable, Pending };
struct LogEntry
{
    std::string phase;
    bool success;
    unsigned captures;
    std::string previous;
    std::string observed;
};
static struct TestWorld
{
    ULONGLONG nowMs = 1000;
    bool playerAvailable = true;
    unsigned frames = 0;
    unsigned captures = 0;
    unsigned explicitProbes = 0;
    unsigned lookupCalls = 0;
    unsigned backupCalls = 0;
    unsigned nameReleases = 0;
    unsigned handlesCreated = 0;
    unsigned handlesReleased = 0;
    unsigned cleanupRequests = 0;
    unsigned visibilityChanges = 0;
    bool newExplicitProbe = true;
    LookupOutcome currentLookup = LookupOutcome::Unavailable;
    bool oldNameValid = true;
    bool invalidationSucceeds = true;
    ULONGLONG invalidAt = 0;
    bool backupSucceeds = true;
    std::string backupName = "backup_photo";
    char borrowedName[128] = "";
    std::string interruptAfterLog;
    std::vector<CaptureOutcome> captureOutcomes = { {}, {} };
    std::vector<LookupOutcome> lookupOutcomes = { LookupOutcome::Success };
    std::vector<std::string> lookupNames;
    std::string expectedReleasedName = "portrait_1";
    std::vector<int> activeHandles;
    std::vector<LogEntry> logs;
    std::vector<std::string> operations;
} world;

// Generated declarations come first so the stubs use production fields/constants/enumeration.
#include "photo_self_test_state.h"

static ULONGLONG GetTickCount64() { return world.nowMs; }
static bool PlayerAvailable() { return world.playerAvailable; }
static void MaintainOwnedPedCleanup() {}
static void MaintainPortraitAndCard()
{
    Check(!C.photoTexture[0], "probe waits never expose an accepted name to normal handle maintenance");
}
static void WAIT(DWORD delay)
{
    Check(delay == 0, "diagnostic waits yield game frames");
    world.nowMs += 16;
    ++world.frames;
    Check(world.frames < 2000, "all diagnostic waits remain bounded");
}
static int AcquireHandle()
{
    Check(world.activeHandles.empty(), "a new phase cannot allocate over an owned handle");
    const int handle = 101 + static_cast<int>(world.handlesCreated++);
    world.activeHandles.push_back(handle);
    return handle;
}
static void ReleaseTargetPhoto()
{
    if (C.photoDownload > 0)
    {
        Check(world.activeHandles.size() == 1 && world.activeHandles[0] == C.photoDownload,
            "cleanup releases exactly the handle retained by the preceding phase");
        world.activeHandles.clear();
        ++world.handlesReleased;
    }
    C.photoDownload = -1;
    C.photoDownloadStatus = -1;
    C.photoLookupName[0] = '\0';
    C.photoTexture[0] = '\0';
    C.photoTextureValid = false;
    C.photoLookupValid = false;
    world.newExplicitProbe = true;
}
static bool PhotographPed(Ped subject)
{
    Check(subject == kSubject, "every capture uses the same provisional subject");
    Check(world.captures < world.captureOutcomes.size(), "the driver performs only configured captures");
    ReleaseTargetPhoto();
    const CaptureOutcome outcome = world.captureOutcomes[world.captures++];
    world.operations.push_back("capture");
    C.photoRequestAttempts = 0;
    C.photoWritten = outcome.written;
    C.photoWriteComplete = outcome.complete;
    if (outcome.success || outcome.pendingHandle)
    {
        C.photoDownload = AcquireHandle();
        C.photoDownloadStatus = outcome.success ? 0 : 1;
    }
    if (outcome.success)
    {
        const std::string name = "portrait_" + std::to_string(world.captures);
        strcpy_s(C.photoTexture, name.c_str());
        strcpy_s(C.photoLookupName, name.c_str());
        C.photoTextureValid = true;
    }
    return outcome.success;
}
static bool LookupPhotoTexture(int cacheType, char (&name)[64])
{
    Check(cacheType == Card::kPhotoCacheType && !C.photoTexture[0],
        "explicit probes use the configured cache without normal accepted-name polling");
    ++world.lookupCalls;
    ++C.photoRequestAttempts;
    if (world.newExplicitProbe)
    {
        Check(world.explicitProbes < world.lookupOutcomes.size(), "only configured explicit probes execute");
        world.currentLookup = world.lookupOutcomes[world.explicitProbes++];
        world.operations.push_back("explicit");
        world.newExplicitProbe = false;
        if (world.currentLookup != LookupOutcome::Unavailable) C.photoDownload = AcquireHandle();
    }
    C.photoDownloadStatus = world.currentLookup == LookupOutcome::Success ? 0
        : world.currentLookup == LookupOutcome::Pending ? 1 : -1;
    if (world.currentLookup != LookupOutcome::Success) return false;
    const std::string observed = world.explicitProbes <= world.lookupNames.size()
        ? world.lookupNames[world.explicitProbes - 1] : "portrait_" + std::to_string(world.captures);
    strcpy_s(name, observed.c_str());
    strcpy_s(C.photoLookupName, name);
    C.photoTextureValid = true;
    return true;
}
static void LogPhotoCacheTest(const char* phase, bool success, ULONGLONG started,
    unsigned captures, const char* previousName, const char* observedName)
{
    Check(started <= world.nowMs && captures == world.captures, "phase logs retain the actual capture count");
    world.logs.push_back({ phase, success, captures, previousName, observedName });
    if (world.interruptAfterLog == phase) world.playerAvailable = false;
}
static void RequestOwnedPedCleanup(Ped subject)
{
    Check(subject == kSubject && world.activeHandles.empty(),
        "the caller releases the last handle before requesting provisional ped cleanup");
    ++world.cleanupRequests;
}
namespace ENTITY
{
static void SET_ENTITY_VISIBLE(Ped subject, bool visible)
{
    Check(subject == kSubject && !visible, "the diagnostic caller hides only its provisional subject");
    ++world.visibilityChanges;
}
}
namespace NETWORK
{
static void _TEXTURE_DOWNLOAD_RELEASE_BY_NAME(const char* name)
{
    Check(name == world.expectedReleasedName && world.activeHandles.empty(),
        "name invalidation uses only the latest accepted name after releasing explicit ownership");
    ++world.nameReleases;
    world.operations.push_back("invalidate");
    world.invalidAt = world.nowMs + 32;
}
static bool _TEXTURE_DOWNLOAD_TEXTURE_NAME_IS_VALID(const char* name)
{
    Check(name != world.borrowedName, "the backup helper copies its borrowed name before another native");
    const std::string copied(name);
    strcpy_s(world.borrowedName, "native scratch changed");
    if (copied == world.backupName) return world.backupSucceeds;
    if (world.nameReleases && world.invalidationSucceeds && world.nowMs >= world.invalidAt)
        world.oldNameValid = false;
    return world.oldNameValid;
}
static const char* _REQUEST_PEDSHOT_TEXTURE_LOCAL_BACKUP_DOWNLOAD(int slot, int cacheType)
{
    Check(slot == Card::kPhotoSlot && cacheType == Card::kPhotoCacheType,
        "backup readback preserves the configured slot and cache type");
    Check(world.activeHandles.empty() && !C.photoTexture[0],
        "backup reading starts without an explicit handle or accepted name");
    if (world.backupCalls++ == 0) world.operations.push_back("backup");
    strcpy_s(world.borrowedName, world.backupName.c_str());
    return world.borrowedName;
}
}

#include "photo_self_test_under_test.h"

static void Reset()
{
    world = TestWorld();
    C = PortraitState();
    lastPhotoStage = "none";
    lastStartFailure = ContractStartFailure::None;
}
static void CheckPhases(std::initializer_list<const char*> expected)
{
    Check(world.logs.size() == expected.size(), "the driver logs exactly the expected phases");
    unsigned index = 0;
    for (const char* phase : expected)
        Check(world.logs[index++].phase == phase, "diagnostic phases execute in their intended order");
}
static void RunCallerAndCheck(bool interrupted = false)
{
    Check(RunPhotoTestAsCaller(kSubject) == 0, "diagnostics never issue a bounty target");
    Check(world.activeHandles.empty() && world.handlesCreated == world.handlesReleased,
        "the real diagnostic caller releases all positive handles on every exit");
    Check(world.cleanupRequests == 1 && world.visibilityChanges == 1,
        "the caller hides and requests checked cleanup of its one provisional ped");
    Check(lastStartFailure == (interrupted ? ContractStartFailure::Interrupted
        : ContractStartFailure::PhotoDiagnosticComplete),
        "completed diagnostics use the dedicated no-retry outcome while interruption stays distinct");
}
static void TestInitialFailure()
{
    Reset();
    world.captureOutcomes = { { false, true, true, true } };
    RunCallerAndCheck();
    CheckPhases({ "begin", "initial" });
    Check(world.captures == 1 && world.explicitProbes == 0 && world.nameReleases == 0 &&
        world.backupCalls == 0 && world.handlesReleased == 1,
        "initial failure stops all later probes while releasing a pending initial handle");
}
static void TestReopenAndRewrite()
{
    Reset();
    RunCallerAndCheck();
    CheckPhases({ "begin", "initial", "A_reopen_without_write", "B_second_capture" });
    Check(world.captures == 2 && world.explicitProbes == 1 && world.handlesReleased == 3,
        "successful A performs exactly one same-subject rewrite and caller cleanup retains no handle");
    Check(world.operations == std::vector<std::string>({ "capture", "explicit", "capture" }),
        "A runs between the two captures without generating or writing a photo");
}
static void TestNameRecoveryWithoutRewrite()
{
    for (LookupOutcome failure : { LookupOutcome::Unavailable, LookupOutcome::Pending })
    {
        Reset();
        world.lookupOutcomes = { failure, LookupOutcome::Success };
        RunCallerAndCheck();
        CheckPhases({ "begin", "initial", "A_reopen_without_write", "C_before_name_release", "C_name_invalidation",
            "C_reopen_without_write" });
        Check(world.captures == 1 && world.explicitProbes == 2 && world.nameReleases == 1 &&
            world.backupCalls == 0,
            "A failure and C recovery cannot generate or write another capture");
        Check(world.operations == std::vector<std::string>({ "capture", "explicit", "invalidate", "explicit" }),
            "name recovery changes cache invalidation only, after ownership is released");
    }
}
static void TestRewriteFailureRecovery()
{
    Reset();
    world.captureOutcomes = { {}, { false, true, true, true } };
    world.lookupOutcomes = { LookupOutcome::Success, LookupOutcome::Success };
    world.lookupNames = { "reopened_portrait", "recovered_portrait" };
    world.expectedReleasedName = "reopened_portrait";
    RunCallerAndCheck();
    CheckPhases({ "begin", "initial", "A_reopen_without_write", "B_second_capture",
        "C_before_name_release", "C_name_invalidation", "C_reopen_without_write" });
    Check(world.captures == 2 && world.explicitProbes == 2 && world.backupCalls == 0,
        "failed rewritten download can recover through C without a third capture");
    Check(world.logs.back().previous == "reopened_portrait",
        "the name accepted by A replaces the initial name before invalidation after B");

    for (bool failedWrite : { false, true })
    {
        Reset();
        world.captureOutcomes = { {}, { false, !failedWrite, false, false } };
        RunCallerAndCheck();
        CheckPhases({ "begin", "initial", "A_reopen_without_write", "B_second_capture" });
        Check(world.nameReleases == 0 && world.backupCalls == 0,
            "an incomplete second write cannot enter readback recovery probes");
    }
}
static void TestBackupIsLastAndIsolated()
{
    for (bool invalidates : { false, true })
    {
        Reset();
        world.invalidationSucceeds = invalidates;
        world.lookupOutcomes = { LookupOutcome::Pending, LookupOutcome::Pending };
        RunCallerAndCheck();
        if (invalidates)
            CheckPhases({ "begin", "initial", "A_reopen_without_write", "C_before_name_release", "C_name_invalidation",
                "C_reopen_without_write", "D_backup_without_write" });
        else
            CheckPhases({ "begin", "initial", "A_reopen_without_write", "C_before_name_release", "C_name_invalidation",
                "D_backup_without_write" });
        Check(world.captures == 1 && world.operations.back() == "backup" && world.backupCalls == 1,
            "backup is the final isolated readback phase and performs no capture");
        Check(!C.photoTexture[0] && C.photoDownload == -1 &&
            world.logs.back().observed == world.backupName,
            "backup results survive borrowed-buffer mutation without entering normal accepted state");
    }
}
static void TestAlreadyInvalidName()
{
    Reset();
    world.oldNameValid = false;
    world.lookupOutcomes = { LookupOutcome::Unavailable, LookupOutcome::Success };
    RunCallerAndCheck();
    CheckPhases({ "begin", "initial", "A_reopen_without_write", "C_before_name_release",
        "C_name_already_invalid", "C_reopen_without_write" });
    Check(!world.logs[3].success && world.logs[4].success && world.captures == 1,
        "an already-invalid name is logged distinctly and its reopen still performs no write");
}
static void TestInterruption()
{
    for (const char* phase : { "begin", "initial", "A_reopen_without_write",
        "C_before_name_release", "C_name_invalidation", "C_reopen_without_write", "D_backup_without_write" })
    {
        Reset();
        world.interruptAfterLog = phase;
        world.lookupOutcomes = { LookupOutcome::Pending, LookupOutcome::Pending };
        RunCallerAndCheck(true);
        Check(world.logs.back().phase == phase, "interruption prevents every phase after the interruption log");
    }
    Reset();
    world.interruptAfterLog = "A_reopen_without_write";
    RunCallerAndCheck(true);
    Check(world.captures == 1 && world.handlesReleased == 2 && world.nameReleases == 0,
        "interruption immediately after successful A releases its handle and prevents the second capture");
}
int main()
{
    TestInitialFailure();
    TestReopenAndRewrite();
    TestNameRecoveryWithoutRewrite();
    TestRewriteFailureRecovery();
    TestBackupIsLastAndIsolated();
    TestAlreadyInvalidName();
    TestInterruption();
    std::printf("All %u photo self-test driver checks passed (actual production driver/helpers/caller).\n", checks);
}
