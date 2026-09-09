#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

using Ped = int;
using Entity = int;
using Object = int;
using Hash = unsigned;
using DWORD = std::uint32_t;
using ULONGLONG = unsigned long long;
struct Vector3 { float x = 0, y = 0, z = 0; };
constexpr Ped kPlayer = 42, kTarget = 43;
constexpr Object kCard = 100;
static Ped pedMe = kPlayer;
static struct Contract { Ped target = kTarget; bool photoTextureValid = true; } C;
static unsigned checks = 0;
static void Check(bool condition, const char* message)
{
    ++checks;
    if (!condition) { std::fprintf(stderr, "FAILED: %s\n", message); std::exit(EXIT_FAILURE); }
}
constexpr Hash Joaat(const char* text)
{
    Hash hash = 0;
    for (; *text; ++text)
    {
        unsigned c = static_cast<unsigned char>(*text);
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        hash += c; hash += hash << 10; hash ^= hash >> 6;
    }
    hash += hash << 3; hash ^= hash >> 11; hash += hash << 15;
    return hash;
}
struct TraceRecord
{
    std::string stage, detail;
    Hash model = 0;
    Ped target = 0;
    bool hasPoint = false;
    Vector3 point;
    ULONGLONG now = 0;
};
static struct World
{
    Ped currentPlayer = kPlayer;
    Object card = kCard, primaryItem = 0;
    bool playerExists = true, playerDead = false, cardExists = true, itemRunning = false;
    bool paused = false, faded = false, inspection = true, visible = true, projected = true;
    Entity attachedTo = kPlayer;
    Hash model = 1234, item = 5678, state = 9012;
    Vector3 pedPosition = {1, 2, 3}, pedRotation = {4, 5, 6};
    Vector3 cardPosition = {7, 8, 9}, cardRotation = {10, 11, 12};
    Vector3 gameplayPosition = {13, 14, 15}, gameplayRotation = {16, 17, 18};
    Vector3 renderedPosition = {19, 20, 21}, renderedRotation = {22, 23, 24};
    ULONGLONG now = 1000;
    unsigned snapshotReads = 0, taskReads = 0, slotReads = 0, waits = 0;
    unsigned startTaskOnFrame = 0;
    std::vector<std::string> events;
    std::vector<TraceRecord> records;
} w;
namespace ENTITY
{
static bool DOES_ENTITY_EXIST(Entity entity)
{
    return (entity == kPlayer && w.playerExists) || (entity == w.card && w.cardExists);
}
static Vector3 GET_ENTITY_COORDS(Entity entity, bool p1, bool p2)
{
    Check(p1 && !p2 && DOES_ENTITY_EXIST(entity), "coordinates belong to an admitted live entity");
    ++w.snapshotReads; return entity == kPlayer ? w.pedPosition : w.cardPosition;
}
static Vector3 GET_ENTITY_ROTATION(Entity entity, int order)
{
    Check(order == 2 && DOES_ENTITY_EXIST(entity), "rotation belongs to an admitted entity");
    ++w.snapshotReads; return entity == kPlayer ? w.pedRotation : w.cardRotation;
}
static Entity GET_ENTITY_ATTACHED_TO(Entity entity)
{
    Check(entity == w.card && w.cardExists, "attachment reads only the admitted card");
    ++w.snapshotReads; return w.attachedTo;
}
static bool IS_ENTITY_VISIBLE(Entity entity)
{
    Check(entity == w.card && w.cardExists, "visibility reads only the admitted card");
    ++w.snapshotReads; return w.visible;
}
static Hash GET_ENTITY_MODEL(Entity entity)
{
    Check(entity == w.card && w.cardExists, "model reads only the admitted card");
    ++w.snapshotReads; return w.model;
}
}
namespace PED
{
static bool IS_PED_DEAD_OR_DYING(Ped ped, bool includeDying)
{
    Check(ped == kPlayer && w.playerExists && includeDying, "readiness checks the living inspector");
    return w.playerDead;
}
}
namespace PLAYER { static Ped PLAYER_PED_ID() { return w.currentPlayer; } }
namespace TASK
{
static bool IS_PED_RUNNING_TASK_ITEM_INTERACTION(Ped ped)
{
    Check(ped == kPlayer && w.playerExists && !w.playerDead, "ownership reads only the living current inspector");
    ++w.taskReads; return w.itemRunning;
}
static Entity _GET_ITEM_INTERACTION_ENTITY_FROM_PED(Ped ped, Hash slot)
{
    Check(ped == kPlayer && slot == Joaat("primaryItem") && w.itemRunning,
        "ownership checks the primary item of the running task");
    ++w.slotReads; return w.primaryItem;
}
static Hash GET_ITEM_INTERACTION_ITEM_ID(Ped ped)
{
    Check(ped == kPlayer, "item ID belongs to the inspector");
    ++w.snapshotReads; return w.item;
}
static Hash GET_ITEM_INTERACTION_STATE(Ped ped)
{
    Check(ped == kPlayer, "item state belongs to the inspector");
    ++w.snapshotReads; return w.state;
}
static bool IS_PED_RUNNING_INSPECTION_TASK(Ped ped)
{
    Check(ped == kPlayer, "inspection state belongs to the inspector");
    ++w.snapshotReads; return w.inspection;
}
}
namespace CAMERA
{
static bool IS_SCREEN_FADED_OUT() { return w.faded; }
static Vector3 GET_GAMEPLAY_CAM_COORD() { ++w.snapshotReads; return w.gameplayPosition; }
static Vector3 GET_GAMEPLAY_CAM_ROT(int order)
{
    Check(order == 2, "gameplay-camera rotation uses order two");
    ++w.snapshotReads; return w.gameplayRotation;
}
static float GET_GAMEPLAY_CAM_FOV() { ++w.snapshotReads; return 45.0f; }
static Vector3 GET_FINAL_RENDERED_CAM_COORD() { ++w.snapshotReads; return w.renderedPosition; }
static Vector3 GET_FINAL_RENDERED_CAM_ROT(int order)
{
    Check(order == 2, "rendered-camera rotation uses order two");
    ++w.snapshotReads; return w.renderedRotation;
}
static float GET_FINAL_RENDERED_CAM_FOV() { ++w.snapshotReads; return 50.0f; }
}
namespace GRAPHICS
{
static bool GET_SCREEN_COORD_FROM_WORLD_COORD(Vector3 point, float* x, float* y)
{
    Check(point.x == w.cardPosition.x && point.y == w.cardPosition.y && point.z == w.cardPosition.z,
        "projection samples the actual card position");
    ++w.snapshotReads; *x = 0.25f; *y = 0.75f; return w.projected;
}
}
namespace HUD { static bool IS_PAUSE_MENU_ACTIVE() { return w.paused; } }
#include "../rdr2 scripting environment/samples/Pools/startup_trace.h"
static void RecordTrace(const StartupTrace::Event& event)
{
    w.records.push_back({event.stage, event.detail, event.model, event.ped, event.hasPoint, event.point, w.now});
    w.events.push_back(event.stage);
}
static ULONGLONG GetTickCount64() { return w.now; }
static void MaintainOwnedPedCleanup() { w.events.push_back("cleanup"); }
static void SetRuntimePaused(bool) {}
static void MaintainPortraitAndCard() { w.events.push_back("portrait"); }
static void UpdateCard() { w.events.push_back("card_update"); }
static void WAIT(int delay)
{
    Check(delay == 0, "inspection tracing keeps the normal per-frame yield");
    ++w.waits; w.now += 16; w.events.push_back("wait");
    if (w.startTaskOnFrame && w.waits >= w.startTaskOnFrame)
    {
        w.itemRunning = true; w.primaryItem = w.card;
    }
}
// Intentionally no camera/collision/pose/task mutation shims: any such call fails compilation.
#include "card_inspection_camera_under_test.h"

