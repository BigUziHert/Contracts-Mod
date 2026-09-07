#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using Ped = int; using Entity = int; using BOOL = int; using DWORD = unsigned;
struct Vector3 { float x, y, z; Vector3(float a=0, float b=0, float c=0):x(a),y(b),z(c) {} };
static unsigned checks = 0;
static void Check(bool value, const char* what) { ++checks; if (!value) { std::fprintf(stderr,"FAILED: %s\n",what); std::exit(1); } }
struct World {
    unsigned now = 1000, loadAt = 0, waits = 0, cancelsAt = 999999;
    bool collision = true, nav = true, safe = true, outside = true, groundOk = true, water = false, hit = false;
    bool sceneActive = false, startOk = true, occupied = false; int starts = 0, stops = 0, probes = 0;
    int interior = 0, probeStatus = 2; float safeDx = 0, safeDz = 0, ground = 10, slope = 1, waterHeight = 0;
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
static bool GET_SAFE_COORD_FOR_PED(Vector3 p,bool,Vector3* out,int) { *out=p; out->x+=w.safeDx; out->z+=w.safeDz; return w.safe; }
static void ADD_NAVMESH_REQUIRED_REGION(float,float,float) {}
}
namespace MISC {
static bool GET_GROUND_Z_AND_NORMAL_FOR_3D_COORD(Vector3,float* ground,Vector3* normal) { *ground=w.ground; normal->z=w.slope; return w.groundOk; }
static bool IS_POSITION_OCCUPIED(Vector3,float,bool,bool,bool,bool,bool,Ped,bool) { return w.occupied; }
}
namespace INTERIOR {
static bool IS_COLLISION_MARKED_OUTSIDE(Vector3) { return w.outside; }
static int GET_INTERIOR_FROM_COLLISION(Vector3) { return w.interior; }
}
namespace WATER { static bool GET_WATER_HEIGHT(Vector3,float* out) { *out=w.waterHeight; return w.water; } }
namespace SHAPETEST {
static int START_EXPENSIVE_SYNCHRONOUS_SHAPE_TEST_LOS_PROBE(Vector3 a,Vector3 b,int flags,Entity,int) {
    Check(b.z>a.z && flags==87,"clearance uses upright source-backed mask87 probes"); return ++w.probes;
}
static int GET_SHAPE_TEST_RESULT(int,BOOL* hit,Vector3*,Vector3*,Entity*) { *hit=w.hit; return w.probeStatus; }
}
namespace STREAMING {
static bool IS_LOAD_SCENE_ACTIVE() { return w.sceneActive; }
static bool LOAD_SCENE_START_SPHERE(Vector3,float,int) { Check(!w.sceneActive,"never replace a foreign scene load"); ++w.starts; w.sceneActive=w.startOk; return w.startOk; }
static void LOAD_SCENE_STOP() { Check(w.starts==1 && w.startOk,"only stop a successfully owned scene"); ++w.stops; w.sceneActive=false; }
static void REQUEST_COLLISION_AT_COORD(Vector3) {}
}
#include "../rdr2 scripting environment/samples/Pools/routine_spawn.h"
#include "../rdr2 scripting environment/samples/Pools/routine_locations.h"
static bool Validate() { Vector3 out; return RoutineSpawn::Validate({100,200,10},8,2.5f,{100,200,10},out); }
int main() {
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
