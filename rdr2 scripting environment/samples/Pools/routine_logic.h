#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// Native-free policy. The bridge owns coordinates, opening-hour evidence and tasks.
namespace Routine
{
constexpr int kMinutesPerDay = 1440;
struct Window { int startMinute = 0; int endMinute = 0; }; // Equal endpoints mean 24 hours.
enum class Phase { Work, Shops, Leisure, Rest };
constexpr unsigned PhaseMask(Phase phase) { return 1u << static_cast<unsigned>(phase); }
constexpr int NormalizeMinute(int minute)
{
    return (minute % kMinutesPerDay + kMinutesPerDay) % kMinutesPerDay;
}
constexpr bool IsOpen(Window window, int minute)
{
    const int start = NormalizeMinute(window.startMinute), end = NormalizeMinute(window.endMinute);
    minute = NormalizeMinute(minute);
    return start == end || (start < end ? minute >= start && minute < end : minute >= start || minute < end);
}
constexpr int MinutesUntilClose(Window window, int minute)
{
    if (!IsOpen(window, minute)) return 0;
    if (NormalizeMinute(window.startMinute) == NormalizeMinute(window.endMinute)) return kMinutesPerDay;
    return NormalizeMinute(window.endMinute - NormalizeMinute(minute));
}
constexpr bool CanArriveAndStay(Window window, int minute, int travelMinutes, int minimumStay)
{
    if (travelMinutes < 0 || minimumStay < 0 || !IsOpen(window, minute)) return false;
    if (NormalizeMinute(window.startMinute) == NormalizeMinute(window.endMinute)) return true;
    const int remaining = MinutesUntilClose(window, minute);
    // Arrival at closing is never valid; leaving exactly at closing is valid.
    return travelMinutes < remaining && minimumStay <= remaining - travelMinutes;
}
constexpr int BoundedOffset(std::uint32_t seed, int maximumMinutes = 30)
{
    const int bound = maximumMinutes < 0 ? 0 : (maximumMinutes > 60 ? 60 : maximumMinutes);
    return static_cast<int>(seed % static_cast<unsigned>(2 * bound + 1)) - bound;
}
constexpr Phase PhaseAt(int minute, int offsetMinutes = 0)
{
    // Offset routine transitions only; never apply it to business opening hours.
    const int offset = offsetMinutes < -60 ? -60 : (offsetMinutes > 60 ? 60 : offsetMinutes);
    minute = NormalizeMinute(minute - offset);
    return minute < 360 ? Phase::Rest : minute < 840 ? Phase::Work :
        minute < 1080 ? Phase::Shops : Phase::Leisure;
}
struct Candidate
{
    int id = -1;
    unsigned phases = 0;
    unsigned occupations = 0; // Zero means suitable for any occupation.
    Window hours;
    bool available = true;
    bool fallback = false;
    int travelMinutes = 0;
};
inline int SelectDestination(const Candidate* candidates, std::size_t count, Phase phase,
    unsigned occupationMask, int minute, int minimumStay, std::uint32_t seed, int excludedId = -1)
{
    if (!candidates || count == 0) return -1;
    // Finite rotated scans: preferred phase first, then only explicitly authored fallbacks.
    const std::size_t first = seed % count;
    for (int pass = 0; pass < 2; ++pass)
        for (std::size_t step = 0; step < count; ++step)
        {
            const Candidate& candidate = candidates[(first + step) % count];
            if (candidate.id < 0 || candidate.id == excludedId || !candidate.available ||
                (candidate.occupations != 0 && (candidate.occupations & occupationMask) == 0) ||
                !CanArriveAndStay(candidate.hours, minute, candidate.travelMinutes, minimumStay)) continue;
            if (pass == 0 ? (candidate.phases & PhaseMask(phase)) != 0 : candidate.fallback)
                return candidate.id;
        }
    return -1;
}

enum class State { Travelling, Wandering, Waiting, Suspended };
enum class Action { None, Travel, Wander, Wait };
struct Config
{
    float arrivalDistance = 4.0f;
    float progressDistance = 1.0f;
    std::uint64_t taskGraceMs = 1500;
    std::uint64_t retryMs = 4000;
    std::uint64_t noProgressMs = 20000;
    std::uint64_t travelTimeoutMs = 300000;
    std::uint64_t cooldownMs = 60000;
    std::uint64_t selectionRetryMs = 5000;
    unsigned maxRetries = 2;
};
struct Observation
{
    std::uint64_t nowMs = 0; // Monotonic gameplay clock, with pauses already removed.
    int minute = 0;
    int scheduleOffset = 0;
    int destinationId = -1;
    bool destinationAvailable = true;
    bool destinationOpen = true;
    float distance = 0.0f;
    bool taskActive = false;
    // OR of combat, search, native combat/task, restraint/get-up/ragdoll,
    // unavailable player, unloaded navigation and any active interaction priority.
    bool blocked = false;
    bool reevaluate = false; // One-shot bridge request, e.g. changed world availability.
};
struct Decision
{
    Action action = Action::None;
    bool reevaluate = false;
    int failedDestination = -1;
};
constexpr std::uint64_t Elapsed(std::uint64_t now, std::uint64_t then)
{
    return now >= then ? now - then : 0;
}

struct Controller
{
    State state = State::Waiting;
    int destinationId = -1;

