#pragma once

#include <cstdint>

// Native-free decisions for one paired animation. The bridge starts both clips in
// the same frame, polls only those clips, and stops calling Step after a terminal
// decision. Reset Memory and set startedMs when starting a new handoff.
namespace Handoff
{
struct Config
{
    std::uint64_t startGraceMs = 1500;
    // The game bridge replaces this with the longer clip duration plus 2000 ms.
    std::uint64_t timeoutMs = 10000;
    // Estimated contact phase, not a verified event in the selected animation.
    float transferPhase = 0.48f;
    float finishPhase = 0.97f;
};

struct Memory
{
    std::uint64_t startedMs = 0;
    bool seenGiver = false;
    bool seenPlayer = false;
    float maxGiverPhase = 0.0f;
    float maxPlayerPhase = 0.0f;
    bool transferred = false;
};

struct Observation
{
    std::uint64_t nowMs = 0;
    bool actorsValid = false;
    bool giverPlaying = false;
    bool playerPlaying = false;
    float giverPhase = 0.0f;
    float playerPhase = 0.0f;
    bool giverFinished = false;
    bool playerFinished = false;
    bool payout = false;
};

struct Decision
{
    bool transfer = false;
    bool complete = false;
    bool cancel = false;
};

inline Decision Step(Memory& memory, const Config& config, const Observation& observation)
{
    Decision decision;
    const std::uint64_t elapsed = observation.nowMs >= memory.startedMs
        ? observation.nowMs - memory.startedMs : 0;
    if (!observation.actorsValid || elapsed >= config.timeoutMs)
    {
        decision.cancel = true;
        return decision;
    }

    // A finished flag or stale phase alone cannot prove that our clip started.
    if (observation.giverPlaying)
    {
        memory.seenGiver = true;
        if (observation.giverPhase > memory.maxGiverPhase && observation.giverPhase <= 1.0f)
            memory.maxGiverPhase = observation.giverPhase;
    }
    if (observation.playerPlaying)
    {
        memory.seenPlayer = true;
        if (observation.playerPhase > memory.maxPlayerPhase && observation.playerPhase <= 1.0f)
            memory.maxPlayerPhase = observation.playerPhase;
    }
    // Preserve native completion if one actor finishes between sampled frames
    // and its finished flag disappears before the other actor reaches the end.
    if (memory.seenGiver && observation.giverFinished)
        memory.maxGiverPhase = 1.0f;
    if (memory.seenPlayer && observation.playerFinished)
        memory.maxPlayerPhase = 1.0f;

    const bool giverDone = memory.seenGiver &&
        (memory.maxGiverPhase >= config.finishPhase || observation.giverFinished);
    const bool playerDone = memory.seenPlayer &&
        (memory.maxPlayerPhase >= config.finishPhase || observation.playerFinished);
    const bool stoppedEarly =
        (memory.seenGiver && !observation.giverPlaying && !giverDone) ||
        (memory.seenPlayer && !observation.playerPlaying && !playerDone);
    if (stoppedEarly ||
        (elapsed >= config.startGraceMs && (!memory.seenGiver || !memory.seenPlayer)))
    {
        decision.cancel = true;
        return decision;
    }

    if (!memory.seenGiver || !memory.seenPlayer)
        return decision;

    const bool bothDone = giverDone && playerDone;
    const float donorPhase = observation.payout ? memory.maxPlayerPhase : memory.maxGiverPhase;
    if (!memory.transferred && (bothDone ||
        (observation.giverPlaying && observation.playerPlaying && donorPhase >= config.transferPhase)))
    {
        // A skipped frame may jump straight from before contact to both clips
        // finishing. Transfer first in that case, then open the received card.
        memory.transferred = true;
        decision.transfer = true;
    }
    decision.complete = bothDone && memory.transferred;
    return decision;
}
}
