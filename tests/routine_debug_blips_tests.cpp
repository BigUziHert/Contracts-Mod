#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <string>
#include <vector>

using Blip = int;
using Hash = unsigned;
struct Vector3 { float x = 0, y = 0, z = 0; };
#include "../rdr2 scripting environment/samples/Pools/routine_locations.h"
static unsigned checks = 0;
static void Check(bool value, const char* text)
{
    ++checks;
    if (!value) { std::fprintf(stderr,"FAILED: %s\n",text); std::exit(EXIT_FAILURE); }
}
static Hash joaat(const char* text)
{
    unsigned hash = 0;
    for (; *text; ++text) { hash += static_cast<unsigned char>(*text); hash += hash << 10; hash ^= hash >> 6; }
    hash += hash << 3; hash ^= hash >> 11; hash += hash << 15;
    return hash;
}
static const char* Literal(const char* text) { return text; }
struct MapBlip { Vector3 point; Hash style = 0, sprite = 0; float scale = 0; std::string label; bool foreign = false; };
struct World
{
    std::uint64_t now = 1000;
    int next = 1, creates = 0, moves = 0, removes = 0, styles = 0, names = 0;
    int failures = 0;
    bool missingReturn = false;
    std::map<Blip,MapBlip> blips;
} w;
static std::uint64_t GetTickCount64() { return w.now; }
namespace MAP
{
static bool DOES_BLIP_EXIST(Blip blip) { return w.blips.contains(blip); }
static Blip BLIP_ADD_FOR_COORDS(Hash style, Vector3 point)
{
    ++w.creates;
    if (w.failures > 0) { --w.failures; return w.missingReturn ? w.next++ : 0; }
    const Blip handle = w.next++;
    w.blips[handle] = {point,style};
    return handle;
}
static void SET_BLIP_COORDS(Blip blip, Vector3 point)
{
    Check(DOES_BLIP_EXIST(blip) && !w.blips.at(blip).foreign,"coordinate updates touch only retained debug handles");
    ++w.moves; w.blips.at(blip).point = point;
}
static void SET_BLIP_SPRITE(Blip blip, Hash sprite, bool)
{
    Check(DOES_BLIP_EXIST(blip),"missing allocation never receives sprite calls");
    ++w.styles; w.blips.at(blip).sprite = sprite;
}
static void SET_BLIP_SCALE(Blip blip, float scale) { w.blips.at(blip).scale = scale; }
static void _SET_BLIP_NAME(Blip blip, const char* label) { ++w.names; w.blips.at(blip).label = label; }
}
static void RemoveBlip(Blip& handle)
{
    if (handle && MAP::DOES_BLIP_EXIST(handle))
    {
        Check(!w.blips.at(handle).foreign,"debug cleanup never removes a gameplay/foreign blip");
        ++w.removes; w.blips.erase(handle);
    }
    handle = 0;
}
#include "../rdr2 scripting environment/samples/Pools/routine_debug_blips.h"

