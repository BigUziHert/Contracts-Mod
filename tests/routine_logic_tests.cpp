#include "../rdr2 scripting environment/samples/Pools/routine_logic.h"

#include <cstdio>
#include <cstdlib>

using namespace Routine;
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
static Observation Observe(std::uint64_t now, float distance = 100.0f)
{
    Observation observation;
    observation.nowMs = now;
    observation.minute = 600;
    observation.destinationId = 7;
    observation.distance = distance;
    return observation;
}
static void OpeningWindowsAndArrival()
{
    const Window shop{480, 1080}, saloon{1080, 120}, allDay{0, 0};
    for (int minute = 0; minute < 1440; ++minute)
    {
        Check(IsOpen(shop, minute) == (minute >= 480 && minute < 1080), "shop opening includes start, excludes end");
        Check(IsOpen(saloon, minute) == (minute >= 1080 || minute < 120), "saloon opening crosses midnight");
        Check(IsOpen(allDay, minute), "equal endpoints remain open for every minute");
        Check(IsOpen(shop, minute - 1440) == IsOpen(shop, minute + 1440), "opening accepts normalized days");
    }
    Check(MinutesUntilClose(saloon, 1430) == 130, "late evening time-to-close crosses midnight");
    Check(MinutesUntilClose(saloon, 110) == 10, "after-midnight close remains same active window");
    Check(MinutesUntilClose(saloon, 120) == 0, "closing is unavailable");
    Check(MinutesUntilClose(allDay, 1440) == 1440, "24-hour venue has no daily forced closure");
    Check(!CanArriveAndStay(shop, 1060, 25, 0), "reject shop closed before arrival");
    Check(!CanArriveAndStay(shop, 1060, 20, 0), "reject arrival exactly at closing");
    Check(!CanArriveAndStay(shop, 1060, 10, 11), "reject insufficient remaining visit time");
    Check(CanArriveAndStay(shop, 1060, 10, 10), "visit can finish at closing");
    Check(CanArriveAndStay(saloon, 1430, 45, 60), "travel and visit may cross midnight while open");
    Check(!CanArriveAndStay(shop, 470, 20, 15), "closed venue excluded even if it would open during travel");
    Check(!CanArriveAndStay(shop, 600, -1, 10), "invalid travel estimate rejected");
    Check(!CanArriveAndStay(shop, 600, 1, -1), "invalid stay estimate rejected");
    Check(CanArriveAndStay(allDay, 1400, 2000, 100), "24-hour fallback survives travel across day boundaries");
    Check(PhaseAt(359) == Phase::Rest && PhaseAt(360) == Phase::Work, "morning starts work");
    Check(PhaseAt(840) == Phase::Shops && PhaseAt(1080) == Phase::Leisure, "afternoon and evening have distinct habits");
    Check(PhaseAt(0) == Phase::Rest && PhaseAt(1439) == Phase::Leisure, "midnight switches to rest");
    Check(PhaseAt(370, 30) == Phase::Rest && PhaseAt(390, 30) == Phase::Work, "offset delays routine transition");
    for (std::uint32_t seed = 0; seed < 1000; ++seed)
    {
        const int offset = BoundedOffset(seed);
        Check(offset >= -30 && offset <= 30, "contract time variation stays bounded");
        Check(!IsOpen(shop, 1080), "schedule variation never changes shop closing");
    }
}
static void CandidateSelection()
{
    const unsigned work = PhaseMask(Phase::Work), rest = PhaseMask(Phase::Rest);
    Candidate candidates[] = {
        {10, work, 1, {480, 1080}, true, false, 10},
        {11, work, 1, {480, 1080}, true, false, 15},
        {12, rest, 0, {0, 0}, true, true, 5},
        {13, work, 2, {0, 0}, true, false, 0}
    };
    Check(SelectDestination(candidates, 4, Phase::Work, 1, 600, 15, 0) == 10, "seed selects first matching workplace");
    Check(SelectDestination(candidates, 4, Phase::Work, 1, 600, 15, 1) == 11, "seed rotates compatible workplaces");
    Check(SelectDestination(candidates, 4, Phase::Work, 1, 600, 15, 3) == 10, "incompatible occupation is skipped");
    Check(SelectDestination(candidates, 4, Phase::Work, 1, 600, 15, 0, 10) == 11, "explicit excluded destination is skipped");
    candidates[0].available = false;
    Check(SelectDestination(candidates, 4, Phase::Work, 1, 600, 15, 0) == 11, "occupied or unloaded destination is skipped");
    candidates[1].available = false;
    Check(SelectDestination(candidates, 4, Phase::Work, 1, 600, 15, 0) == 12, "authored all-day fallback handles unavailable work");
    candidates[0].available = candidates[1].available = true;
    Check(SelectDestination(candidates, 4, Phase::Work, 1, 1060, 15, 0) == 12, "closing before visit finishes selects safe fallback");
    candidates[2].available = false;
    Check(SelectDestination(candidates, 4, Phase::Leisure, 1, 600, 15, 0) == -1, "no available fallback cannot invent a destination");
    Check(SelectDestination(nullptr, 4, Phase::Work, 1, 600, 15, 0) == -1, "null catalog is bounded failure");
    Check(SelectDestination(candidates, 0, Phase::Work, 1, 600, 15, 0) == -1, "empty catalog is bounded failure");
}
static void TravelArrivalAndWandering()
{
    const Config config;
    Controller controller;
    auto observation = Observe(0);
    Check(controller.Tick(config, observation).action == Action::Travel, "new distant destination issues one travel task");
    observation.taskActive = true;
    for (std::uint64_t now = 16; now < 10000; now += 16)
    {
        observation.nowMs = now;
        observation.distance = 100.0f - static_cast<float>(now) / 1000.0f;
        Check(controller.Tick(config, observation).action == Action::None, "healthy travel never reissues every frame");
    }
    observation.nowMs = 12000;
    observation.distance = 4.0f;
    Check(controller.Tick(config, observation).action == Action::Wander, "arrival always delegates behavior to native area wandering");
    Check(controller.state == State::Wandering && controller.destinationId == 7, "wandering retains fixed destination identity");
    observation.nowMs = 16000;
    Check(controller.Tick(config, observation).action == Action::None, "healthy wandering or its observed ambient pause stays uninterrupted");
    observation.taskActive = false;
    observation.nowMs = 17000;
    Check(controller.Tick(config, observation).action == Action::None, "dropped wandering gets task absence grace");
    observation.nowMs = 18500;
    Check(controller.Tick(config, observation).action == Action::Wander, "dropped native wandering gets bounded local recovery");
    observation.taskActive = true;
    observation.nowMs = 19000;
    Check(controller.Tick(config, observation).action == Action::None, "wander recovery does not restart a healthy task");
    controller.Reset();
    observation = Observe(0, 2.0f);
    Check(controller.Tick(config, observation).action == Action::Wander, "nearby initial destination also chooses wandering");

    observation.taskActive = true; // The bridge also supplies this for an observed native scenario.
    observation.destinationOpen = false;
    Check(controller.Tick(config, observation).action == Action::Wait && controller.state == State::Waiting,
        "closing hours override a healthy ambient pause after arrival");
    controller.Reset();
    observation = Observe(0, 2.0f);
    controller.Tick(config, observation);
    observation.taskActive = true;
    observation.destinationAvailable = false;
    Check(controller.Tick(config, observation).action == Action::Wait,
        "an unavailable destination overrides a healthy ambient pause after arrival");
}
static void BoundedFailureAndCooldown()
{
    Config config;
    Controller controller;
    auto observation = Observe(0);
    Check(controller.Tick(config, observation).action == Action::Travel, "missing-task test starts travel");
    observation.nowMs = 1;
    Check(controller.Tick(config, observation).action == Action::None, "newly absent task is tolerated");
    observation.nowMs = 1501;
    Check(controller.Tick(config, observation).action == Action::None, "task retry also respects issue interval");
    for (unsigned retry = 1; retry <= 2; ++retry)
    {
        observation.nowMs = retry * 4000;
        Check(controller.Tick(config, observation).action == Action::Travel, "dropped travel task has bounded retry");
        observation.nowMs++;
        Check(controller.Tick(config, observation).action == Action::None, "retry grace restarts after each submitted task");
    }
    observation.nowMs = 12000;
    const Decision failure = controller.Tick(config, observation);
    Check(failure.action == Action::Wait && failure.reevaluate && failure.failedDestination == 7, "exhausted travel requests fallback once");
    Check(controller.IsCoolingDown(7, 12000) && !controller.IsCoolingDown(7, 72000), "failed destination cooldown expires exactly at deadline");
    observation.nowMs = 12001;
    Check(controller.Tick(config, observation).action == Action::None, "cooled destination cannot immediately restart");
    observation.destinationId = 8;
    Check(controller.Tick(config, observation).action == Action::Travel, "another available destination remains usable");
    Check(controller.IsCoolingDown(7, 12001), "new destination does not erase earlier cooldown");
    controller.Reset();
    Check(!controller.IsCoolingDown(7, 12001), "contract cleanup clears destination cooldown");
    observation = Observe(0);
    observation.taskActive = true;
    controller.Tick(config, observation);
    for (unsigned retry = 1; retry <= 2; ++retry)
    {
        observation.nowMs = retry * 20000;
        Check(controller.Tick(config, observation).action == Action::Travel, "active but stuck travel also has bounded recovery");
    }
    observation.nowMs = 60000;
    Check(controller.Tick(config, observation).failedDestination == 7, "permanently stuck native task fails destination");
    controller.Reset();
    config.noProgressMs = 600000;
    observation = Observe(0);
    observation.taskActive = true;
    controller.Tick(config, observation);
    observation.nowMs = config.travelTimeoutMs;
    observation.distance = 5.0f;
    Check(controller.Tick(config, observation).failedDestination == 7, "absolute deadline bounds slow or looping travel");
}
static void PriorityResumeClockAndClosure()
{
    const Config config;
    for (const char* priority : {"combat", "search", "native combat", "restraint/get-up", "player unavailable", "navigation unloaded"})
    {
        Controller controller;
        auto observation = Observe(0);
        controller.Tick(config, observation);
        observation.blocked = true;
        observation.destinationOpen = false;
        observation.nowMs = 600000;
        const Decision blocked = controller.Tick(config, observation);
        Check(blocked.action == Action::None && !blocked.reevaluate && controller.state == State::Suspended, priority);
        observation.blocked = false;
        observation.nowMs++;
        const Decision resumed = controller.Tick(config, observation);
        Check(resumed.action == Action::None && resumed.reevaluate, "resume reselects current routine before task submission");
        Check(!controller.IsCoolingDown(7, observation.nowMs), "priority interruption never marks old destination as navigation failure");
        observation.destinationId = 8;
        observation.destinationOpen = true;
        observation.nowMs++;
        Check(controller.Tick(config, observation).action == Action::Travel, "resumed routine accepts newly selected destination");
    }
    Controller controller;
    auto observation = Observe(0);
    controller.Tick(config, observation);
    observation.minute = 1020;
    observation.nowMs = 1000;
    Check(controller.Tick(config, observation).reevaluate, "time skip selects appropriate new phase");
    observation.destinationId = 8;
    Check(controller.Tick(config, observation).action == Action::Travel, "fresh phase selection takes effect once");
    observation.minute = 1019;
    Check(controller.Tick(config, observation).reevaluate, "backward time change cannot leave stale schedule");
    observation.destinationId = 9;
    controller.Tick(config, observation);
    observation.destinationOpen = false;
    const Decision closed = controller.Tick(config, observation);
    Check(closed.action == Action::Wait && closed.reevaluate, "venue closing during travel stops stale route and reselects");
    observation.nowMs++;
    Check(!controller.Tick(config, observation).reevaluate, "unavailable destination does not spin selection every frame");
    observation.nowMs += config.selectionRetryMs;
    Check(controller.Tick(config, observation).reevaluate, "waiting eventually retries availability");
    controller.Reset();
    observation = Observe(0);
    observation.minute = 1439;
    controller.Tick(config, observation);
    observation.minute = 0;
    Check(controller.Tick(config, observation).reevaluate, "midnight habit transition reselects overnight destination");
    observation.destinationId = 8;
    controller.Tick(config, observation);
    observation.minute = 1;
    Check(!controller.Tick(config, observation).reevaluate, "ordinary clock advancement does not reissue routine");
}
int main()
{
    OpeningWindowsAndArrival();
    CandidateSelection();
    TravelArrivalAndWandering();
    BoundedFailureAndCooldown();
    PriorityResumeClockAndClosure();
    std::printf("routine_logic: %u checks passed\n", checks);
}
