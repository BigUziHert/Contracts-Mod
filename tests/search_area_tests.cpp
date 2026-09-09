// Exercise the actual static search-circle creation and the normal frame tail.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using Blip = int;
using Hash = unsigned;
struct Vector3 { float x = 0, y = 0, z = 0; };
static unsigned checks = 0;
static void Check(bool value, const char* message)
{
    ++checks;
    if (!value) { std::fprintf(stderr, "FAILED: %s\n", message); std::exit(EXIT_FAILURE); }
}
static bool SamePoint(Vector3 a, Vector3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
struct ContractDef { Vector3 spawn; float searchRadius; };
static struct { const ContractDef* def = nullptr; Blip searchBlip = 0; Vector3 targetPos; } C;
static constexpr Hash BLIP_STYLE_MP_MISSION_GIVER = 1234;
static struct World
{
    Vector3 centre;
    float radius = 0;
    unsigned creates = 0, styles = 0;
    std::vector<std::string> events;
} w;
namespace MAP
{
static Blip BLIP_ADD_FOR_RADIUS(Hash style, Vector3 centre, float radius)
{
    Check(style == BLIP_STYLE_MP_MISSION_GIVER, "the circle uses the contract mission style");
    ++w.creates; w.centre = centre; w.radius = radius;
    return 77;
}
}
static void StyleTargetBlip(Blip blip, const char* color, bool large, bool pulse)
{
    Check(blip == 77 && std::strcmp(color, "BLIP_MODIFIER_MP_COLOR_32") == 0 && large && !pulse,
        "the static search circle keeps its target color and size");
    ++w.styles;
}
static void TraceCardInspection() { w.events.push_back("trace"); }
static void UpdateCard() { w.events.push_back("card"); }
static void WAIT(int delay) { Check(delay == 0, "the normal frame yields once"); w.events.push_back("wait"); }

#include "search_area_under_test.h"

int main()
{
    for (float radius : {35.0f, 50.0f, 65.0f, 85.0f})
    {
        w = {}; C = {};
        const ContractDef definition{{1370, -1354, 78}, radius};
        C.def = &definition; C.targetPos = {1500, -1200, 80};
        AddSearchBlip();
        Check(C.searchBlip == 77 && w.creates == 1 && w.styles == 1 &&
            SamePoint(w.centre, definition.spawn) && w.radius == radius,
            "the circle uses the selected static contract's exact center and native wander radius");
        for (int frame = 0; frame < 20; ++frame)
        {
            C.targetPos.x += 5;
            RunProductionSearchFrameTail();
            Check(SamePoint(w.centre, definition.spawn) && w.creates == 1 && w.styles == 1,
                "moving targets leave the original search area fixed");
        }
        Check(w.events.size() == 60 && w.events[0] == "trace" && w.events[1] == "card" && w.events[2] == "wait",
            "normal card maintenance and frame yields remain active");
    }
    std::printf("Static contract search area: %u checks passed.\n", checks);
}