static void Reset()
{
    RoutineDebugBlips::Clear(); w = World{};
    w.blips[100000] = {{},0,0,1,"Existing hunt",true};
}
static Blip FindLabel(const char* text)
{
    for (const auto& entry : w.blips) if (entry.second.label == text) return entry.first;
    return 0;
}
static bool SamePoint(Vector3 a, Vector3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
static void CatalogCreationReuseAndToggleCleanup()
{
    Reset(); RoutineDebugBlips::Update(false,nullptr,nullptr);
    Check(w.creates==0 && w.removes==0,"disabled overlay starts without native allocation or cleanup");
    RoutineDebugBlips::Update(true,nullptr,nullptr);
    Check(w.creates==RoutineData::kLocationCount,"enabled overlay allocates all authored anchors once");
    for (const auto& location : RoutineData::kLocations)
    {
        if (!location.enabled) continue;
        const char* townName = nullptr;
        for (const auto& town : RoutineData::kTowns) if (town.id == location.town) townName = town.name;
        Check(townName!=nullptr,"every debug location belongs to an authored town");
        const auto label = std::string("Routine: ")+townName+" - "+location.name;
        const Blip blip = FindLabel(label.c_str());
        Check(blip!=0 && SamePoint(w.blips.at(blip).point,location.anchor),"catalog dot labels identify exact source anchor coordinates");
        Check(w.blips.at(blip).style==joaat("BLIP_STYLE_DEBUG_RED") && w.blips.at(blip).sprite==joaat("BLIP_AMBIENT_PED_SMALL"),
            "catalog markers use the source-defined always-visible red style and dot sprite");
    }
    const int styles = w.styles, names = w.names;
    for (int frame=0; frame<100; ++frame) { w.now+=16; RoutineDebugBlips::Update(true,nullptr,nullptr); }
    Check(w.creates==RoutineData::kLocationCount && w.moves==0 && w.styles==styles && w.names==names,
        "unchanged debug frame reuses handles without recreating, moving, restyling or renaming");
    RoutineDebugBlips::Update(false,nullptr,nullptr);
    Check(w.removes==RoutineData::kLocationCount && w.blips.size()==1 && w.blips.contains(100000),
        "disable clears precisely the owned overlay and leaves the existing hunt blip");
    const int removes = w.removes;
    RoutineDebugBlips::Update(false,nullptr,nullptr); RoutineDebugBlips::Clear();
    Check(w.removes==removes,"repeated disable and explicit clear are idempotent");
    RoutineDebugBlips::Update(true,nullptr,nullptr);
    Check(w.creates==2*RoutineData::kLocationCount,"reenabling builds a fresh overlay after cleanup");
}
static void ExactMarkersMoveWithoutRecreation()
{
    Reset();
    Vector3 spawn{1,2,3}, destination{4,5,6};
    RoutineDebugBlips::Update(true,&spawn,&destination);
    const Blip initial = FindLabel("Routine: Exact initial spawn"), current = FindLabel("Routine: Current destination");
    Check(initial && current && initial!=current,"exact spawn and validated destination have distinct labeled handles");
    Check(w.creates==RoutineData::kLocationCount+2 && SamePoint(w.blips.at(initial).point,spawn) && SamePoint(w.blips.at(current).point,destination),
        "exact points are separate from the catalog's candidate anchors");
    Check(w.blips.at(initial).scale==1.2f && w.blips.at(current).scale==1.5f,
        "exact spawn and destination markers remain visually distinguishable by scale");
    destination={7,8,9}; RoutineDebugBlips::Update(true,&spawn,&destination);
    Check(w.moves==1 && w.creates==RoutineData::kLocationCount+2 && SamePoint(w.blips.at(current).point,destination),
        "validated destination changes move the retained marker rather than allocate another");
    spawn={10,11,12}; RoutineDebugBlips::Update(true,&spawn,&destination);
    Check(w.moves==2 && SamePoint(w.blips.at(initial).point,spawn),"replacement contract updates exact initial spawn on the retained handle");
    RoutineDebugBlips::Update(true,&spawn,nullptr);
    Check(!w.blips.contains(current) && w.blips.contains(initial) && w.removes==1,
        "missing validated destination clears only its dynamic marker");
    RoutineDebugBlips::Update(true,nullptr,nullptr);
    Check(!w.blips.contains(initial) && w.blips.size()==static_cast<std::size_t>(RoutineData::kLocationCount+1),
        "ending a hunt removes exact points while the catalog overlay remains visible");
    spawn.x=std::numeric_limits<float>::quiet_NaN();
    const int creates = w.creates;
    RoutineDebugBlips::Update(true,&spawn,nullptr);
    Check(w.creates==creates,"invalid exact coordinates never create a map marker");
}
static void FailedCreationAndLostHandlesHaveBoundedRetries()
{
    for (bool nonzeroMissing : {false,true})
    {
        Reset(); w.failures=1; w.missingReturn=nonzeroMissing;
        RoutineDebugBlips::Update(true,nullptr,nullptr);
        Check(w.creates==RoutineData::kLocationCount && w.styles==RoutineData::kLocationCount-1,
            "failed zero or missing nonzero allocation gets no styling calls");
        w.now=2999; RoutineDebugBlips::Update(true,nullptr,nullptr);
        Check(w.creates==RoutineData::kLocationCount,"failed handle cannot be retried before the two-second deadline");
        w.now=3000; RoutineDebugBlips::Update(true,nullptr,nullptr);
        Check(w.creates==RoutineData::kLocationCount+1 && w.styles==RoutineData::kLocationCount,
            "only the failed marker retries when its deadline arrives");
    }
    Reset(); w.failures=10000;
    for (std::uint64_t time : {1000u,3000u,5000u,7000u,60000u})
    { w.now=time; RoutineDebugBlips::Update(true,nullptr,nullptr); }
    Check(w.creates==3*RoutineData::kLocationCount && w.blips.size()==1,
        "persistent allocation failure stops after three attempts per marker");
    RoutineDebugBlips::Update(false,nullptr,nullptr); w.failures=0;
    RoutineDebugBlips::Update(true,nullptr,nullptr);
    Check(w.blips.size()==static_cast<std::size_t>(RoutineData::kLocationCount+1),"toggling resets exhausted creation attempts");

    const Blip lost = RoutineDebugBlips::Detail::state.anchors[0].handle;
    w.blips.erase(lost);
    const int before = w.creates;
    RoutineDebugBlips::Update(true,nullptr,nullptr);
    Check(w.creates==before,"lost existing marker starts a delayed recovery instead of immediate recreation");
    w.now+=1999; RoutineDebugBlips::Update(true,nullptr,nullptr);
    Check(w.creates==before,"lost handle recovery respects the full retry interval");
    ++w.now; RoutineDebugBlips::Update(true,nullptr,nullptr);
    Check(w.creates==before+1 && !w.blips.contains(lost),"only the lost handle is replaced after its delay");
}
int main()
{
    CatalogCreationReuseAndToggleCleanup();
    ExactMarkersMoveWithoutRecreation();
    FailedCreationAndLostHandlesHaveBoundedRetries();
    RoutineDebugBlips::Clear();
    std::printf("Routine debug blips: %u checks passed.\n",checks);
}
