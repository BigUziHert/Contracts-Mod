struct Vector3 { float x, y, z; };
#include "../rdr2 scripting environment/samples/Pools/routine_plan.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

struct TextCall
{
    std::string text;
    float x, y, scale;
    int red, green, blue, alpha;
};
static std::vector<TextCall> draws;
static void DrawTextToScreen(const char* text, float x, float y, float scale,
    int red, int green, int blue, int alpha)
{
    draws.push_back({text, x, y, scale, red, green, blue, alpha});
}
#include "../rdr2 scripting environment/samples/Pools/routine_card.h"

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
static bool Near(float a, float b) { return std::fabs(a - b) < .00001f; }
static std::string Lowercase(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
        [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return text;
}
static void CheckDrawnCard(const RoutinePlan::Plan& plan)
{
    const auto lines = RoutinePlan::CardLines(plan);
    draws.clear();
    RoutineCard::Draw(lines);
    Check(draws.size() == 8, "complete card draws one title, six actual habit fields and one variation note");
    Check(draws[0].text == "USUAL HAUNTS", "routine section has the requested title");
    Check(draws[7].text == "Visits may vary.", "card describes habits as variable visits");
    Check(draws[1].text == std::string("Occupation: ") + RoutinePlan::OccupationName(plan.occupation),
        "drawn occupation matches the issued plan");
    Check(draws[2].text == std::string("Town: ") + RoutineData::kTowns[plan.townIndex].name,
        "drawn town matches the issued plan");
    const char* phasePrefixes[] = {"Day: ", "Afternoon: ", "Evening: ", "Late: "};
    for (int phase = 0; phase < 4; ++phase)
    {
        const auto& location = RoutineData::kLocations[plan.route[phase]];
        const std::string exteriorName = std::string(location.name) == "General store frontage"
            ? "Store frontage" : location.name;
        Check(draws[phase + 3].text == std::string(phasePrefixes[phase]) + exteriorName,
            "drawn habit names the exact selected route location and preserves its exterior qualifier");
    }
    for (std::size_t index = 0; index < lines.size(); ++index)
    {
        Check(draws[index + 1].text == lines[index], "renderer preserves every cached production CardLines string exactly");
        Check(!lines[index].empty() && lines[index].size() <= 32,
            "actual authored card text stays within the existing 32-character field budget");
    }

    // DrawCardFace panel bounds: .30-.70 x .33-.67; portrait ends at x=.47;
    // reward starts at y=.57. These verify anchors/spacing, not engine font metrics.
    float previousY = .33f;
    for (const auto& draw : draws)
    {
        Check(Near(draw.x, .49f) && draw.x > .47f && draw.x < .70f,
            "routine text begins to the right of the existing portrait inside the panel");
        Check(std::isfinite(draw.y) && draw.y >= .33f && draw.y < .57f,
            "every routine text anchor remains inside the panel above the existing reward");
        Check(draw.y > previousY && draw.y - previousY >= .012f,
            "routine title, fields and footer have distinct vertically ordered anchors");
        Check(draw.scale >= .25f && draw.scale <= .30f,
            "routine copy uses the bounded small text scales");
        Check(draw.red == 255 && draw.green == 255 && draw.blue == 255 && draw.alpha == 255,
            "routine copy keeps the existing opaque white card text color");
        previousY = draw.y;
        const auto lower = Lowercase(draw.text);
        for (const char* unsupported : {"poker", "blackjack", "five-finger", "five finger", "haircut",
            "shopping", "purchases", "buys ", "plays ", "watching a show", "drinks ", "tram", "theatre"})
            Check(lower.find(unsupported) == std::string::npos,
                "enabled exterior routine card does not promise unsupported commerce, games, shows or transport");
    }
    Check(Near(draws.front().y, .343f) && Near(draws.front().scale, .30f),
        "section heading stays in the small gap above the first habit field");
    Check(Near(draws[1].y, .375f) && Near(draws[6].y, .510f) && Near(draws[1].scale, .28f),
        "six habit fields fit between the heading and variation note");
    Check(Near(draws.back().y, .545f) && Near(draws.back().scale, .25f),
        "variation note leaves the reward anchor at .57 untouched");
}
static void EverySupportedRouteDrawsItsOwnClues()
{
    unsigned validPlans = 0;
    for (int town = 0; town < RoutineData::kTownCount; ++town)
        for (unsigned occupation : {RoutineData::Local, RoutineData::Laborer, RoutineData::DockWorker, RoutineData::LivestockHand})
            for (std::uint32_t seed = 0; seed < 32; ++seed)
            {
                RoutinePlan::Plan plan;
                if (!RoutinePlan::Build(plan, town, occupation, seed)) continue;
                ++validPlans;
                CheckDrawnCard(plan);
            }
    Check(validPlans == 480, "renderer coverage includes all fifteen supported town/occupation profiles");
}
static void InvalidIdentityNeverDrawsStaleInformation()
{
    RoutinePlan::Plan plan;
    draws.clear();
    RoutineCard::Draw(RoutinePlan::CardLines(plan));
    Check(draws.empty(), "unprepared plan draws no section title, facts or footer");
    Check(RoutinePlan::Build(plan, 4, RoutineData::DockWorker, 19), "valid card established before failed replacement");
    CheckDrawnCard(plan);
    auto stale = RoutinePlan::CardLines(plan);
    stale[0].clear();
    draws.clear();
    RoutineCard::Draw(stale);
    Check(draws.empty(), "cleared identity suppresses every stale habit field from the previous cache");
    plan.route[2] = RoutineData::kLocationCount;
    RoutineCard::Draw(RoutinePlan::CardLines(plan));
    Check(draws.empty(), "corrupt route cannot render stale or out-of-range card information");
}
int main()
{
    EverySupportedRouteDrawsItsOwnClues();
    InvalidIdentityNeverDrawsStaleInformation();
    std::printf("routine_card: %u checks passed\n", checks);
}
