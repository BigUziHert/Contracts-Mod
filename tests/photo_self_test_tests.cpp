// Exercises the actual v4 diagnostic driver, read-only reopen helper, and caller cleanup.
// Native object/inspection internals and rendering are covered by their separate suites.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using DWORD = std::uint32_t;
using ULONGLONG = std::uint64_t;
using Ped = int;
struct ContractDef {};
constexpr Ped kSubject = 77;
static unsigned checks = 0;
static void Check(bool value, const char* description)
{
    ++checks;
    if (!value) { std::fprintf(stderr, "FAILED: %s\n", description); std::exit(EXIT_FAILURE); }
}

struct CaptureOutcome { bool success = true; bool pendingHandle = false; };
enum class LookupOutcome { Success, Unavailable, Pending };
struct LogEntry
{
    std::string control, phase;
    bool success;
    unsigned captures;
    std::string previous, observed;
    bool plain;
};
static const char* kControls[] = {
    "baseline", "unbound_object", "plain_inspection", "bound_object", "portrait_inspection"
};
static struct TestWorld
{
    ULONGLONG nowMs = 1000;
    bool playerAvailable = true;
    unsigned frames = 0;
    unsigned captures = 0;
    unsigned consumers = 0;
    unsigned objects = 0;
    unsigned inspections = 0;
    unsigned explicitProbes = 0;
    unsigned lookupCalls = 0;
    unsigned handlesCreated = 0;
    unsigned handlesReleased = 0;
    unsigned cleanupRequests = 0;
    unsigned visibilityChanges = 0;
    bool newExplicitProbe = true;
    LookupOutcome currentLookup = LookupOutcome::Unavailable;
    std::string interruptAfterLog;
    unsigned interruptInConsumer = 0;
    bool interruptInLookup = false;
    std::vector<CaptureOutcome> captureOutcomes = std::vector<CaptureOutcome>(6);
    std::vector<LookupOutcome> lookupOutcomes = std::vector<LookupOutcome>(5, LookupOutcome::Success);
    std::vector<bool> consumerOutcomes = std::vector<bool>(4, true);
    std::vector<std::string> acceptedNames = std::vector<std::string>(4);
    std::vector<int> activeHandles;
    std::vector<LogEntry> logs;
    std::vector<std::string> operations;
} world;

#include "photo_self_test_state.h"

