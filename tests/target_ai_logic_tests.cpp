#include "../rdr2 scripting environment/samples/Pools/target_ai_logic.h"

#include <cstdio>
#include <cstdlib>

using namespace TargetAI;

static void Check(bool condition, const char* description)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAILED: %s\n", description);
        std::exit(EXIT_FAILURE);
    }
}

static Observation Observe(std::uint64_t now, float distance, bool sight = false)
{
    Observation result;
    result.nowMs = now;
    result.distance = distance;
    result.clearLineOfSight = sight;
    return result;
}

static void Engage(Memory& memory, const Config& config, std::uint64_t now = 0)
{
    Observation observation = Observe(now, 10.0f, true);
    observation.provoked = true;
    Check(Step(memory, config, observation).action == Action::Engage, "provocation engages");
}

static void NormalEncounterAndEscape()
{
    const Config config;
    Memory memory;
    Check(Step(memory, config, Observe(0, 5.0f, true)).action == Action::None,
        "an unknown player can pass nearby without provoking combat");
    Engage(memory, config);
    for (std::uint64_t now = 1; now <= 10000; now += 16)
    {
        Observation observation = Observe(now, 30.0f, true);
        observation.combatTaskActive = observation.nativeInCombat = true;
        Check(Step(memory, config, observation).action == Action::None,
            "healthy combat never reissues its task");
    }
    Observation lost = Observe(11000, 80.0f);
    lost.combatTaskActive = lost.nativeInCombat = true;
    const std::uint64_t contact = memory.lastContactMs;
    lost.nowMs = contact + config.lossGraceMs - 1;
    Check(Step(memory, config, lost).action == Action::None, "LOS grace preserves pursuit");
    lost.nowMs++;
    Check(Step(memory, config, lost).action == Action::Search, "lost contact enters search once");
    lost.nowMs++;
    Check(Step(memory, config, lost).action == Action::None,
        "lingering engine combat flag cannot instantly cancel search");
    lost.nowMs = memory.stateSinceMs + config.searchMs;
    Check(Step(memory, config, lost).action == Action::Wander, "search expires into wandering");
    lost.nowMs++;
    Check(Step(memory, config, lost).action == Action::None, "wander task is not repeated");
    Check(memory.remembersPlayer, "target remembers the player after losing pursuit");
    Observation returned = Observe(lost.nowMs + 1, 44.0f, true);
    Check(Step(memory, config, returned).action == Action::Engage, "returning into sight re-engages");
}

static void SightHysteresisAndLastKnownPosition()
{
    const Config config;
    Memory memory;
    Engage(memory, config);
    Observation edge = Observe(10000, 50.0f, true);
    edge.combatTaskActive = true;
    Decision decision = Step(memory, config, edge);
    Check(decision.action == Action::None && decision.updateLastKnownPosition,
        "engaged target retains sight beyond its acquisition radius");
    Observation lost = Observe(18000, 56.0f);
    lost.combatTaskActive = true;
    decision = Step(memory, config, lost);
    Check(decision.action == Action::Search && !decision.updateLastKnownPosition,
        "search uses the last seen point, not the unseen current player position");
    edge.nowMs = 18001;
    Check(Step(memory, config, edge).action == Action::None,
        "a searching target does not reacquire in the hysteresis band");
    edge.nowMs++;
    edge.distance = config.acquireSightDist;
    Check(Step(memory, config, edge).action == Action::Engage, "acquisition boundary is inclusive");

    Observation close = Observe(edge.nowMs + config.lossGraceMs - 1, 5.0f);
    close.combatTaskActive = true;
    decision = Step(memory, config, close);
    Check(memory.state == State::Engaged && !decision.updateLastKnownPosition,
        "brief nearby occlusion gets grace without revealing a new search destination");
    ++close.nowMs;
    decision = Step(memory, config, close);
    Check(decision.action == Action::Search && !decision.updateLastKnownPosition,
        "nearby walls do not refresh contact indefinitely");
}

