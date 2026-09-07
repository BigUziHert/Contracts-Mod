struct Vector3 { float x, y, z; };
#include "../rdr2 scripting environment/samples/Pools/routine_plan.h"

#include <cstdio>
#include <cstdlib>
#include <set>

using namespace RoutinePlan;
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
static void CheckEmpty(const Plan& plan)
{
    Check(plan.seed == 0 && plan.townIndex == -1 && plan.occupation == 0 && plan.offsetMinutes == 0,
        "unprepared contract has no stale habit metadata");
    for (int location : plan.route) Check(location == -1, "unprepared contract has no route locations");
    Check(!Valid(plan), "unprepared plan cannot be issued");
    for (const auto& line : CardLines(plan)) Check(line.empty(), "unprepared plan produces no false card clues");
}
static void DefaultsAndInvalidInputs()
{
    Plan plan;
    CheckEmpty(plan);
    Check(!Build(plan, -1, RoutineData::Local, 77), "negative town rejected");
    CheckEmpty(plan);
    Check(!Build(plan, RoutineData::kTownCount, RoutineData::Local, 77), "out-of-range town rejected");
    CheckEmpty(plan);
    for (unsigned occupation : {0u, 16u, RoutineData::Local | RoutineData::Laborer})
    {
        Check(!Build(plan, 0, occupation, 77), "unknown or combined occupation cannot produce a misleading title");
        CheckEmpty(plan);
    }
    Check(Build(plan, 0, RoutineData::Local, 77), "valid preparation succeeds before replacement failure");
    Check(!Build(plan, 0, RoutineData::DockWorker, 88), "town without dock-worker workplace rejected");
    CheckEmpty(plan);
    Routine::Candidate candidates[4];
    for (auto& candidate : candidates) candidate.id = 999;
    Check(!Candidates(plan, candidates), "unprepared plan rejects candidate conversion");
    for (const auto& candidate : candidates)
        Check(candidate.id == -1 && !candidate.available, "invalid conversion clears old candidate identities");
    Check(!Candidates(plan, nullptr), "null candidate output handled");
    Check(std::string(OccupationName(RoutineData::Local)) == "Local resident", "local resident title is explicit");
    Check(std::string(OccupationName(RoutineData::Laborer)) == "Laborer", "laborer title is explicit");
    Check(std::string(OccupationName(RoutineData::DockWorker)) == "Dock worker", "dock worker title is explicit");
    Check(std::string(OccupationName(RoutineData::LivestockHand)) == "Livestock hand", "livestock title is explicit");
}
static void TownRoleSeedCoverageAndTruthfulCards()
{
    // Columns: Local, Laborer, DockWorker, LivestockHand. Every supported role
    // must have a complete route; an occupation label cannot substitute a missing job area.
    const bool supported[7][4] = {
        {true, true, false, true}, {true, true, false, true}, {true, true, false, true},
        {true, true, false, true}, {true, true, true, false},
        {true, true, true, false}, {true, true, false, false}
    };
    const char* prefixes[4] = {"Day: ", "Afternoon: ", "Evening: ", "Late: "};
    Check(RoutineData::kTownCount == 7, "coverage table includes every authored town");
    for (int town = 0; town < RoutineData::kTownCount; ++town)
        for (int role = 0; role < 4; ++role)
        {
            const unsigned occupation = 1u << role;
            std::set<int> visited[4];
            for (std::uint32_t seed = 0; seed < 128; ++seed)
            {
                Plan plan, repeat;
                const bool ready = Build(plan, town, occupation, seed);
                Check(ready == supported[town][role], "all towns accept exactly their supported roles");
                if (!ready) { CheckEmpty(plan); continue; }
                Check(Valid(plan), "constructed route is valid");
                Check(plan.seed == seed && plan.townIndex == town && plan.occupation == occupation,
                    "constructed plan preserves contract identity");
                Check(plan.offsetMinutes >= -30 && plan.offsetMinutes <= 30, "habit time offset remains within half an hour");
                Check(Build(repeat, town, occupation, seed), "same seed preparation succeeds");
                Check(repeat.offsetMinutes == plan.offsetMinutes, "time variation is deterministic per contract");
                Routine::Candidate candidates[4];
                Check(Candidates(plan, candidates), "complete plan builds live-selection candidates");
                const auto lines = CardLines(plan);
                Check(lines[1] == std::string("Town: ") + RoutineData::kTowns[town].name, "card uses actual contract town");
                for (const auto& line : lines)
                    Check(!line.empty() && line.size() <= 32, "every card line fits a bounded readable field");
                for (int phase = 0; phase < 4; ++phase)
                {
                    Check(plan.route[phase] == repeat.route[phase], "same contract seed selects same route");
                    visited[phase].insert(plan.route[phase]);
                    const auto& location = RoutineData::kLocations[plan.route[phase]];
                    Check(location.enabled && location.town == RoutineData::kTowns[town].id,
                        "every habit refers to an enabled same-town place");
                    Check(static_cast<int>(location.kind) == phase && (location.occupations & occupation) != 0,
                        "habit destination matches time phase and target occupation");
                    const auto& candidate = candidates[phase];
                    Check(candidate.id == plan.route[phase] && candidate.phases == (1u << phase),
                        "live destination IDs are exactly the four card habit destinations");
                    Check(candidate.occupations == location.occupations && candidate.available,
                        "candidate retains authored occupation and availability constraints");
                    Check(candidate.hours.startMinute == location.openMinute && candidate.hours.endMinute == location.closeMinute,
                        "routine offset cannot change the actual destination visiting window");
                    Check(candidate.travelMinutes == 0 && candidate.fallback == (phase == 3),
                        "bridge supplies travel estimate and only overnight place is fallback");
                    const std::string name = std::string(location.name) == "General store frontage"
                        ? "Store frontage" : location.name;
                    Check(lines[phase + 2] == std::string(prefixes[phase]) + name,
                        "card names actual selected location with its exterior qualifier");
                }
                Check(Routine::SelectDestination(candidates, 4, Routine::Phase::Work, occupation, 600, 15, seed) == plan.route[0],
                    "daytime selection visits the card's actual day destination");
                Check(Routine::SelectDestination(candidates, 4, Routine::Phase::Leisure, occupation, 1320, 15, seed) == plan.route[2],
                    "evening selection visits the card's actual evening destination");
                candidates[0].available = false;
                Check(Routine::SelectDestination(candidates, 4, Routine::Phase::Work, occupation, 600, 15, seed) == plan.route[3],
                    "unavailable work falls back to another truthful card destination");
                const auto stillSame = CardLines(plan);
                Check(stillSame == lines, "live candidate unavailability does not reroll printed habits");
            }
            if (!supported[town][role]) continue;
            for (int phase = 0; phase < 4; ++phase)
                for (int index = 0; index < RoutineData::kLocationCount; ++index)
                {
                    const auto& location = RoutineData::kLocations[index];
                    if (location.enabled && location.town == RoutineData::kTowns[town].id &&
                        static_cast<int>(location.kind) == phase && (location.occupations & occupation) != 0)
                        Check(visited[phase].contains(index), "seed range reaches every eligible authored location");
                }
        }
}
static void IssuedContractsCanReachEveryCatalogSite()
{
    Check(GeneratedOccupation(-1, 0) == 0 && GeneratedOccupation(RoutineData::kTownCount, 0) == 0,
        "unknown towns do not inherit an unrelated occupation");
    std::set<int> visited;
    for (int town = 0; town < RoutineData::kTownCount; ++town)
        for (std::uint32_t seed = 0; seed < 2048; ++seed)
        {
            Plan plan;
            Check(Build(plan, town, GeneratedOccupation(town, seed), seed),
                "actual contract generation always supplies a supported complete profile");
            for (int location : plan.route) visited.insert(location);
        }
    for (int index = 0; index < RoutineData::kLocationCount; ++index)
        if (RoutineData::kLocations[index].enabled)
            Check(visited.contains(index), "every enabled map dot can be selected by an actually generated contract");
}
static void CorruptOrStalePlanCannotPrintClues()
{
    Plan plan;
    Check(Build(plan, 0, RoutineData::Local, 4), "setup valid plan for stale-data validation");
    const Plan good = plan;
    for (int invalid : {-1, RoutineData::kLocationCount})
    {
        plan = good;
        plan.route[2] = invalid;
        Check(!Valid(plan), "invalid route index is rejected");
        for (const auto& line : CardLines(plan)) Check(line.empty(), "invalid route cannot expose stale card facts");
    }
    plan = good;
    plan.route[1] = plan.route[0];
    Check(!Valid(plan), "wrong phase destination is rejected");
    plan = good;
    plan.townIndex = 1;
    Check(!Valid(plan), "route from another town cannot be relabeled");
    plan = good;
    plan.occupation = RoutineData::DockWorker;
    Check(!Valid(plan), "incompatible occupation cannot be relabeled over old route");
    plan = Plan{};
    CheckEmpty(plan);
}
int main()
{
    DefaultsAndInvalidInputs();
    TownRoleSeedCoverageAndTruthfulCards();
    IssuedContractsCanReachEveryCatalogSite();
    CorruptOrStalePlanCannotPrintClues();
    std::printf("routine_plan: %u checks passed\n", checks);
}