static ULONGLONG GetTickCount64() { return world.nowMs; }
static bool PlayerAvailable() { return world.playerAvailable; }
static void MaintainOwnedPedCleanup() {}
static void MaintainPortraitAndCard()
{
    Check(!C.photoTexture[0], "reopen waits do not expose an accepted name to normal handle maintenance");
}
static void WAIT(DWORD delay)
{
    Check(delay == 0, "diagnostic waits yield game frames");
    world.nowMs += 16;
    ++world.frames;
    Check(world.frames < 2000, "failed diagnostic waits remain bounded");
}
static int AcquireHandle()
{
    Check(world.activeHandles.empty(), "a phase cannot allocate over an owned handle");
    const int handle = 101 + static_cast<int>(world.handlesCreated++);
    world.activeHandles.push_back(handle);
    return handle;
}
static void ReleaseTargetPhoto()
{
    if (C.photoDownload > 0)
    {
        Check(world.activeHandles.size() == 1 && world.activeHandles[0] == C.photoDownload,
            "cleanup releases precisely the handle retained by the preceding phase");
        world.activeHandles.clear();
        ++world.handlesReleased;
    }
    C.photoDownload = C.photoDownloadStatus = -1;
    C.photoLookupName[0] = C.photoTexture[0] = '\0';
    C.photoTextureValid = C.photoLookupValid = false;
    world.newExplicitProbe = true;
}
static bool PhotographPed(Ped subject)
{
    Check(subject == kSubject && PlayerAvailable(), "every capture uses the same available provisional subject");
    Check(world.captures < world.captureOutcomes.size(), "the driver never performs an extra capture");
    Check(std::strcmp(photoTestControl, kControls[world.captures ? world.captures - 1 : 0]) == 0,
        "captures retain their baseline or consumer control label");
    Check(photoTestPlainCard == (world.captures < 4),
        "baseline and plain consumers keep portrait binding disabled through their capture checkpoints");
    ReleaseTargetPhoto();
    const CaptureOutcome outcome = world.captureOutcomes[world.captures++];
    world.operations.push_back(std::string(photoTestControl) + "/capture");
    C.photoRequestAttempts = 0;
    C.photoWritten = C.photoWriteComplete = outcome.success;
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
        "reopen uses the configured cache with normal accepted-name polling disabled");
    ++world.lookupCalls;
    ++C.photoRequestAttempts;
    if (world.newExplicitProbe)
    {
        Check(world.explicitProbes < world.lookupOutcomes.size(), "only configured reopen checkpoints execute");
        world.currentLookup = world.lookupOutcomes[world.explicitProbes++];
        world.operations.push_back(std::string(photoTestControl) + "/reopen");
        world.newExplicitProbe = false;
        if (world.currentLookup != LookupOutcome::Unavailable) C.photoDownload = AcquireHandle();
    }
    C.photoDownloadStatus = world.currentLookup == LookupOutcome::Success ? 0
        : world.currentLookup == LookupOutcome::Pending ? 1 : -1;
    if (world.interruptInLookup) world.playerAvailable = false;
    if (world.currentLookup != LookupOutcome::Success) return false;
    const std::string observed = "reopened_" + std::to_string(world.explicitProbes);
    strcpy_s(name, observed.c_str());
    strcpy_s(C.photoLookupName, name);
    C.photoTextureValid = true;
    return true;
}
static bool ProbeConsumer(bool object, ULONGLONG started, unsigned captures)
{
    Check(world.consumers < 4 && PlayerAvailable(), "no consumer runs past a failure or player interruption");
    const unsigned index = world.consumers++;
    Check(started <= world.nowMs && captures == world.captures && captures == index + 2 && C.def &&
        C.photoTexture[0] && C.photoDownload > 0 && world.activeHandles.size() == 1,
        "consumer uses the preceding checkpoint's accepted capture and retained handle");
    Check(object == (index % 2 == 0) && std::strcmp(photoTestControl, kControls[index + 1]) == 0,
        "unbound object, plain inspection, bound object, and portrait inspection run in order");
    Check(photoTestPlainCard == (index < 2), "plain controls disable portrait binding only in the first two stages");
    Check(photoTestBindAttempts == 0 && photoTestBindingTransitions == 0 &&
        C.photoCardFaceDraws == 0 && C.photoCardPanelDraws == 0 &&
        C.photoCardRenderId == 0 && !C.photoCardRenderName[0],
        "each consumer begins with independent binding and rendering counters");
    if (object) ++world.objects; else ++world.inspections;
    world.operations.push_back(std::string(photoTestControl) + (object ? "/object" : "/inspection"));
    photoTestBindAttempts = photoTestBindingTransitions = 7;
    C.photoCardFaceDraws = C.photoCardPanelDraws = 9;
    C.photoCardRenderId = 55;
    strcpy_s(C.photoCardRenderName, "preceding_consumer_render_target");
    if (!world.acceptedNames[index].empty())
    {
        strcpy_s(C.photoTexture, world.acceptedNames[index].c_str());
        strcpy_s(C.photoLookupName, C.photoTexture);
    }
    if (world.interruptInConsumer == index + 1) world.playerAvailable = false;
    return world.consumerOutcomes[index];
}
static bool ProbePhotoObject(ULONGLONG started, unsigned captures) { return ProbeConsumer(true, started, captures); }
static bool ProbePhotoCard(ULONGLONG started, unsigned captures) { return ProbeConsumer(false, started, captures); }
static void LogPhotoCacheTest(const char* phase, bool success, ULONGLONG started,
    unsigned captures, const char* previousName, const char* observedName)
{
    Check(started <= world.nowMs && captures == world.captures, "logs retain the actual capture count");
    if (std::strcmp(phase, "begin") == 0)
        Check(photoTestBindAttempts == 0 && photoTestBindingTransitions == 0 && photoTestPlainCard,
            "baseline starts with fresh counters and no portrait material binding");
    world.logs.push_back({ photoTestControl, phase, success, captures, previousName, observedName, photoTestPlainCard });
    if (world.interruptAfterLog == std::string(photoTestControl) + "/" + phase) world.playerAvailable = false;
}
static void RequestOwnedPedCleanup(Ped subject)
{
    Check(subject == kSubject && world.activeHandles.empty(),
        "caller releases the final handle before requesting cleanup of its own provisional ped");
    ++world.cleanupRequests;
}
namespace ENTITY
{
static void SET_ENTITY_VISIBLE(Ped subject, bool visible)
{
    Check(subject == kSubject && !visible, "caller hides only its provisional subject");
    ++world.visibilityChanges;
}
}

