#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <vector>
using Ped = int; using Entity = int; using BOOL = int; using DWORD = unsigned;
struct Vector3 { float x, y, z; Vector3(float a=0, float b=0, float c=0):x(a),y(b),z(c) {} };
static unsigned checks = 0;
static void Check(bool value, const char* what) { ++checks; if (!value) { std::fprintf(stderr,"FAILED: %s\n",what); std::exit(1); } }
struct World {
    unsigned now = 1000, loadAt = 0, waits = 0, cancelsAt = 999999;
    bool collision = true, nav = true, safe = true, outside = true, groundOk = true, water = false, hit = false;
    bool sceneActive = false, startOk = true, occupied = false; int starts = 0, stops = 0, probes = 0;
    int interior = 0, probeStatus = 2; float safeDx = 0, safeDz = 0, ground = 10, slope = 1, waterHeight = 0;
    int safeQueries = 0, sceneQueries = 0, collisionRequests = 0, requiredRegions = 0;
    float laterSafeDx = 0;
    Ped bodyIgnore = 0, probeIgnore = 0;
} w;
static bool PlayerAvailable() { return w.waits < w.cancelsAt; }
template<typename Pred> static bool WaitUntil(DWORD timeout, Pred pred) {
    const auto start = w.now;
    while (PlayerAvailable()) { if (pred()) return true; if(w.now-start>=timeout)return false; w.now+=16; ++w.waits; }
    return false;
}
namespace ENTITY { static bool HAS_COLLISION_LOADED_AROUND_POSITION(Vector3) { return w.collision && w.now>=w.loadAt; } }
namespace PATH {
static bool IS_NAVMESH_LOADED_IN_AREA(Vector3,Vector3) { return w.nav && w.now>=w.loadAt; }
static bool GET_SAFE_COORD_FOR_PED(Vector3 p,bool,Vector3* out,int) {
    ++w.safeQueries; *out=p; out->x+=w.safeDx+(w.safeQueries>1?w.laterSafeDx:0); out->z+=w.safeDz; return w.safe;
}
static void ADD_NAVMESH_REQUIRED_REGION(float,float,float) { ++w.requiredRegions; }
}
namespace MISC {
static bool GET_GROUND_Z_AND_NORMAL_FOR_3D_COORD(Vector3,float* ground,Vector3* normal) { *ground=w.ground; normal->z=w.slope; return w.groundOk; }
static bool IS_POSITION_OCCUPIED(Vector3,float,bool,bool,bool,bool,bool,Ped ignore,bool) { w.bodyIgnore=ignore; return w.occupied; }
}
namespace INTERIOR {
static bool IS_COLLISION_MARKED_OUTSIDE(Vector3) { return w.outside; }
static int GET_INTERIOR_FROM_COLLISION(Vector3) { return w.interior; }
}
namespace WATER { static bool GET_WATER_HEIGHT(Vector3,float* out) { *out=w.waterHeight; return w.water; } }
namespace SHAPETEST {
static int START_EXPENSIVE_SYNCHRONOUS_SHAPE_TEST_LOS_PROBE(Vector3 a,Vector3 b,int flags,Entity ignore,int) {
    Check(b.z>a.z && flags==87,"clearance uses upright source-backed mask87 probes"); w.probeIgnore=ignore; return ++w.probes;
}
static int GET_SHAPE_TEST_RESULT(int,BOOL* hit,Vector3*,Vector3*,Entity*) { *hit=w.hit; return w.probeStatus; }
}
namespace STREAMING {
static bool IS_LOAD_SCENE_ACTIVE() { ++w.sceneQueries; return w.sceneActive; }
static bool LOAD_SCENE_START_SPHERE(Vector3,float,int) { Check(!w.sceneActive,"never replace a foreign scene load"); ++w.starts; w.sceneActive=w.startOk; return w.startOk; }
static void LOAD_SCENE_STOP() { Check(w.starts==1 && w.startOk,"only stop a successfully owned scene"); ++w.stops; w.sceneActive=false; }
static void REQUEST_COLLISION_AT_COORD(Vector3) { ++w.collisionRequests; }
}
#include "../rdr2 scripting environment/samples/Pools/routine_spawn.h"
#include "../rdr2 scripting environment/samples/Pools/routine_locations.h"
static bool Validate() { Vector3 out; return RoutineSpawn::Validate({100,200,10},8,2.5f,{100,200,10},out); }
static bool SamePoint(Vector3 a,Vector3 b) { return a.x==b.x && a.y==b.y && a.z==b.z; }
struct TraceSnapshot {
    StartupTrace::Event event;
    int starts, stops, collisionRequests, requiredRegions;
};
static std::vector<TraceSnapshot> trace;
static void CaptureTrace(const StartupTrace::Event& event) {
    trace.push_back({event,w.starts,w.stops,w.collisionRequests,w.requiredRegions});
}
static void ResetTraceWorld() { w={}; trace.clear(); StartupTrace::sink=CaptureTrace; }
static void CheckTraceStages(std::initializer_list<const char*> expected,const Vector3& point) {
    Check(trace.size()==expected.size(),"trace contains exactly one event for each requested preparation boundary");
    std::size_t index=0;
    for(const char* stage:expected) {
        const auto& event=trace[index++].event;
        Check(std::strcmp(event.stage,stage)==0,"preparation trace events retain their execution order");
        Check(event.hasPoint && SamePoint(event.point,point),"each streaming breadcrumb retains the requested anchor");
    }
}
static void OptionalTraceSinkCopiesPoints() {
    Check(StartupTrace::sink==nullptr,"startup tracing is disabled by default");
    StartupTrace::Record("no_sink");
    Check(trace.empty(),"recording with no sink is a harmless no-op");
    ResetTraceWorld();
    Vector3 point(100,200,10);
    StartupTrace::Record("point_copy",0x12345u,42,&point,3,"before_request");
    point=Vector3(900,800,700);
    StartupTrace::Record("defaults");
    Check(trace.size()==2,"installed optional sink receives each explicit record once");
    const auto& copied=trace[0].event;
    Check(copied.hasPoint && SamePoint(copied.point,{100,200,10}),
        "captured event owns a coordinate copy that survives changes to its source");
    Check(copied.model==0x12345u && copied.ped==42 && copied.freePeds==3 &&
        std::strcmp(copied.stage,"point_copy")==0 && std::strcmp(copied.detail,"before_request")==0,
        "sink receives the supplied stage, model, actor, pool count and detail");
    const auto& defaults=trace[1].event;
    Check(!defaults.hasPoint && SamePoint(defaults.point,{}) && defaults.model==0 && defaults.ped==0 &&
        defaults.freePeds==-1 && defaults.detail[0]=='\0',"omitted trace values are explicit and initialized");
    StartupTrace::sink=nullptr;
    StartupTrace::Record("removed_sink");
    Check(trace.size()==2,"removing the sink immediately disables further records");
    trace.clear();
}
static void SceneTraceBracketsOwnedNativeCalls() {
    const Vector3 anchor(100,200,10);
    for(int outcome=0;outcome<3;++outcome) {
        ResetTraceWorld();
        w.loadAt=1100;
        if(outcome==1)w.collision=false;
        if(outcome==2)w.cancelsAt=1;
        Check(RoutineSpawn::EnsureLoaded(anchor)==(outcome==0),
            "owned-loader trace preserves success, timeout and interruption results");
        CheckTraceStages({"scene_start_begin","scene_start_owned","nav_region_request","collision_wait_begin",
            outcome==0?"collision_wait_ready":"collision_wait_failed","scene_stop_begin","scene_stopped"},anchor);
        Check(trace[0].starts==0 && trace[1].starts==1,
            "scene-start breadcrumbs bracket the actual native return");
        Check(trace[2].requiredRegions==0 && trace[3].requiredRegions==1 && trace[3].collisionRequests==0,
            "nav request is traced before its native and collision wait is traced before polling");
        Check(trace[4].collisionRequests>0 && trace[4].stops==0,
            "collision result is recorded after polling and before owned scene cleanup");
        Check(trace[5].stops==0 && trace[6].stops==1 && w.starts==1 && w.stops==1 && !w.sceneActive,
            "owned scene cleanup occurs exactly once between stop breadcrumbs on every exit");
    }
    ResetTraceWorld();w.loadAt=1100;w.startOk=false;
    Check(RoutineSpawn::EnsureLoaded(anchor),"refused scene load can still complete through passive collision streaming");
    CheckTraceStages({"scene_start_begin","scene_start_refused","nav_region_request","collision_wait_begin",
        "collision_wait_ready"},anchor);
    Check(trace[0].starts==0 && trace[1].starts==1 && w.starts==1 && w.stops==0,
        "refused start records its actual return without inventing an owned cleanup");
    StartupTrace::sink=nullptr;
}
static void SceneTracePreservesForeignAndLoadedPaths() {
    const Vector3 anchor(100,200,10);
    for(bool ready:{true,false}) {
        ResetTraceWorld();w.sceneActive=true;w.loadAt=1100;w.collision=ready;
        Check(RoutineSpawn::EnsureLoaded(anchor)==ready,"foreign loader keeps its existing passive success or timeout result");
        CheckTraceStages({"nav_region_request","collision_wait_begin",ready?"collision_wait_ready":"collision_wait_failed"},anchor);
        Check(w.starts==0 && w.stops==0 && w.sceneActive,
            "foreign loader emits neither scene-start nor scene-stop records or native calls");
    }
    for(bool available:{true,false}) {
        ResetTraceWorld();w.sceneActive=true;if(!available)w.cancelsAt=0;
        Check(RoutineSpawn::EnsureLoaded(anchor)==available,"loaded trace fast path retains player availability handling");
        Check(trace.empty() && w.sceneQueries==0 && w.starts==0 && w.stops==0 &&
            w.requiredRegions==0 && w.collisionRequests==0,"already loaded destination emits no streaming breadcrumbs");
    }
    StartupTrace::sink=nullptr;
}
static void CheckStage(const char* stage) {
    Check(std::strcmp(RoutineSpawn::diagnostic.check,stage)==0,"failure diagnostic identifies the actual rejecting check");
}
static void AcceptedPointDoesNotRerollNavigation() {
    w={};
    const Vector3 anchor(100,200,10);
    Vector3 accepted,alternate;
    Check(RoutineSpawn::Validate(anchor,8,2.5f,anchor,accepted) && w.safeQueries==1,
        "initial candidate is selected and validated with one safe-coordinate query");
    w.laterSafeDx=3;
    Check(RoutineSpawn::ValidatePoint(anchor,8,2.5f,accepted,42),
        "accepted destination remains valid when another nav selection would move three metres");
    Check(w.safeQueries==1,"post-capture exact-point validation never repeats nav selection");
    Check(w.bodyIgnore==42 && w.probeIgnore==42,"exact-point occupancy and clearance ignore only the supplied target");
    Check(SamePoint(RoutineSpawn::diagnostic.candidate,accepted) && SamePoint(RoutineSpawn::diagnostic.projected,accepted),
        "successful exact-point diagnostics retain the accepted coordinate");
    CheckStage("ok");

    // Demonstrate the old failure trigger: the same selector can return a valid
    // neighbour more than the removed .5m identity limit from the accepted point.
    Check(RoutineSpawn::Validate(anchor,8,2.5f,accepted,alternate) && alternate.x-accepted.x==3,
        "repeat selection can return another valid point beyond the old half-metre identity check");
    Check(w.safeQueries==2,"counterfactual candidate selection actually performs its second nav query");
    Check(RoutineSpawn::ValidatePoint(anchor,8,2.5f,accepted) && w.safeQueries==2,
        "stored point still validates independently of the selector's changed answer");
}
static void AcceptedPointStillRejectsUnsafeWorldChanges() {
    const Vector3 anchor(100,200,10);
    const char* stages[]={"collision_or_nav_unloaded","collision_or_nav_unloaded","ground_unavailable",
        "ground_height_or_slope","ground_height_or_slope","interior","interior","water","occupied",
        "clearance_blocked","clearance_pending","anchor_bounds"};
    for(int failure=0;failure<12;++failure) {
        w={};
        Vector3 stored=anchor;
        if(failure==0)w.collision=false; if(failure==1)w.nav=false;
        if(failure==2)w.groundOk=false; if(failure==3)w.slope=.5f;
        if(failure==4)w.ground=10.5f; if(failure==5)w.outside=false;
        if(failure==6)w.interior=123; if(failure==7){w.water=true;w.waterHeight=10;}
        if(failure==8)w.occupied=true; if(failure==9)w.hit=true;
        if(failure==10)w.probeStatus=1; if(failure==11)stored.x+=20;
        Check(!RoutineSpawn::ValidatePoint(anchor,8,2.5f,stored),
            "stored point fails closed when streaming, ground, exterior, water, clearance or bounds change");
        Check(w.safeQueries==0,"unsafe exact point is rejected directly without searching for a replacement");
        CheckStage(stages[failure]);
        Check(SamePoint(RoutineSpawn::diagnostic.candidate,stored) && SamePoint(RoutineSpawn::diagnostic.projected,stored),
            "rejection diagnostics identify the stored point rather than a newly selected location");
        if(failure==3 || failure==4)Check(RoutineSpawn::diagnostic.ground==w.ground,
            "height/slope failure records the observed ground value");
    }
    const Vector3 zeroGround(100,200,0);
    for(float shift:{.35f,-.35f}) {
        w={};w.ground=shift;
        Check(RoutineSpawn::ValidatePoint(zeroGround,8,2.5f,zeroGround),
            "exact-point validation accepts ground drift at the 0.35m boundary");
        CheckStage("ok");
    }
    for(float shift:{.3501f,-.3501f}) {
        w={};w.ground=shift;
        Check(!RoutineSpawn::ValidatePoint(zeroGround,8,2.5f,zeroGround),
            "exact-point validation rejects ground drift beyond 0.35m in either direction");
        CheckStage("ground_height_or_slope");
    }
    w={};w.ground=10.5f;
    Check(Validate(),"initial candidate keeps its existing one-metre ground-projection allowance");
    Check(!RoutineSpawn::ValidatePoint(anchor,8,2.5f,anchor),
        "stored point uses stricter ground identity without weakening initial surface safety");
    CheckStage("ground_height_or_slope");
}
static void AlreadyLoadedPointNeverTouchesSceneLoader() {
    const Vector3 anchor(100,200,10);
    w={};w.sceneActive=true;
    Check(RoutineSpawn::EnsureLoaded(anchor),"already loaded destination succeeds while a foreign scene request is active");
    Check(w.waits==0 && w.sceneQueries==0 && w.starts==0 && w.stops==0 && w.collisionRequests==0 && w.requiredRegions==0 && w.sceneActive,
        "loaded destination makes no scene, collision-request, nav-region or wait calls");
    Vector3 out;
    Check(RoutineSpawn::Prepare(anchor,8,2.5f,7,out),"candidate preparation uses the already-loaded fast path");
    Check(w.sceneQueries==0 && w.starts==0 && w.stops==0 && w.collisionRequests==0 && w.requiredRegions==0 && w.sceneActive,
        "candidate preparation preserves the foreign loader when streaming is already ready");
    w={};w.sceneActive=true;w.cancelsAt=0;
    Check(!RoutineSpawn::EnsureLoaded(anchor),"loaded fast path still rejects an unavailable player");
    Check(w.sceneQueries==0 && w.starts==0 && w.stops==0 && w.requiredRegions==0 && w.sceneActive,
        "player-loss fast path does not touch the foreign scene");
    w={};w.sceneActive=true;w.loadAt=1100;
    Check(RoutineSpawn::EnsureLoaded(anchor),"unloaded location can become ready under an existing foreign loader");
    Check(w.waits>0 && w.collisionRequests>0 && w.requiredRegions==1 && w.starts==0 && w.stops==0 && w.sceneActive,
        "passive bounded loading requests local data without owning or stopping a foreign scene");
    w={};w.collision=false;
    Check(!RoutineSpawn::EnsureLoaded(anchor),"owned scene loading still has a finite failure path");
    CheckStage("stream_timeout_or_interrupted");
    Check(w.stops==1,"failed bounded loading releases only its own successfully started scene");
}
int main() {
    OptionalTraceSinkCopiesPoints();
    Check(Validate() && w.probes==5,"ground-level clear outdoor nav point accepted");
    for(int failure=0;failure<12;++failure) {
        w={};
        if(failure==0)w.collision=false; if(failure==1)w.nav=false;
        if(failure==2)w.safe=false; if(failure==3)w.safeDx=20;
        if(failure==4)w.safeDz=5; if(failure==5)w.ground=15;
        if(failure==6)w.groundOk=false; if(failure==7)w.slope=.5f;
        if(failure==8)w.outside=false; if(failure==9)w.interior=123;
        if(failure==10){w.water=true;w.waterHeight=10;} if(failure==11)w.hit=true;
        Check(!Validate(),"unloaded, unsafe, rooftop, indoor, steep, water or obstructed point rejected");
    }
    w={};w.probeStatus=1;Check(!Validate(),"unfinished collision probe fails closed");
    w={};w.occupied=true;Check(!Validate(),"occupied destination fails closed");
    w={};w.ground=11;w.safeDz=1;Check(!Validate(),"upper surface inside broad anchor tolerance is rejected");
    Vector3 out;
    w={};w.loadAt=1100;
    Check(RoutineSpawn::Prepare({100,200,10},8,2.5f,7,out) && w.waits>0 && w.stops==1,"bounded loading succeeds and releases owned scene");
    w={};w.collision=false;
    Check(!RoutineSpawn::Prepare({100,200,10},8,2.5f,7,out) && w.now<4016 && w.stops==1,"timeout releases owned scene within budget");
    w={};w.sceneActive=true;w.collision=false;
    Check(!RoutineSpawn::Prepare({100,200,10},8,2.5f,7,out) && w.starts==0 && w.stops==0 && w.sceneActive,"foreign scene survives failure");
    w={};w.loadAt=1100;w.cancelsAt=1;
    Check(!RoutineSpawn::Prepare({100,200,10},8,2.5f,7,out) && w.stops==1,"player interruption releases the owned request");
    w={};w.loadAt=1100;w.startOk=false;
    Check(RoutineSpawn::Prepare({100,200,10},8,2.5f,7,out) && w.stops==0,"passive collision recovery never releases refused scene ownership");
    w={};w.safeDx=50;
    Check(!RoutineSpawn::Prepare({100,200,10},8,2.5f,7,out) && w.probes==0,"bounded candidates never accept a distant nav projection");
    AcceptedPointDoesNotRerollNavigation();
    AcceptedPointStillRejectsUnsafeWorldChanges();
    AlreadyLoadedPointNeverTouchesSceneLoader();
    SceneTraceBracketsOwnedNativeCalls();
    SceneTracePreservesForeignAndLoadedPaths();
    using namespace RoutineData;
    for(const Town& town:kTowns) {
        unsigned kinds=0; bool fallback=false;
        for(int i=0;i<kLocationCount;++i) {
            const auto& place=kLocations[i]; if(place.town!=town.id || !place.enabled)continue;
            kinds|=1u<<static_cast<unsigned>(place.kind);
            fallback|=place.kind==PlaceKind::Rest && place.openMinute==place.closeMinute;
            const float dx=town.searchCenter.x-place.anchor.x,dy=town.searchCenter.y-place.anchor.y;
            Check(std::sqrt(dx*dx+dy*dy)+place.candidateRadius+place.wanderRadius<=town.searchRadius,"fixed search circle contains authored routine locations");
            Check(place.source[0] && place.candidateRadius>0 && place.wanderRadius>0 && place.maxHeightDelta<=2.5f,"location has provenance and bounded separate radii");
            for(int j=i+1;j<kLocationCount;++j)Check(std::strcmp(place.id,kLocations[j].id)!=0,"location ids unique");
        }
        Check(kinds==15 && fallback,"every town has all four phases and an all-day fallback");
    }
    std::printf("Routine spawn/catalog: %u checks passed.\n",checks);
}