static void TaskRecoveryIsDebouncedAndThrottled()
{
    const Config config;
    Memory memory;
    Engage(memory, config);
    Observation dropped = Observe(100, 10.0f, true);
    Check(Step(memory, config, dropped).action == Action::None, "task dropout starts a grace period");
    dropped.nowMs = 1100;
    Check(Step(memory, config, dropped).action == Action::None, "retry interval also gates recovery");
    dropped.nowMs = 3000;
    Decision decision = Step(memory, config, dropped);
    Check(decision.action == Action::Engage && decision.taskRecovery, "sustained dropped task recovers");
    for (std::uint64_t now = 3001; now < 6000; ++now)
    {
        dropped.nowMs = now;
        Check(Step(memory, config, dropped).action == Action::None, "failed recovery is not spammed");
    }
    dropped.nowMs = 6000;
    Check(Step(memory, config, dropped).taskRecovery, "failed recovery can retry after its interval");

    dropped.nowMs = 6100;
    dropped.combatTaskActive = true;
    Check(Step(memory, config, dropped).action == Action::None, "restored task cancels dropout");
    dropped.nowMs = 20000;
    dropped.combatTaskActive = false;
    Check(Step(memory, config, dropped).action == Action::None,
        "a later dropout gets a fresh grace period even after the retry interval");
}

static void TemporaryIncapacitationAndNativeCombat()
{
    const Config config;
    Memory memory;
    Observation fallen = Observe(0, 70.0f);
    fallen.provoked = true;
    fallen.canAct = false;
    Check(Step(memory, config, fallen).action == Action::None, "do not assign combat during ragdoll");
    fallen.nowMs = 4000;
    fallen.provoked = false;
    fallen.canAct = true;
    Check(Step(memory, config, fallen).action == Action::Engage,
        "a damage event is remembered until the target can get up");
    fallen.nowMs = 20000;
    fallen.canAct = false;
    Check(Step(memory, config, fallen).action == Action::None, "do not replace tasks while incapacitated");
    fallen.nowMs++;
    fallen.canAct = true;
    Check(Step(memory, config, fallen).action == Action::Search, "expired contact grace is honored after recovery");

    Memory native;
    Observation fighting = Observe(0, 40.0f, true);
    fighting.nativeInCombat = fighting.combatTaskActive = true;
    Check(Step(native, config, fighting).action == Action::AdoptCombat,
        "adopt autonomous combat without replacing its task or weapon");
    fighting.nowMs = 10000;
    Check(Step(native, config, fighting).action == Action::None, "adoption does not repeat");
}

static void ContactResetsLossGraceAndSearchReacquires()
{
    Config config;
    config.lossGraceMs = 1000;
    config.searchMs = 2000;
    Memory memory;
    Engage(memory, config);
    Observation observation = Observe(900, 80.0f);
    observation.combatTaskActive = true;
    Check(Step(memory, config, observation).action == Action::None, "short contact loss does not transition");
    observation.nowMs = 950;
    observation.distance = 30.0f;
    observation.clearLineOfSight = true;
    Step(memory, config, observation);
    observation.nowMs = 1500;
    observation.distance = 80.0f;
    observation.clearLineOfSight = false;
    Check(Step(memory, config, observation).action == Action::None, "fresh LOS resets the full loss grace");
    observation.nowMs = 1950;
    Check(Step(memory, config, observation).action == Action::Search, "custom grace is respected");
    observation.nowMs = 3950;
    observation.distance = 30.0f;
    observation.clearLineOfSight = true;
    Check(Step(memory, config, observation).action == Action::Engage,
        "reacquisition wins over simultaneous search expiry");
    observation.nowMs = 1;
    observation.distance = 80.0f;
    observation.clearLineOfSight = false;
    Check(Step(memory, config, observation).action == Action::None,
        "clock rollback cannot underflow into immediate disengagement");
}

int main()
{
    NormalEncounterAndEscape();
    SightHysteresisAndLastKnownPosition();
    TaskRecoveryIsDebouncedAndThrottled();
    TemporaryIncapacitationAndNativeCombat();
    ContactResetsLossGraceAndSearchReacquires();
    std::puts("Target AI policy: 5 scenario groups passed.");
}