#include "photo_self_test_under_test.h"

static void Reset()
{
    world = TestWorld();
    C = PortraitState();
    lastPhotoStage = "none";
    lastStartFailure = ContractStartFailure::None;
    photoTestPlainCard = false;
    photoTestControl = "none";
    photoTestBindAttempts = photoTestBindingTransitions = 99;
}
static void RunCallerAndCheck(bool interrupted = false)
{
    Check(RunPhotoTestAsCaller(kSubject) == 0, "diagnostics never issue a bounty target");
    Check(world.activeHandles.empty() && world.handlesCreated == world.handlesReleased,
        "real caller releases every positive or pending handle on all exits");
    Check(world.cleanupRequests == 1 && world.visibilityChanges == 1 && !C.def,
        "caller clears temporary metadata and queues cleanup of its one provisional subject");
    Check(!photoTestPlainCard && std::strcmp(photoTestControl, "none") == 0,
        "scoped diagnostic controls reset after successful, failed, or interrupted runs");
    Check(lastStartFailure == (interrupted ? ContractStartFailure::Interrupted : ContractStartFailure::PhotoDiagnosticComplete),
        "interruption stays distinct from a completed diagnostic failure without rerolling");
}
static std::vector<std::string> PhaseKeys()
{
    std::vector<std::string> result;
    for (const LogEntry& entry : world.logs) result.push_back(entry.control + "/" + entry.phase);
    return result;
}
static std::vector<std::string> ExpectedPhases()
{
    std::vector<std::string> result = { "baseline/begin", "baseline/initial", "baseline/reopen_without_write", "baseline/capture" };
    for (unsigned i = 1; i < 5; ++i)
        for (const char* phase : { "consumer", "reopen_without_write", "capture" })
            result.push_back(std::string(kControls[i]) + "/" + phase);
    result.push_back("portrait_inspection/complete");
    return result;
}
static void CheckStoppedAt(const std::string& key)
{
    auto expected = ExpectedPhases();
    while (!expected.empty() && expected.back() != key) expected.pop_back();
    Check(!expected.empty() && PhaseKeys() == expected, "failure or interruption prevents every later diagnostic phase");
}
static void TestSuccessfulSequence()
{
    Reset();
    RunCallerAndCheck();
    Check(PhaseKeys() == ExpectedPhases(), "baseline and all four controls log their checkpoints in order");
    Check(world.captures == 6 && world.explicitProbes == 5 && world.consumers == 4 &&
        world.objects == 2 && world.inspections == 2 && world.handlesReleased == 11,
        "a successful run uses six captures, five reopened handles, and four isolated consumers");
    std::vector<std::string> expected = { "baseline/capture", "baseline/reopen", "baseline/capture" };
    for (unsigned i = 1; i < 5; ++i)
    {
        expected.push_back(std::string(kControls[i]) + (i % 2 ? "/object" : "/inspection"));
        expected.push_back(std::string(kControls[i]) + "/reopen");
        expected.push_back(std::string(kControls[i]) + "/capture");
    }
    Check(world.operations == expected, "each no-write reopen precedes its next capture after consumer retirement");
    unsigned checkpoint = 0;
    for (const LogEntry& log : world.logs)
    {
        if (log.phase == "capture")
        {
            ++checkpoint;
            Check(log.previous == "reopened_" + std::to_string(checkpoint),
                "capture log keeps a copy of the last observed name after PhotographPed clears scratch state");
        }
    }
}
static void TestEveryCaptureFailure()
{
    for (unsigned i = 0; i < 6; ++i)
    {
        for (bool pending : { false, true })
        {
            Reset();
            world.captureOutcomes[i] = { false, pending };
            RunCallerAndCheck();
            const std::string key = i == 0 ? "baseline/initial" : std::string(kControls[i - 1]) + "/capture";
            CheckStoppedAt(key);
            Check(world.captures == i + 1 && !world.logs.back().success,
                "each failed capture stops before another consumer or rewrite regardless of pending ownership");
        }
    }
}
static void TestEveryReopenFailure()
{
    for (unsigned i = 0; i < 5; ++i)
    {
        for (LookupOutcome outcome : { LookupOutcome::Unavailable, LookupOutcome::Pending })
        {
            Reset();
            world.lookupOutcomes[i] = outcome;
            RunCallerAndCheck();
            CheckStoppedAt(std::string(kControls[i]) + "/reopen_without_write");
            Check(world.explicitProbes == i + 1 && world.captures == i + 1 && !world.logs.back().success,
                "each failed reopen stops without another write or an unrelated fallback");
            Check(world.nowMs - 1000 >= Card::kPhotoNameMs && world.nowMs - 1000 < Card::kPhotoNameMs + 16,
                "unavailable and pending reopen phases stop at the production timeout");
        }
    }
}
static void TestEveryConsumerFailure()
{
    for (unsigned i = 0; i < 4; ++i)
    {
        Reset();
        world.consumerOutcomes[i] = false;
        RunCallerAndCheck();
        CheckStoppedAt(std::string(kControls[i + 1]) + "/consumer");
        Check(world.captures == i + 2 && world.explicitProbes == i + 1 && world.consumers == i + 1 &&
            !world.logs.back().success, "failed consumer retirement prevents both reopen and another capture");
    }
}
static void TestLatestConsumerName()
{
    for (unsigned i = 0; i < 4; ++i)
    {
        Reset();
        world.acceptedNames[i] = "changed_during_consumer";
        world.lookupOutcomes[i + 1] = LookupOutcome::Unavailable;
        RunCallerAndCheck();
        Check(world.logs.back().previous == world.acceptedNames[i],
            "reopen logs preserve the latest accepted name after a consumer changes it");
    }
}
static void TestEveryInterruption()
{
    for (const std::string& key : ExpectedPhases())
    {
        Reset();
        world.interruptAfterLog = key;
        RunCallerAndCheck(true);
        CheckStoppedAt(key);
    }
    for (unsigned i = 1; i <= 4; ++i)
    {
        Reset();
        world.interruptInConsumer = i;
        RunCallerAndCheck(true);
        CheckStoppedAt(std::string(kControls[i]) + "/consumer");
    }
    Reset();
    world.interruptInLookup = true;
    world.lookupOutcomes[0] = LookupOutcome::Pending;
    RunCallerAndCheck(true);
    CheckStoppedAt("baseline/reopen_without_write");
    Check(world.frames == 1 && world.captures == 1, "player interruption ends pending reopen polling on its next frame");
    Reset();
    world.playerAvailable = false;
    RunCallerAndCheck(true);
    CheckStoppedAt("baseline/begin");
    Check(world.captures == 0 && world.handlesCreated == 0, "unavailable player prevents all native capture work");
}
int main()
{
    TestSuccessfulSequence();
    TestEveryCaptureFailure();
    TestEveryReopenFailure();
    TestEveryConsumerFailure();
    TestLatestConsumerName();
    TestEveryInterruption();
    std::printf("All %u photo self-test driver checks passed (actual production driver/helper/caller).\n", checks);
}
