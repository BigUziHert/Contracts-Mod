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
    for (unsigned recovery = 0; recovery < 4; ++recovery)
    {
        observation.taskActive = false;
        observation.nowMs += 5000;
        Check(controller.Tick(config, observation).action == Action::None, "each independent dropped wander receives absence grace");
        observation.nowMs += config.taskGraceMs;
        Check(controller.Tick(config, observation).action == Action::Wander,
            "healthy intervening wandering resets the retry budget for later ambient exits");
        observation.taskActive = true;
        ++observation.nowMs;
        Check(controller.Tick(config, observation).action == Action::None && !controller.IsCoolingDown(7, observation.nowMs),
            "independent wander recoveries never cool down a healthy stop");
    }
    controller.Reset();
    observation = Observe(0, 2.0f);
    Check(controller.Tick(config, observation).action == Action::Wander, "nearby initial destination also chooses wandering");

    observation.taskActive = true; // The bridge also supplies this for an observed native scenario.
    observation.destinationOpen = false;
    Check(controller.Tick(config, observation).action == Action::None && controller.state == State::Waiting,
        "closing hours stop accepting the destination without inventing an unauthored movement task");
    controller.Reset();
    observation = Observe(0, 2.0f);
    controller.Tick(config, observation);
    observation.taskActive = true;
    observation.destinationAvailable = false;
    Check(controller.Tick(config, observation).action == Action::None && controller.state == State::Waiting,
        "an unavailable destination is rejected without an artificial standing task");
}
static void ConfiguredAreaArrivalBoundary()
{
    Config config;
    config.arrivalDistance = 45.0f;
    Controller controller;
    auto observation = Observe(0, 45.01f);
    Check(controller.Tick(config, observation).action == Action::Travel,
        "configured area arrival still travels immediately outside the 45 metre boundary");
    observation.taskActive = true;
    observation.nowMs = 16;
    observation.distance = 45.0f;
    Check(controller.Tick(config, observation).action == Action::Wander && controller.state == State::Wandering,
        "configured arrival starts wandering exactly at the area boundary");
    observation.nowMs = 32;
    observation.distance = 46.0f;
    Check(controller.Tick(config, observation).action == Action::None && controller.state == State::Wandering,
        "healthy wandering does not restart travel after crossing back outside the arrival boundary");

    controller.Reset();
    observation = Observe(0, 44.99f);
    Check(controller.Tick(config, observation).action == Action::Wander,
        "a newly selected destination already inside its area needs no centre-directed travel");

    controller.Reset();
    observation = Observe(0, 45.0f);
    observation.blocked = true;
    Check(controller.Tick(config, observation).action == Action::None && controller.state == State::Suspended,
        "arrival distance never overrides higher-priority activity");
    observation.blocked = false;
    ++observation.nowMs;
    const Decision resumed = controller.Tick(config, observation);
    Check(resumed.action == Action::None && resumed.reevaluate,
        "resuming inside the area requires fresh selection before arrival");
    ++observation.nowMs;
    Check(controller.Tick(config, observation).action == Action::Wander,
        "a valid destination can arrive after the resume selection boundary");

    for (bool closed : {false, true})
    {
        controller.Reset();
        observation = Observe(0, 45.0f);
        observation.destinationOpen = !closed;
        observation.destinationAvailable = closed;
        observation.fallbackDestinationId = 12;
        Check(controller.Tick(config, observation).action == Action::WanderFallback &&
            controller.state == State::Waiting && controller.destinationId == 12,
            "an unavailable or closed destination at the area boundary cannot be accepted as arrived");
    }
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
    Check(failure.action == Action::None && failure.reevaluate && failure.failedDestination == 7,
        "exhausted travel requests selection without inventing an unauthored fallback");
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
    observation = Observe(0);
    observation.taskActive = true;
    controller.Tick(config, observation);
    observation.nowMs = 20000;
    observation.distance = 140.0f;
    Check(controller.Tick(config, observation).action == Action::Travel, "detour stall receives a bounded retry");
    observation.nowMs = 39999;
    observation.distance = 130.0f;
    Check(controller.Tick(config, observation).action == Action::None, "new detour progress is measured from the retry position");
    observation.nowMs = 40000;
    Check(controller.Tick(config, observation).action == Action::None,
        "progress on a detour does not retry again because of an obsolete best distance");
    controller.Reset();
    config.noProgressMs = 600000;
    observation = Observe(0);
    observation.taskActive = true;
    controller.Tick(config, observation);
    observation.nowMs = config.travelTimeoutMs;
    observation.distance = 5.0f;
    Check(controller.Tick(config, observation).failedDestination == 7, "absolute deadline bounds slow or looping travel");

    controller.Reset();
    observation = Observe(0, 500.0f);
    observation.taskActive = true;
    controller.Tick(config, observation);
    const auto deadline = controller.TripTimeoutMs();
    Check(deadline == TravelEstimateMs(500.0) && deadline > config.travelTimeoutMs,
        "a long route receives the walking estimate as its deadline above the 300-second floor");
    observation.nowMs = config.travelTimeoutMs;
    observation.distance = 250.0f;
    Check(controller.Tick(config, observation).action == Action::None, "healthy long travel survives the old absolute deadline");
    observation.nowMs = deadline;
    observation.distance = 5.0f;
    Check(controller.Tick(config, observation).failedDestination == 7,
        "the derived long-route deadline remains absolute and bounded");

    controller.Reset();
    observation = Observe(0, 2.0f);
    controller.Tick(config, observation);
    for (unsigned retry = 1; retry <= config.maxRetries; ++retry)
    {
        observation.nowMs = retry * config.retryMs - config.taskGraceMs;
        Check(controller.Tick(config, observation).action == Action::None, "unhealthy wander retry restarts absence grace");
        observation.nowMs += config.taskGraceMs;
        Check(controller.Tick(config, observation).action == Action::Wander, "continuously missing wander keeps a bounded retry budget");
    }
    observation.nowMs += config.retryMs - config.taskGraceMs;
    controller.Tick(config, observation);
    observation.nowMs += config.taskGraceMs;
    Check(controller.Tick(config, observation).failedDestination == 7,
        "wander recovery still fails when no healthy task is observed between retries");
}
static void UnchangedDestinationReevaluation()
{
    const Config config;
    for (const float distance : {2.0f, 100.0f})
    {
        Controller controller;
        auto observation = Observe(0, distance);
        controller.Tick(config, observation);
        const State previous = controller.state;
        observation.taskActive = true;
        observation.nowMs = 100;
        observation.reevaluate = true;
        Check(controller.Tick(config, observation).reevaluate && controller.state == previous,
            "reevaluation keeps the live task state observable until selection returns");
        observation.reevaluate = false;
        ++observation.nowMs;
        Check(controller.Tick(config, observation).action == Action::None && controller.state == previous,
            "same healthy destination preserves travelling or wandering without another task");
        observation.reevaluate = true;
        controller.Tick(config, observation);
        observation.reevaluate = false;
        observation.destinationId = 8;
        observation.distance = 100.0f;
        Check(controller.Tick(config, observation).action == Action::Travel,
            "selection of a different destination still issues new travel");
    }
    Controller controller;
    auto observation = Observe(0);
    observation.taskActive = true;
    controller.Tick(config, observation);
    observation.nowMs = config.travelTimeoutMs - 2;
    observation.distance = 5.0f;
    observation.reevaluate = true;
    controller.Tick(config, observation);
    observation.reevaluate = false;
    ++observation.nowMs;
    Check(controller.Tick(config, observation).action == Action::None, "same-destination recheck retains the original trip");
    ++observation.nowMs;
    Check(controller.Tick(config, observation).failedDestination == 7, "reevaluation cannot extend the original travel deadline");
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
    Check(closed.action == Action::None && closed.reevaluate, "venue closing during travel stops accepting the stale route and reselects");
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
static void AmbientFallbackSelectionAndPromotion()
{
    const Config config;
    for (int unavailableCase = 0; unavailableCase < 3; ++unavailableCase)
    {
        Controller controller;
        auto observation = Observe(0, 2.0f);
        controller.Tick(config, observation);
        observation.fallbackDestinationId = 7;
        observation.taskActive = observation.fallbackTaskActive = true;
        observation.nowMs = 100;
        if (unavailableCase == 0) observation.destinationId = -1;
        else if (unavailableCase == 1) observation.destinationAvailable = false;
        else observation.destinationOpen = false;
        Decision decision = controller.Tick(config, observation);
        Check(decision.action == Action::None && decision.reevaluate &&
            controller.state == State::Waiting && controller.destinationId == 7,
            "failed destination selection retains the accepted ambient area and its healthy task");
        for (std::uint64_t now = 101; now <= 20101; now += 1000)
        {
            observation.nowMs = now;
            decision = controller.Tick(config, observation);
            Check(decision.action == Action::None && controller.destinationId == 7,
                "repeated failed selections leave healthy fallback wandering or a scenario uninterrupted");
        }
        observation.reevaluate = true;
        Check(controller.Tick(config, observation).action == Action::None,
            "explicit fallback recheck also preserves its healthy activity");
        observation.reevaluate = false;
        observation.destinationId = 7;
        observation.destinationAvailable = observation.destinationOpen = true;
        observation.distance = 30.0f;
        Check(controller.Tick(config, observation).action == Action::None &&
            controller.state == State::Wandering && controller.destinationId == 7,
            "accepting the active fallback promotes wandering without returning to its centre");
    }

    Controller controller;
    auto observation = Observe(0);
    observation.destinationOpen = false;
    observation.taskActive = true; // The closed destination's task is not healthy fallback evidence.
    observation.fallbackDestinationId = 12;
    const Decision closed = controller.Tick(config, observation);
    Check(closed.action == Action::WanderFallback && closed.reevaluate && controller.destinationId == 12,
        "closed destination selects an independent authored fallback rather than trusting its stale task");
    observation.nowMs = 1;
    Check(controller.Tick(config, observation).action == Action::None,
        "new fallback is issued once while the native task starts");
    observation.fallbackTaskActive = true;
    observation.nowMs = config.selectionRetryMs;
    const Decision retry = controller.Tick(config, observation);
    Check(retry.action == Action::None && retry.reevaluate,
        "healthy ambient fallback continues while destination selection retries after five seconds");

    controller.Reset();
    observation = Observe(0);
    observation.destinationId = -1;
    Check(controller.Tick(config, observation).action == Action::None && controller.destinationId == -1,
        "missing authored fallback never issues arbitrary wandering or a standing task");
    observation.nowMs = config.selectionRetryMs - 1;
    Check(!controller.Tick(config, observation).reevaluate, "fallback-free selection respects its retry deadline");
    ++observation.nowMs;
    Check(controller.Tick(config, observation).reevaluate, "fallback-free selection still retries at the deadline");
}
static void AmbientFallbackRecoveryAndPriority()
{
    const Config config;
    Controller controller;
    auto observation = Observe(0);
    observation.destinationId = -1;
    observation.fallbackDestinationId = 12;
    Check(controller.Tick(config, observation).action == Action::WanderFallback,
        "no usable destination immediately starts authored fallback wandering");
    for (unsigned retry = 1; retry <= 8; ++retry)
    {
        observation.nowMs = (retry - 1) * config.retryMs + 1;
        Check(controller.Tick(config, observation).action == Action::None, "fallback task absence starts a fresh grace interval");
        observation.nowMs += config.taskGraceMs;
        Check(controller.Tick(config, observation).action == Action::None, "fallback recovery observes the four-second issue throttle");
        observation.nowMs = retry * config.retryMs - 1;
        observation.reevaluate = true;
        Check(controller.Tick(config, observation).action == Action::None,
            "failed selection recheck does not issue its own movement task");
        observation.reevaluate = false;
        ++observation.nowMs;
        Check(controller.Tick(config, observation).action == Action::WanderFallback,
            "missing fallback task keeps recovering beyond the ordinary destination retry budget");
        Check(controller.destinationId == 12 && !controller.IsCoolingDown(12, observation.nowMs),
            "ambient recovery never exhausts or cools down its fallback area");
    }

    observation.fallbackTaskActive = true;
    observation.nowMs = 33000;
    controller.Tick(config, observation);
    observation.fallbackTaskActive = false;
    observation.nowMs = 36000;
    Check(controller.Tick(config, observation).action == Action::None, "a newly lost healthy fallback task gets absence grace");
    observation.nowMs = 37000;
    observation.reevaluate = true;
    controller.Tick(config, observation);
    observation.reevaluate = false;
    observation.nowMs = 37499;
    Check(controller.Tick(config, observation).action == Action::None, "fallback absence grace lasts the full 1500 milliseconds");
    ++observation.nowMs;
    Check(controller.Tick(config, observation).action == Action::WanderFallback,
        "failed selection preserves the original fallback absence timer instead of delaying recovery");

    observation.blocked = true;
    observation.nowMs = 100000;
    const Decision blocked = controller.Tick(config, observation);
    Check(blocked.action == Action::None && !blocked.reevaluate && controller.state == State::Suspended,
        "higher-priority activity suppresses fallback movement and selection");
    observation.blocked = false;
    ++observation.nowMs;
    const Decision resumed = controller.Tick(config, observation);
    Check(resumed.action == Action::None && resumed.reevaluate, "priority release requests a fresh selection before ambient recovery");
    ++observation.nowMs;
    Check(controller.Tick(config, observation).action == Action::WanderFallback && controller.destinationId == 12,
        "failed selection after priority release restarts authored ambient wandering");
}
static void FailedTravelUsesAmbientFallback()
{
    Config config;
    config.noProgressMs = config.travelTimeoutMs + 1;
    Controller controller;
    auto observation = Observe(0);
    observation.taskActive = true;
    observation.fallbackDestinationId = 12;
    controller.Tick(config, observation);
    observation.nowMs = config.travelTimeoutMs;
    const Decision failed = controller.Tick(config, observation);
    Check(failed.action == Action::WanderFallback && failed.reevaluate && failed.failedDestination == 7 &&
        controller.state == State::Waiting && controller.destinationId == 12,
        "failed travel immediately returns to ambient fallback instead of standing still");
    Check(controller.IsCoolingDown(7, observation.nowMs) && !controller.IsCoolingDown(12, observation.nowMs),
        "failed travel cools down only the failed destination, preserving fallback availability");
    observation.fallbackTaskActive = true;
    observation.nowMs += config.selectionRetryMs;
    Check(controller.Tick(config, observation).action == Action::None && controller.destinationId == 12,
        "cooling-down destination cannot interrupt an active ambient fallback");
    observation.destinationId = 12;
    observation.distance = 30.0f;
    Check(controller.Tick(config, observation).action == Action::None && controller.state == State::Wandering,
        "reselection of the active fallback after travel failure does not restart travel");
}
int main()
{
    OpeningWindowsAndArrival();
    CandidateSelection();
    TravelArrivalAndWandering();
    ConfiguredAreaArrivalBoundary();
    BoundedFailureAndCooldown();
    UnchangedDestinationReevaluation();
    PriorityResumeClockAndClosure();
    AmbientFallbackSelectionAndPromotion();
    AmbientFallbackRecoveryAndPriority();
    FailedTravelUsesAmbientFallback();
    std::printf("routine_logic: %u checks passed\n", checks);
}
