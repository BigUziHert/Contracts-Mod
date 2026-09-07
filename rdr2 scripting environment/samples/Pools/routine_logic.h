#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

// Native-free policy. The bridge owns coordinates, opening-hour evidence and tasks.
namespace Routine
{
constexpr int kMinutesPerDay = 1440;
constexpr float kWalkMetresPerSecond = 1.0f;
constexpr double kWalkDetourFactor = 1.2;
constexpr std::uint64_t kMinimumTravelMs = 300000;
inline std::uint64_t TravelEstimateMs(double distance)
{
    if (!(distance > 0.0)) return 0;
    const double estimate = std::ceil(distance * 1000.0 * kWalkDetourFactor / kWalkMetresPerSecond);
    // The native walking task accepts a signed millisecond timeout.
    const auto maximum = static_cast<std::uint64_t>((std::numeric_limits<int>::max)());
    return estimate >= static_cast<double>(maximum) ? maximum : static_cast<std::uint64_t>(estimate);
}
struct Window { int startMinute = 0; int endMinute = 0; }; // Equal endpoints mean 24 hours.
// Keep existing phase indices stable for saved/in-memory route identities.
enum class Phase { Work, Shops, Leisure, Rest, Lunch };
constexpr int kPhaseCount = 5;
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
constexpr Phase PhaseAt(int minute, int /*legacyOffsetMinutes*/ = 0)
{
    // These are exact world-clock windows. Legacy habit offsets are deliberately
    // ignored, including when an old prepared plan survives a load/interruption.
    minute = NormalizeMinute(minute);
    if (minute < 180 || minute >= 1140) return Phase::Leisure;
    if (minute < 360) return Phase::Rest;
    if (minute >= 660 && minute < 720) return Phase::Lunch;
    return minute < 960 ? Phase::Work : Phase::Shops;
}
constexpr int NextPhaseMinute(int minute)
{
    minute = NormalizeMinute(minute);
    return minute < 180 ? 180 : minute < 360 ? 360 : minute < 660 ? 660 :
        minute < 720 ? 720 : minute < 960 ? 960 : minute < 1140 ? 1140 : 180;
}
constexpr Phase NextPhaseAt(int minute) { return PhaseAt(NextPhaseMinute(minute)); }
constexpr const char* PhaseName(Phase phase)
{
    switch (phase)
    {
    case Phase::Work: return "Work";
    case Phase::Lunch: return "Lunch";
    case Phase::Shops: return "Errands";
    case Phase::Leisure: return "Evening";
    case Phase::Rest: return "Rest";
    }
    return "Unknown";
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
enum class Action { None, Travel, Wander, WanderFallback };
struct Config
{
    float arrivalDistance = 4.0f;
    float progressDistance = 1.0f;
    std::uint64_t taskGraceMs = 1500;
    std::uint64_t retryMs = 4000;
    std::uint64_t noProgressMs = 20000;
    std::uint64_t travelTimeoutMs = kMinimumTravelMs; // Floor; long trips use the shared walking estimate.
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
    // A fixed, previously accepted authored area supplied by the bridge. This
    // activity is observed separately from the newly selected destination.
    int fallbackDestinationId = -1;
    bool fallbackTaskActive = false;
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
    std::uint64_t TripTimeoutMs() const { return tripTimeoutMs; }
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
            selectionPending = false;
            return {}; // Higher priority owns the ped; never submit routine tasks.
        }
        if (state == State::Suspended || clockChanged || observation.reevaluate)
        {
            // Keep the task's state observable while the bridge reselects. A pause
            // or fallback recheck may return this exact destination next frame.
            selectionState = state;
            selectionDestination = destinationId;
            selectionPending = true;
            if (state == State::Suspended)
            {
                state = State::Waiting;
                destinationId = -1;
                waiting = false;
                missingTask = false;
            }
            else if (state != State::Waiting)
            {
                waiting = false;
                missingTask = false;
            }
            // A failed recheck must not restart the fallback's absence grace
            // or make every selection attempt submit another wander task.
            return {Action::None, true, -1};
        }
        if (observation.destinationId < 0 || !observation.destinationAvailable ||
            !observation.destinationOpen || IsCoolingDown(observation.destinationId, observation.nowMs))
            return Wait(config, observation);

        if (state == State::Waiting && observation.destinationId == observation.fallbackDestinationId &&
            observation.fallbackTaskActive)
        {
            // This area already owns healthy native wandering or a scenario.
            // Accept it as the regular stop without walking back to its centre.
            state = State::Wandering;
            destinationId = observation.destinationId;
            selectionPending = waiting = missingTask = false;
            retries = 0;
            return {};
        }

        if (selectionPending)
        {
            selectionPending = false;
            if (selectionDestination == observation.destinationId && observation.taskActive &&
                (selectionState == State::Wandering || selectionState == State::Travelling))
            {
                state = selectionState;
                destinationId = selectionDestination;
                if (state == State::Wandering) retries = 0;
                return {};
            }
            state = State::Waiting;
        }
        if (destinationId != observation.destinationId || state == State::Waiting)
        {
            destinationId = observation.destinationId;
            state = State::Travelling;
            startedAtMs = progressAtMs = lastTaskAtMs = observation.nowMs;
            bestDistance = observation.distance;
            const auto estimate = TravelEstimateMs(observation.distance);
            tripTimeoutMs = estimate > config.travelTimeoutMs ? estimate : config.travelTimeoutMs;
            retries = 0;
            waiting = missingTask = false;
            if (observation.distance <= config.arrivalDistance) return Arrive(observation);
            return {Action::Travel, false, -1};
        }
        if (state == State::Travelling)
        {
            if (observation.distance <= config.arrivalDistance) return Arrive(observation);
            if (Elapsed(observation.nowMs, startedAtMs) >= tripTimeoutMs)
                return Fail(config, observation);
            if (observation.distance + config.progressDistance <= bestDistance)
            {
                bestDistance = observation.distance;
                progressAtMs = observation.nowMs;
            }
            const bool stalled = Elapsed(observation.nowMs, progressAtMs) >= config.noProgressMs;
            const bool dropped = TaskMissing(config, observation.nowMs, observation.taskActive);
            if ((stalled || dropped) && Elapsed(observation.nowMs, lastTaskAtMs) >= config.retryMs)
            {
                if (retries >= config.maxRetries) return Fail(config, observation);
                ++retries;
                progressAtMs = lastTaskAtMs = observation.nowMs;
                bestDistance = observation.distance;
                missingTask = false;
                return {Action::Travel, false, -1};
            }
            return {};
        }
        if (observation.taskActive) retries = 0;
        if (TaskMissing(config, observation.nowMs, observation.taskActive) &&
            Elapsed(observation.nowMs, lastTaskAtMs) >= config.retryMs)
        {
            if (retries >= config.maxRetries) return Fail(config, observation);
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
    std::uint64_t tripTimeoutMs = kMinimumTravelMs;
    float bestDistance = 0.0f;
    unsigned retries = 0;
    int lastMinute = -1;
    Phase lastPhase = Phase::Rest;
    bool missingTask = false, waiting = false;
    bool selectionPending = false;
    State selectionState = State::Waiting;
    int selectionDestination = -1;

    bool TaskMissing(const Config& config, std::uint64_t nowMs, bool taskActive)
    {
        if (taskActive) { missingTask = false; return false; }
        if (!missingTask) { missingTask = true; missingSinceMs = nowMs; }
        return Elapsed(nowMs, missingSinceMs) >= config.taskGraceMs;
    }
    Decision Arrive(const Observation& observation)
    {
        state = State::Wandering;
        lastTaskAtMs = observation.nowMs;
        missingTask = false;
        retries = 0;
        return {Action::Wander, false, -1};
    }
    Decision Wait(const Config& config, const Observation& observation)
    {
        const bool entering = state != State::Waiting || !waiting ||
            destinationId != observation.fallbackDestinationId;
        state = State::Waiting;
        destinationId = observation.fallbackDestinationId;
        selectionPending = false;
        const bool retry = !waiting || Elapsed(observation.nowMs, lastSelectionMs) >= config.selectionRetryMs;
        if (retry) lastSelectionMs = observation.nowMs;
        waiting = true;
        Action action = Action::None;
        if (destinationId < 0 || observation.fallbackTaskActive) missingTask = false;
        else if (entering || (TaskMissing(config, observation.nowMs, false) &&
            Elapsed(observation.nowMs, lastTaskAtMs) >= config.retryMs))
        {
            // Selection failure cannot exhaust ambient recovery and leave a
            // frozen ped. Missing tasks keep receiving throttled retries.
            action = Action::WanderFallback;
            lastTaskAtMs = observation.nowMs;
            missingTask = false;
        }
        return {action, retry, -1};
    }
    Decision Fail(const Config& config, const Observation& observation)
    {
        const int failed = destinationId;
        cooldowns[nextCooldown] = {failed, observation.nowMs + config.cooldownMs};
        nextCooldown = (nextCooldown + 1) % cooldowns.size();
        Decision decision = Wait(config, observation);
        decision.reevaluate = true;
        decision.failedDestination = failed;
        return decision;
    }
};
}
