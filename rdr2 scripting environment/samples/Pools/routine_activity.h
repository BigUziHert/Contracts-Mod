#pragma once
#include "routine_logic.h"

// Native-free activity lifecycle. Route selection still owns places; this owns
// one borrowed world point, never its population actor or the point itself.
namespace RoutineActivity
{
enum class Kind { None, Work, Eat, Drink, Social, Rest, Sleep };
enum class State { Ambient, Entering, Active, Exiting, Suspended };
enum class Command { None, Find, Exit, RecoverExit, Wander, Resume };
struct Point { int id = 0; Kind kind = Kind::None; unsigned hash = 0; };
struct Observation
{
    std::uint64_t nowMs = 0;
    Routine::Phase phase = Routine::Phase::Rest;
    int destination = -1;
    bool eligible = false, blocked = false, forceLeave = false;
    bool anyScenario = false, exitingScenario = false;
    bool pointActive = false, pointValid = false;
    bool entryTaskActive = true;
};
struct Controller
{
    State state = State::Ambient;
    Point point;
    bool confirmed = false;
    // Arrival can be 45m from the area centre and a point another 35m away.
    // Allow walking and its enter animation, while dropped tasks fail much sooner.
    static constexpr std::uint64_t kEntryMs = 120000, kExitMs = 8000;
    static constexpr std::uint64_t kRetryMs = 20000, kCooldownMs = 120000;

    bool IsCoolingDown(int id, std::uint64_t now) const
    {
        for (const auto& item : failures) if (item.id == id && now < item.until) return true;
        return false;
    }
    void Reject(int id, std::uint64_t now)
    {
        if (id <= 0) return;
        for (auto& item : failures) if (item.id == id) { item.until = now + kCooldownMs; return; }
        failures[nextFailure] = {id, now + kCooldownMs};
        nextFailure = (nextFailure + 1) % failures.size();
    }
    // Called exactly once in response to Find. No point means keep wandering and
    // retry later; failed points remain excluded across phase/interruption changes.
    bool Offer(Point candidate, const Observation& o)
    {
        nextTry = o.nowMs + kRetryMs;
        if (candidate.id <= 0 || IsCoolingDown(candidate.id, o.nowMs)) return false;
        point = candidate;
        confirmed = false;
        phase = o.phase; destination = o.destination;
        state = State::Entering;
        started = o.nowMs;
        missing = false;
        return true;
    }
    Command Leave(const Observation& o)
    {
        if (state == State::Exiting) return Command::None;
        if (o.anyScenario || o.exitingScenario || state == State::Entering)
        {
            state = State::Exiting; started = o.nowMs; recovered = false;
            return Command::Exit;
        }
        Clear(o.nowMs);
        return Command::Resume;
    }
    Command Tick(const Observation& o)
    {
        if (o.blocked) { state = State::Suspended; return Command::None; }
        if (state == State::Exiting)
        {
            if (!o.anyScenario && !o.exitingScenario)
            {
                Clear(o.nowMs); return Command::Resume;
            }
            if (Routine::Elapsed(o.nowMs, started) >= 2 * kExitMs)
            {
                Reject(point.id, o.nowMs);
                Clear(o.nowMs + kRetryMs);
                return Command::Wander; // bounded ordinary-task recovery, never warp
            }
            if (!recovered && Routine::Elapsed(o.nowMs, started) >= kExitMs)
            {
                recovered = true; return Command::RecoverExit;
            }
            return Command::None;
        }
        if (state == State::Suspended)
        {
            if (point.id > 0 && o.pointActive && o.pointValid && o.eligible &&
                phase == o.phase && destination == o.destination && !o.forceLeave)
            { state = State::Active; confirmed = true; return Command::None; }
            return Leave(o);
        }
        const bool changed = point.id > 0 && (phase != o.phase || destination != o.destination || !o.eligible);
        if (changed || o.forceLeave) return Leave(o);
        if (state == State::Entering || state == State::Active)
        {
            if (!o.pointValid)
            {
                Reject(point.id, o.nowMs);
                return Fail(o);
            }
            if (o.pointActive)
            {
                state = State::Active; confirmed = true; missing = false;
                return Command::None; // never restart a successful activity
            }
            if (state == State::Entering)
            {
                if (o.entryTaskActive) missing = false;
                else if (!missing) { missing = true; missingSince = o.nowMs; }
                if (Routine::Elapsed(o.nowMs, started) < kEntryMs &&
                    (!missing || Routine::Elapsed(o.nowMs, missingSince) < 1500)) return Command::None;
            }
            else
            {
                if (!missing) { missing = true; missingSince = o.nowMs; }
                if (Routine::Elapsed(o.nowMs, missingSince) < 1500) return Command::None;
            }
            Reject(point.id, o.nowMs);
            return Fail(o);
        }
        if (o.eligible && !o.anyScenario && !o.exitingScenario && o.nowMs >= nextTry)
            return Command::Find;
        return Command::None;
    }
private:
    struct Failure { int id = 0; std::uint64_t until = 0; };
    std::array<Failure, 64> failures{};
    std::size_t nextFailure = 0;
    std::uint64_t started = 0, nextTry = 0, missingSince = 0;
    Routine::Phase phase = Routine::Phase::Rest;
    int destination = -1;
    bool recovered = false, missing = false;
    void Clear(std::uint64_t retryAt)
    {
        point = {}; confirmed = false; state = State::Ambient; missing = false; nextTry = retryAt;
    }
    Command Fail(const Observation& o)
    {
        if (o.anyScenario || o.exitingScenario || state == State::Entering) return Leave(o);
        Clear(o.nowMs);
        return Command::Wander;
    }
};
}