static void Reset()
{
    w = {}; Cd = {}; cardInspectionTrace = {}; pedMe = kPlayer; C = {};
    StartupTrace::sink = RecordTrace;
    Cd.obj = w.card; Cd.ownsObj = true; Cd.inspectingPed = kPlayer;
}
static void TraceAt(ULONGLONG now) { w.now = now; TraceCardInspection(); }
static bool HasDetail(const TraceRecord& record, const char* field)
{
    return record.detail.find(field) != std::string::npos;
}
static void PendingOwnedAndMatchingTasks()
{
    Reset(); TraceAt(1000);
    Check(w.records.size() == 1 && w.records[0].stage == "card_inspection_sample" && w.slotReads == 0,
        "pending owned card samples before its native task starts");
    const TraceRecord& record = w.records[0];
    Check(record.model == w.model && record.target == kTarget && record.hasPoint &&
        record.point.x == w.cardPosition.x, "snapshot records the actual card model/location and target");
    for (const char* field : {"card=100;", "inspector=42;", "sample=0;", "elapsed=0;", "owned=1;",
        "examining=0;", "item=0000162E;", "state=00002334;", "inspection=1;", "attachedTo=42;",
        "visible=1;", "textureValid=1;", "pedPos=1.000,2.000,3.000;", "pedRot=4.00,5.00,6.00;",
        "cardRot=10.00,11.00,12.00;", "gamePos=13.000,14.000,15.000;", "gameRot=16.00,17.00,18.00;",
        "gameFov=45.00;", "renderPos=19.000,20.000,21.000;", "renderRot=22.00,23.00,24.00;",
        "renderFov=50.00;", "projected=1;", "screen=0.250,0.750"})
        Check(HasDetail(record, field), field);
    for (bool owned : {true, false})
    {
        Reset(); Cd.ownsObj = owned; Cd.examining = true;
        w.itemRunning = true; w.primaryItem = w.card;
        Check(OwnCardTaskRunning(), "production ownership recognizes the matching held prop");
        TraceAt(1000); TraceAt(1250);
        Check(w.records.size() == 2, "owned and native fallback cards both receive sampled observations");
    }
    Reset(); Cd.inHand = true; TraceAt(1000);
    Check(w.records.size() == 1, "reused handoff card traces when it becomes the pending inspection");
    w.visible = w.inspection = w.projected = C.photoTextureValid = false; w.attachedTo = 0;
    TraceAt(1250);
    for (const char* field : {"visible=0;", "inspection=0;", "projected=0;", "textureValid=0;", "attachedTo=0;"})
        Check(HasDetail(w.records.back(), field), "unattached, hidden or unprojectable cards retain their negative evidence");

    Reset();
    const float huge = (std::numeric_limits<float>::max)();
    w.cardPosition = w.cardRotation = w.pedPosition = w.pedRotation = {huge, huge, huge};
    w.gameplayPosition = w.gameplayRotation = w.renderedPosition = w.renderedRotation = {huge, huge, huge};
    TraceAt(1000);
    Check(w.records.size() == 1 && w.records[0].detail.size() <= 1023 && HasDetail(w.records[0], "card=100;"),
        "extreme finite native coordinates produce bounded diagnostics without a formatting abort");
}
static void SamplingIsThrottledAndBounded()
{
    Reset(); TraceAt(1000);
    const unsigned reads = w.snapshotReads;
    TraceAt(1000); TraceAt(1100); TraceAt(1249);
    Check(w.records.size() == 1 && w.snapshotReads == reads, "subinterval ticks perform no native snapshot reads");
    TraceAt(1250);
    Check(w.records.size() == 2 && w.snapshotReads > reads, "sampling resumes at the quarter-second boundary");
    TraceAt(15000); TraceAt(16000);
    Check(w.records.back().now == 16000, "the final fifteen-second boundary still permits a sample");
    const std::size_t samples = w.records.size();
    const unsigned afterBoundaryReads = w.snapshotReads;
    TraceAt(16250); TraceAt(60000);
    Check(w.records.size() == samples && w.snapshotReads == afterBoundaryReads,
        "sampling stops after fifteen seconds without restarting the inspection clock");
    Reset();
    for (ULONGLONG now = 1000; now <= 61000; now += 10) TraceAt(now);
    Check(w.records.size() <= 64 && w.records.front().now == 1000 && w.records.back().now <= 16000,
        "a long inspection remains bounded under dense gameplay ticks");
    for (std::size_t i = 1; i < w.records.size(); ++i)
        Check(w.records[i].now - w.records[i - 1].now >= 250, "dense frames cannot produce a trace burst");
}
static void EndAndReplacementAreIsolated()
{
    Reset(); TraceAt(1000);
    const unsigned reads = w.snapshotReads;
    Cd = {}; TraceAt(1001); TraceAt(1002);
    Check(w.records.size() == 2 && w.records[1].stage == "card_inspection_end" && w.snapshotReads == reads,
        "put-away emits one end event without reading the retired entity");
    Check(HasDetail(w.records[1], "card=100;") && cardInspectionTrace.object == 0,
        "end event identifies the old card and clears its sampling state");
    w.card = kCard + 1; Cd.obj = w.card; Cd.ownsObj = true; Cd.inspectingPed = kPlayer;
    TraceAt(1003);
    Check(w.records.size() == 3 && w.records.back().stage == "card_inspection_sample" &&
        HasDetail(w.records.back(), "card=101;"), "replacement receives an immediate fresh sample");
    w.card = kCard + 2; Cd.obj = w.card; TraceAt(1004);
    Check(w.records.size() == 5 && w.records[3].stage == "card_inspection_end" &&
        HasDetail(w.records[3], "card=101;") && HasDetail(w.records[3], "reason=replaced") &&
        HasDetail(w.records[4], "card=102;") && HasDetail(w.records[4], "sample=0;"),
        "replacement without a retired frame closes the old card and independently starts the next");
}
static void RefusedOwnershipAndReadiness()
{
    for (int invalid = 0; invalid < 11; ++invalid)
    {
        for (bool previouslyActive : {false, true})
        {
            Reset();
            if (previouslyActive) TraceAt(1000);
            const unsigned reads = w.snapshotReads;
            switch (invalid)
            {
            case 0: Cd.obj = 0; break;
            case 1: w.cardExists = false; break;
            case 2: Cd.inspectingPed = 0; Cd.inHand = true; break;
            case 3: Cd.inspectingPed = kPlayer + 1; break;
            case 4: w.playerExists = false; break;
            case 5: w.playerDead = true; break;
            case 6: w.currentPlayer = kPlayer + 1; break;
            case 7: Cd.ownsObj = false; break;
            case 8: Cd.examining = true; break;
            case 9: w.itemRunning = true; w.primaryItem = kCard + 1; break;
            case 10: Cd.ownsObj = false; w.itemRunning = true; w.primaryItem = kCard + 1; break;
            }
            TraceAt(1250); TraceAt(1500);
            Check(w.snapshotReads == reads && w.records.size() == (previouslyActive ? 2u : 0u),
                "missing, retired, foreign-task or invalid-player state produces no native snapshots");
            Check(cardInspectionTrace.object == 0, "failed admission clears the previous trace runtime");
            if (previouslyActive) Check(w.records.back().stage == "card_inspection_end", "failed admission closes an active trace once");
        }
    }
}
static void ProductionWaitAndFrameIntegration()
{
    Reset(); w.startTaskOnFrame = 2;
    Check(WaitUntil(200, [] { return OwnCardTaskRunning(); }), "real startup wait accepts the delayed matching task");
    Check(w.waits == 2 && w.records.size() == 1, "delayed startup records one throttled observation");
    Check(w.events == std::vector<std::string>{
        "cleanup", "card_inspection_sample", "portrait", "wait",
        "cleanup", "portrait", "wait", "cleanup", "portrait"},
        "real WaitUntil traces before portrait maintenance and retains predicate/yield order");
    Reset(); RunProductionCardFrameTail();
    Check(w.events == std::vector<std::string>{"card_inspection_sample", "card_update", "wait"},
        "actual frame integration preserves card tracing, update and yield order");
    Reset(); TraceAt(1000); w.paused = w.faded = true;
    TraceAt(16000); TraceAt(17000);
    Check(w.records.size() == 2 && w.records.back().now == 16000,
        "pauses and fades cannot extend the fixed wall-clock sampling budget");
}
int main()
{
    PendingOwnedAndMatchingTasks(); SamplingIsThrottledAndBounded(); EndAndReplacementAreIsolated();
    RefusedOwnershipAndReadiness(); ProductionWaitAndFrameIntegration();
    std::printf("Card inspection trace: %u checks passed.\n", checks);
}
