#pragma once

#include <cstdint>

// Decisions only: the game bridge owns native calls, last-known world coordinates,
// and a weapon choice made once per target. Call Step with a monotonic millisecond clock.
namespace TargetAI
{
enum class State { Wander, Engaged, Search };
enum class Action { None, Engage, AdoptCombat, Search, Wander };

struct Config
{
    float acquireSightDist = 45.0f;
    float retainSightDist = 55.0f;
    std::uint64_t lossGraceMs = 8000;
    std::uint64_t searchMs = 10000;
    std::uint64_t taskDropGraceMs = 1000;
    std::uint64_t taskRetryMs = 3000;
};

struct Observation
{
    std::uint64_t nowMs = 0;
    float distance = 0.0f;
    bool clearLineOfSight = false;
    // A current threat or a newly consumed player-damage event, never sticky damage history.
    bool provoked = false;
    bool nativeInCombat = false;
    // Include an active combat task that has not yet entered the engine's combat state.
    bool combatTaskActive = false;
    // False during ragdoll, getting up, or another temporary inability to accept a task.
    bool canAct = true;
};

struct Memory
{
    State state = State::Wander;
    bool remembersPlayer = false;
    std::uint64_t lastContactMs = 0;
    std::uint64_t stateSinceMs = 0;
    std::uint64_t lastTaskIssuedMs = 0;
    std::uint64_t taskMissingSinceMs = 0;
    bool taskMissing = false;
    bool previousNativeCombat = false;
    bool pendingEngagement = false;
};

struct Decision
{
    Action action = Action::None;
    // Copy the observed player position only when true. Search must not track an unseen player.
    bool updateLastKnownPosition = false;
    // An Engage request repairing a dropped task, not a new encounter or a weapon reroll.
    bool taskRecovery = false;
};

inline std::uint64_t Elapsed(std::uint64_t now, std::uint64_t then)
{
    return now >= then ? now - then : 0;
}

// Setup issues the initial wander task. Thereafter actions occur at transitions,
// apart from throttled recovery after a combat task has genuinely remained absent.
inline Decision Step(Memory& memory, const Config& config, const Observation& observation)
{
    Decision decision;
    const float retainDistance = config.retainSightDist >= config.acquireSightDist
        ? config.retainSightDist : config.acquireSightDist;
    const bool acquireSight = observation.clearLineOfSight &&
        observation.distance <= config.acquireSightDist;
    const bool retainSight = observation.clearLineOfSight &&
        observation.distance <= retainDistance;
    const bool nativeCombatStarted = observation.nativeInCombat && !memory.previousNativeCombat;
    memory.previousNativeCombat = observation.nativeInCombat;
    decision.updateLastKnownPosition = memory.state == State::Engaged ? retainSight : acquireSight;

    if (memory.state != State::Engaged)
    {
        // Do not lose a shot/antagonism event while the target is recovering from a fall.
        memory.pendingEngagement = memory.pendingEngagement || observation.provoked || nativeCombatStarted;
        if (observation.canAct && (memory.pendingEngagement || (memory.remembersPlayer && acquireSight)))
        {
            memory.state = State::Engaged;
            memory.remembersPlayer = true;
            memory.pendingEngagement = false;
            memory.lastContactMs = observation.nowMs;
            memory.stateSinceMs = observation.nowMs;
            memory.lastTaskIssuedMs = observation.nowMs;
            memory.taskMissing = false;
            decision.action = observation.nativeInCombat ? Action::AdoptCombat : Action::Engage;
            return decision;
        }

        if (memory.state == State::Search && observation.canAct &&
            Elapsed(observation.nowMs, memory.stateSinceMs) >= config.searchMs)
        {
            memory.state = State::Wander;
            memory.stateSinceMs = observation.nowMs;
            memory.taskMissing = false;
            decision.action = Action::Wander;
        }
        return decision;
    }

    // Neither engine combat state nor proximity alone is contact: both can persist
    // behind a wall after the player has escaped the target's sight.
    if (retainSight || observation.provoked)
        memory.lastContactMs = observation.nowMs;

    if (observation.canAct && Elapsed(observation.nowMs, memory.lastContactMs) >= config.lossGraceMs &&
        !retainSight && !observation.provoked)
    {
        memory.state = State::Search;
        memory.stateSinceMs = observation.nowMs;
        memory.taskMissing = false;
        decision.action = Action::Search;
        return decision;
    }

    if (!observation.canAct || observation.combatTaskActive)
    {
        memory.taskMissing = false;
        return decision;
    }

    if (!memory.taskMissing)
    {
        memory.taskMissing = true;
        memory.taskMissingSinceMs = observation.nowMs;
    }
    if (Elapsed(observation.nowMs, memory.taskMissingSinceMs) >= config.taskDropGraceMs &&
        Elapsed(observation.nowMs, memory.lastTaskIssuedMs) >= config.taskRetryMs)
    {
        memory.lastTaskIssuedMs = observation.nowMs;
        memory.taskMissing = false;
        decision.action = Action::Engage;
        decision.taskRecovery = true;
    }
    return decision;
}
}