    void Reset() { *this = Controller{}; }
    bool IsCoolingDown(int id, std::uint64_t nowMs) const
    {
        for (const auto& cooldown : cooldowns)
            if (cooldown.id == id && nowMs < cooldown.untilMs) return true;
        return false;
    }
    Decision Tick(const Config& config, const Observation& observation)
    {
        const int minute = NormalizeMinute(observation.minute);
        const Phase phase = PhaseAt(minute, observation.scheduleOffset);
        const bool clockChanged = lastMinute >= 0 &&
            (phase != lastPhase || NormalizeMinute(minute - lastMinute) > 90);
        lastMinute = minute;
        lastPhase = phase;
        if (observation.blocked)
        {
            state = State::Suspended;
            missingTask = false;
            return {}; // Higher priority owns the ped; never even issue a wait task.
        }
        if (state == State::Suspended || clockChanged || observation.reevaluate)
        {
            state = State::Waiting;
            destinationId = -1;
            waiting = false;
            missingTask = false;
            return {Action::None, true, -1};
        }
        if (observation.destinationId < 0 || !observation.destinationAvailable ||
            !observation.destinationOpen || IsCoolingDown(observation.destinationId, observation.nowMs))
            return Wait(config, observation.nowMs);

        if (destinationId != observation.destinationId || state == State::Waiting)
        {
            destinationId = observation.destinationId;
            state = State::Travelling;
            startedAtMs = progressAtMs = lastTaskAtMs = observation.nowMs;
            bestDistance = observation.distance;
            retries = 0;
            waiting = missingTask = false;
            if (observation.distance <= config.arrivalDistance) return Arrive(observation);
            return {Action::Travel, false, -1};
        }
        if (state == State::Travelling)
        {
            if (observation.distance <= config.arrivalDistance) return Arrive(observation);
            if (Elapsed(observation.nowMs, startedAtMs) >= config.travelTimeoutMs)
                return Fail(config, observation.nowMs);
            if (observation.distance + config.progressDistance <= bestDistance)
            {
                bestDistance = observation.distance;
                progressAtMs = observation.nowMs;
            }
            const bool stalled = Elapsed(observation.nowMs, progressAtMs) >= config.noProgressMs;
            const bool dropped = TaskMissing(config, observation);
            if ((stalled || dropped) && Elapsed(observation.nowMs, lastTaskAtMs) >= config.retryMs)
            {
                if (retries >= config.maxRetries) return Fail(config, observation.nowMs);
                ++retries;
                progressAtMs = lastTaskAtMs = observation.nowMs;
                missingTask = false;
                return {Action::Travel, false, -1};
            }
            return {};
        }
        if (TaskMissing(config, observation) && Elapsed(observation.nowMs, lastTaskAtMs) >= config.retryMs)
        {
            if (retries >= config.maxRetries) return Fail(config, observation.nowMs);
            ++retries;
            // Recover a dropped area-wander task after the usual absence grace.
            state = State::Wandering;
            lastTaskAtMs = observation.nowMs;
            missingTask = false;
            return {Action::Wander, false, -1};
        }
        return {};
    }

private:
    struct Cooldown { int id = -1; std::uint64_t untilMs = 0; };
    std::array<Cooldown, 16> cooldowns{};
    std::size_t nextCooldown = 0;
    std::uint64_t startedAtMs = 0, progressAtMs = 0, lastTaskAtMs = 0;
    std::uint64_t missingSinceMs = 0, lastSelectionMs = 0;
    float bestDistance = 0.0f;
    unsigned retries = 0;
    int lastMinute = -1;
    Phase lastPhase = Phase::Rest;
    bool missingTask = false, waiting = false;

    bool TaskMissing(const Config& config, const Observation& observation)
    {
        if (observation.taskActive) { missingTask = false; return false; }
        if (!missingTask) { missingTask = true; missingSinceMs = observation.nowMs; }
        return Elapsed(observation.nowMs, missingSinceMs) >= config.taskGraceMs;
    }
    Decision Arrive(const Observation& observation)
    {
        state = State::Wandering;
        lastTaskAtMs = observation.nowMs;
        missingTask = false;
        retries = 0;
        return {Action::Wander, false, -1};
    }
    Decision Wait(const Config& config, std::uint64_t nowMs)
    {
        const bool needsTask = state != State::Waiting || !waiting;
        state = State::Waiting;
        destinationId = -1;
        missingTask = false;
        const bool retry = !waiting || Elapsed(nowMs, lastSelectionMs) >= config.selectionRetryMs;
        if (retry) lastSelectionMs = nowMs;
        waiting = true;
        return {needsTask ? Action::Wait : Action::None, retry, -1};
    }
    Decision Fail(const Config& config, std::uint64_t nowMs)
    {
        const int failed = destinationId;
        cooldowns[nextCooldown] = {failed, nowMs + config.cooldownMs};
        nextCooldown = (nextCooldown + 1) % cooldowns.size();
        Decision decision = Wait(config, nowMs);
        decision.reevaluate = true;
        decision.failedDestination = failed;
        return decision;
    }
};
}
