#include <array>
#include <cstdio>
#include <cstdlib>
#include "../rdr2 scripting environment/samples/Pools/routine_activity.h"

using namespace RoutineActivity;
static unsigned checks = 0;
static void Check(bool result, const char* message)
{
    ++checks;
    if (!result) { std::fprintf(stderr, "FAILED: %s\n", message); std::exit(EXIT_FAILURE); }
}
static Observation At(int minute, std::uint64_t now = 1000)
{
    Observation o;
    o.nowMs = now; o.phase = Routine::PhaseAt(minute); o.destination = 12;
    o.eligible = o.pointValid = true;
    return o;
}
static Controller Start(Observation& o, Kind kind = Kind::Work)
{
    Controller c;
    Check(c.Tick(o) == Command::Find, "arrived and eligible target requests one activity search");
    Check(c.Offer({101, kind, 123}, o) && !c.confirmed, "usable point begins entry without inheriting confirmation from another point");
    o.pointActive = o.anyScenario = true;
    Check(c.Tick(o) == Command::None && c.state == State::Active && c.confirmed, "confirmed point entry starts a successful activity");
    return c;
}
static void SuccessfulActivityIsPreserved()
{
    for (const auto kind : {Kind::Work, Kind::Eat, Kind::Drink, Kind::Social, Kind::Rest, Kind::Sleep})
    {
        auto o = At(600); auto c = Start(o, kind);
        for (std::uint64_t elapsed : {1ULL, 1500ULL, 20000ULL, 300000ULL, 3600000ULL})
        {
            o.nowMs = 1000 + elapsed;
            Check(c.Tick(o) == Command::None && c.state == State::Active && c.point.id == 101,
                "healthy confirmed activity never restarts on polling, retry deadlines or elapsed real time");
        }
        o.eligible = false;
        Check(c.Tick(o) == Command::Exit && c.state == State::Exiting,
            "loss of a usable destination exits an otherwise healthy activity");
    }
    auto o = At(600); auto c = Start(o);
    o.destination = 13;
    Check(c.Tick(o) == Command::Exit, "a changed accepted destination leaves the old activity");
    o = At(600); c = Start(o); o.forceLeave = true;
    Check(c.Tick(o) == Command::Exit, "explicit route invalidation can end an otherwise healthy activity");
}
static void ScheduleBoundariesAndTimeSkips()
{
    for (int boundary : {180, 360, 660, 720, 960, 1140})
    {
        auto o = At(boundary - 1); auto c = Start(o);
        o.phase = Routine::PhaseAt(boundary); o.nowMs += 1;
        Check(c.Tick(o) == Command::Exit && c.state == State::Exiting,
            "every exact activity boundary requests exit before new activity or travel");
        o.anyScenario = o.pointActive = false;
        Check(c.Tick(o) == Command::Resume && c.state == State::Ambient && !c.confirmed,
            "completed exit hands routing back to current clock rather than the retired activity");
        Check(c.Tick(o) == Command::Find,
            "after routing makes the current destination eligible, the latest activity can be selected");
    }
    auto o = At(1439); auto c = Start(o, Kind::Social);
    o.phase = Routine::PhaseAt(0); o.nowMs += 1000;
    Check(c.Tick(o) == Command::None && c.state == State::Active,
        "midnight preserves a successful evening activity until 03:00");
    o.phase = Routine::PhaseAt(179); o.nowMs += 1000;
    Check(c.Tick(o) == Command::None, "02:59 remains evening rather than old midnight rest period");
    o.phase = Routine::PhaseAt(180);
    Check(c.Tick(o) == Command::Exit, "03:00 changes from social time to rest");

    struct Skip { int from, to; bool changed; };
    for (const auto& skip : std::array<Skip, 7>{{
        {600,800,false}, {600,700,true}, {700,800,true}, {800,1200,true},
        {1200,60,false}, {60,240,true}, {240,600,true}}})
    {
        o = At(skip.from); c = Start(o);
        o.phase = Routine::PhaseAt(skip.to); o.nowMs += 1000;
        Check(c.Tick(o) == (skip.changed ? Command::Exit : Command::None),
            "time skips adopt the activity appropriate now without replaying missed phases");
    }
}
static void UnavailableActivitiesKeepRetrying()
{
    auto o = At(660); Controller c;
    Check(c.Tick(o) == Command::Find && !c.Offer({}, o) && c.state == State::Ambient,
        "no unoccupied compatible activity leaves ambient behavior available");
    o.nowMs += Controller::kRetryMs - 1;
    Check(c.Tick(o) == Command::None, "unavailable point search waits for bounded retry instead of every frame");
    ++o.nowMs;
    Check(c.Tick(o) == Command::Find && c.Offer({102, Kind::Drink, 234}, o),
        "retry can choose another suitable lunch action such as drinking");
    o.pointValid = false;
    Check(c.Tick(o) == Command::Exit && c.IsCoolingDown(102, o.nowMs),
        "point deletion, occupancy, incompatibility or geometry invalidation rejects and cools down entry");
    o.pointActive = o.anyScenario = o.exitingScenario = false;
    Check(c.Tick(o) == Command::Resume, "entry failure without a physical scenario promptly resumes routing");
    Check(!c.Offer({102, Kind::Drink, 234}, o), "the same failed point cannot be selected again during cooldown");
    Check(c.Offer({103, Kind::Eat, 345}, o), "another suitable activity remains available while the failed point cools down");
    Check(!c.IsCoolingDown(102, o.nowMs + Controller::kCooldownMs), "failure cooldown eventually permits a repaired point");

    o = At(180); c = {}; o.anyScenario = true;
    Check(c.Tick(o) == Command::None, "unowned healthy ambient scenario is not displaced by a routine retry");
    o.anyScenario = false; o.exitingScenario = true;
    Check(c.Tick(o) == Command::None, "ambient scenario exit receives time before a new point search");
    o.exitingScenario = false; o.eligible = false;
    Check(c.Tick(o) == Command::None, "travelling, inaccessible or unaccepted area cannot assign an activity");
}
static void EntryAndExitRecoveryIsBounded()
{
    auto o = At(660); Controller c;
    Check(c.Offer({101, Kind::Eat, 123}, o), "lunch point can begin a walking scenario entry");
    o.nowMs += Controller::kEntryMs - 1;
    Check(c.Tick(o) == Command::None && c.state == State::Entering,
        "unconfirmed scenario entry is granted its full bounded approach allowance");
    ++o.nowMs;
    Check(c.Tick(o) == Command::Exit && c.IsCoolingDown(101, o.nowMs),
        "failed scenario entry cannot leave the target standing indefinitely");
    o.anyScenario = o.exitingScenario = true;
    const auto exitStarted = o.nowMs;
    for (std::uint64_t elapsed : {1ULL, 1000ULL, Controller::kExitMs - 1})
    {
        o.nowMs = exitStarted + elapsed;
        Check(c.Tick(o) == Command::None, "normal scenario exit is not replaced by travel or repeated clears");
    }
    o.nowMs = exitStarted + Controller::kExitMs;
    Check(c.Tick(o) == Command::RecoverExit, "stalled exit gets one gentle recovery after its fixed allowance");
    ++o.nowMs;
    Check(c.Tick(o) == Command::None, "exit recovery is not repeatedly reissued while native exit is running");
    o.nowMs = exitStarted + 2 * Controller::kExitMs;
    Check(c.Tick(o) == Command::Wander && c.state == State::Ambient && c.IsCoolingDown(101, o.nowMs),
        "persistent exit stall receives bounded ambient recovery without releasing target ownership");
    o.anyScenario = o.exitingScenario = false;
    Check(c.Tick(o) == Command::None, "exit-stall recovery does not immediately retry another scenario");
    o.nowMs += Controller::kRetryMs;
    Check(c.Tick(o) == Command::Find, "ambient recovery eventually retries a usable activity");

    o = At(600); c = Start(o);
    o.pointActive = o.anyScenario = false;
    Check(c.Tick(o) == Command::None, "brief active-scenario reporting gap does not thrash tasks");
    o.nowMs += 1499;
    Check(c.Tick(o) == Command::None, "dropped active scenario receives absence grace");
    ++o.nowMs;
    Check(c.Tick(o) == Command::Wander && c.IsCoolingDown(101, o.nowMs),
        "confirmed activity loss recovers wandering and avoids the failed point");

    o = At(660); c = {};
    Check(c.Offer({102, Kind::Drink, 234}, o), "a second lunch attempt begins normal point entry");
    o.nowMs += 2000; o.entryTaskActive = false;
    Check(c.Tick(o) == Command::None, "briefly unreported scenario-use task receives entry absence grace");
    o.nowMs += 1499;
    Check(c.Tick(o) == Command::None, "entry absence grace is shorter than the full walking allowance");
    ++o.nowMs; o.entryTaskActive = true;
    Check(c.Tick(o) == Command::None && c.state == State::Entering,
        "recovered scenario-use task clears a transient reporting gap without restarting entry");
    o.nowMs += 1000; o.entryTaskActive = false;
    Check(c.Tick(o) == Command::None, "a later missing entry task begins a new independent absence grace");
    o.nowMs += 1500;
    Check(c.Tick(o) == Command::Exit && c.IsCoolingDown(102, o.nowMs),
        "dropped entry task is rejected promptly instead of waiting the full two-minute approach allowance");
}
static void InterruptionPriorityAndLoading()
{
    // Native OR guards are covered by target-ai/runtime bridge tests. At the
    // activity boundary every reason must suppress Find/Exit/Wander equally.
    for (const char* interruption : {"combat", "search", "lasso", "hogtie", "capture", "handoff", "ragdoll", "loading"})
    for (const auto startingState : {State::Ambient, State::Entering, State::Active, State::Exiting})
    {
        (void)interruption;
        auto o = At(600); auto c = Start(o); c.state = startingState;
        o.blocked = true; o.forceLeave = true; o.pointValid = false;
        o.nowMs += 600000;
        Check(c.Tick(o) == Command::None && c.state == State::Suspended,
            "higher-priority interruption suppresses all activity commands even after recovery deadlines");
        o.phase = Routine::PhaseAt(700); o.nowMs += 600000;
        Check(c.Tick(o) == Command::None, "time skips while interrupted cannot issue routine tasks");
        o.blocked = o.forceLeave = false; o.pointValid = true;
        Check(c.Tick(o) == Command::Exit, "resume after loading or interruption leaves stale activity for current phase");
        o.pointActive = o.anyScenario = false;
        Check(c.Tick(o) == Command::Resume, "resume waits for old scenario exit before current-time routing");
    }
    auto o = At(600); auto c = Start(o); o.blocked = true;
    Check(c.Tick(o) == Command::None, "temporary loading interruption pauses ownership");
    o.blocked = false;
    Check(c.Tick(o) == Command::None && c.state == State::Active && c.point.id == 101,
        "same valid activity still observed after temporary loading is adopted without restarting it");
    o = At(600); c = Start(o); o.blocked = true; c.Tick(o);
    o.blocked = false; o.pointActive = o.anyScenario = false;
    Check(c.Tick(o) == Command::Resume && c.state == State::Ambient,
        "ended scenario after release resumes routing instead of inventing ongoing work");
}
int main()
{
    SuccessfulActivityIsPreserved(); ScheduleBoundariesAndTimeSkips(); UnavailableActivitiesKeepRetrying();
    EntryAndExitRecoveryIsBounded(); InterruptionPriorityAndLoading();
    std::printf("Routine activity policy: %u checks passed. Native navigation and animation require in-game testing.\n", checks);
}
