#include <climits>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include "../rdr2 scripting environment/samples/Pools/routine_debug_view.h"

static unsigned checks = 0;
static void Check(bool value, const char* message)
{
    ++checks;
    if (!value) { std::fprintf(stderr, "FAILED: %s\n", message); std::exit(1); }
}
static bool Contains(const RoutineDebugView::Lines& lines, const char* text)
{
    for (const auto& line : lines) if (line.find(text) != std::string::npos) return true;
    return false;
}
static RoutineDebugView::Snapshot Live()
{
    RoutineDebugView::Snapshot snapshot;
    snapshot.active = snapshot.targetExists = snapshot.hasDestination = true;
    snapshot.loaded = snapshot.taskActive = snapshot.destinationOpen = snapshot.destinationValid = true;
    snapshot.minute = 1073; snapshot.nextMinute = 1115;
    snapshot.town = "Valentine"; snapshot.occupation = "Livestock hand";
    snapshot.doing = "Walking"; snapshot.destination = "Stable frontage"; snapshot.nextDestination = "Smithfield's frontage";
    snapshot.playerDistance = 63.125f; snapshot.destinationDistance = 12.375f; snapshot.wanderRadius = 26;
    snapshot.x = -269.75f; snapshot.y = 785.4375f; snapshot.z = 118.5f;
    return snapshot;
}
int main()
{
    using namespace RoutineDebugView;
    Snapshot snapshot;
    auto lines = Format(snapshot);
    Check(lines.size() == 10 && lines[0] == "BOUNTY DEBUG [F8 hide]  00:00", "panel always has a bounded ten-row structure and game clock");
    Check(Contains(lines, "Press U for a contract") && Contains(lines, "Red markers: all authored routine stops"),
        "inactive display explains starting a contract and all red markers");
    Check(!Contains(lines, "m away") && lines[9].empty(), "inactive display never shows invented target measurements");

    snapshot = Live(); lines = Format(snapshot);
    Check(lines[0].find("17:53") != std::string::npos && lines[1] == "Valentine | Livestock hand", "active title and identity match the read-only snapshot");
    Check(lines[2] == "Target: 63.1 m away" && lines[4] == "Stop: Stable frontage | 12.4 m", "player and remaining target travel distances use distinct one-decimal metre values");
    snapshot.destination = "General store frontage";
    Check(Format(snapshot)[4] == "Stop: General store frontage | 12.4 m", "authored stop names remain readable without truncation");
    snapshot.destination = "Stable frontage";
    Check(lines[3] == "Doing: Walking" && snapshot.doing == std::string("Walking"), "doing is displayed without mutating the snapshot");
    Check(lines[5] == "Next planned: Smithfield's frontage @ 18:35" && Contains(lines, "Plans may change"), "future stop and time are explicitly a changeable plan");
    Check(!Contains(lines, "will arrive") && !Contains(lines, "guaranteed"), "future schedule never promises arrival");
    Check(lines[7] == "Wander: 26.0 m | Outdoors", "wander radius is separate from interior status");
    Check(lines[8] == "Loaded Y | Routine Y | Open Y | Valid Y", "native and destination validity states stay distinct");
    Check(lines[9] == "XYZ: -269.8, 785.4, 118.5", "coordinates preserve signs and bounded readable precision");
    snapshot.fallback = true; snapshot.inside = true;
    lines = Format(snapshot);
    Check(Contains(lines, "[fallback]") && Contains(lines, "Indoors"), "fallback and indoor location are labelled explicitly");
    snapshot.hasDestination = false;
    lines = Format(snapshot);
    Check(lines[4] == "Stop: None" && !Contains(lines, "[fallback]") && lines[8].find("Open - | Valid -") != std::string::npos,
        "missing destination cannot display stale distance, fallback or availability");

    for (bool dead : {false, true})
    {
        snapshot = Live(); snapshot.targetExists = false; snapshot.targetDead = dead;
        lines = Format(snapshot);
        Check(Contains(lines, dead ? "Deceased (body missing)" : "Missing"), "absent corpse and missing living target remain distinguishable");
        Check(!Contains(lines, "Walking") && !Contains(lines, "m away") && !Contains(lines, "Next planned") && !Contains(lines, "XYZ:"),
            "missing/deceased target suppresses stale travel, distance, plan and position");
    }
    snapshot = Live(); snapshot.targetDead = true;
    lines = Format(snapshot);
    Check(lines[2] == "Target: Deceased" && lines[3] == "Routine stopped." && !Contains(lines, "Walking") && !Contains(lines, "Next planned"),
        "existing corpse is deceased rather than travelling and has no future routine plan");
    Check(lines[4] == "Body: 63.1 m away" && lines[5] == "XYZ: -269.8, 785.4, 118.5",
        "fresh existing-body distance and coordinates remain available for corpse photography testing");

    snapshot = Live(); snapshot.minute = -1; snapshot.nextMinute = 1445;
    lines = Format(snapshot);
    Check(lines[0].find("23:59") != std::string::npos && lines[5].find("00:05") != std::string::npos, "negative and next-day minutes normalize across midnight");
    snapshot.minute = INT_MIN; snapshot.nextMinute = INT_MAX;
    lines = Format(snapshot);
    Check(lines[0].find("21:52") != std::string::npos && lines[5].find("02:07") != std::string::npos,
        "extreme clock integers normalize without overflow or variable-width output");
    snapshot.nextMinute = -1;
    Check(Format(snapshot)[5].find('@') == std::string::npos, "unknown next time never displays a fabricated clock value");

    snapshot = Live(); snapshot.town = nullptr; snapshot.occupation = nullptr; snapshot.doing = nullptr;
    snapshot.destination = nullptr; snapshot.nextDestination = nullptr;
    lines = Format(snapshot);
    Check(lines[1] == "Unknown | Unknown" && lines[3] == "Doing: Waiting" && Contains(lines, "Next planned: None"), "all externally supplied text fields are null safe");
    const std::string longText(10000, 'A');
    snapshot.town = snapshot.occupation = snapshot.doing = snapshot.destination = snapshot.nextDestination = longText.c_str();
    snapshot.fallback = true;
    lines = Format(snapshot);
    for (const auto& line : lines) Check(line.size() <= 60, "long snapshot labels cannot overflow the compact panel rows");
    Check(Contains(lines, "..."), "overlong labels are visibly truncated");
    snapshot = Live(); snapshot.doing = "Walking\n\r\tto shop";
    Check(Format(snapshot)[3].find_first_of("\n\r\t") == std::string::npos, "control characters cannot create extra overlay rows");

    for (float bad : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), 1.0e30f})
    {
        snapshot = Live();
        snapshot.playerDistance = snapshot.destinationDistance = snapshot.wanderRadius = snapshot.x = snapshot.y = snapshot.z = bad;
        lines = Format(snapshot);
        Check(!Contains(lines, "nan") && !Contains(lines, "inf") && lines[2] == "Target: -- m away" && lines[9] == "XYZ: --, --, --",
            "non-finite or implausibly large measurements are explicit unknowns instead of NaN or huge text");
    }
    snapshot = Live(); snapshot.playerDistance = -1;
    Check(Format(snapshot)[2] == "Target: -- m away", "negative distance is unknown rather than a misleading measurement");
    std::printf("Routine debug view: %u checks passed.\n", checks);
}
