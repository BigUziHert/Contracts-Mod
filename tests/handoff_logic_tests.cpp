#include "../rdr2 scripting environment/samples/Pools/handoff_logic.h"

#include <cstdio>
#include <cstdlib>
#include <limits>

using namespace Handoff;

static void Check(bool condition, const char* description)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAILED: %s\n", description);
        std::exit(EXIT_FAILURE);
    }
}

static Observation Observe(std::uint64_t now, float giverPhase = 0.0f, float playerPhase = 0.0f)
{
    Observation observation;
    observation.nowMs = now;
    observation.actorsValid = true;
    observation.giverPlaying = true;
    observation.playerPlaying = true;
    observation.giverPhase = giverPhase;
    observation.playerPhase = playerPhase;
    return observation;
}

static bool Idle(const Decision& decision)
{
    return !decision.transfer && !decision.complete && !decision.cancel;
}

static void SuccessfulHandoff()
{
    Config config;
    Memory memory;
    memory.startedMs = 100;
    Check(Idle(Step(memory, config, Observe(100))), "both clips start without transferring");
    Check(Idle(Step(memory, config, Observe(200, 0.47f, 0.9f))), "receiver progress cannot transfer giver's card");
    Decision decision = Step(memory, config, Observe(300, 0.48f, 0.4f));
    Check(decision.transfer && !decision.complete && !decision.cancel, "giver contact transfers once");
    Check(Idle(Step(memory, config, Observe(400, 0.9f, 0.9f))), "transfer is never repeated");
    Check(Idle(Step(memory, config, Observe(500, 0.99f, 0.96f))), "both clips must finish");
    decision = Step(memory, config, Observe(600, 0.99f, 0.97f));
    Check(decision.complete && !decision.transfer && !decision.cancel, "received card opens after both finish");
}

static void PayoutUsesPlayerDonor()
{
    Config config;
    Memory memory;
    Observation observation = Observe(0, 0.9f, 0.2f);
    observation.payout = true;
    Check(Idle(Step(memory, config, observation)), "payout cannot use clerk phase as donor phase");
    observation.nowMs = 100;
    observation.playerPhase = config.transferPhase;
    Decision decision = Step(memory, config, observation);
    Check(decision.transfer && !decision.complete && !decision.cancel, "payout transfers from player");
}

static void BothMustActuallyStart()
{
    Config config;
    Memory memory;
    memory.startedMs = 100;
    Observation observation = Observe(100, 0.7f, 0.8f);
    observation.playerPlaying = false;
    observation.playerFinished = true;
    Check(Idle(Step(memory, config, observation)), "stale finished and phase do not prove receiver started");
    Check(memory.seenGiver && !memory.seenPlayer && memory.maxPlayerPhase == 0.0f,
        "only playing clips update startup evidence and phase");
    observation.nowMs = memory.startedMs + config.startGraceMs - 1;
    Check(Idle(Step(memory, config, observation)), "startup grace waits for second clip");
    observation.nowMs++;
    Check(Step(memory, config, observation).cancel, "missing receiver cancels at startup deadline");

    memory = Memory{};
    observation = Observe(0, 0.7f, 0.0f);
    observation.playerPlaying = false;
    Check(Idle(Step(memory, config, observation)), "one playing donor cannot transfer");
    observation.nowMs = 100;
    observation.playerPlaying = true;
    Check(Step(memory, config, observation).transfer, "delayed receiver starting within grace enables transfer");

    memory = Memory{};
    observation = Observe(config.startGraceMs);
    observation.giverPlaying = observation.playerPlaying = false;
    observation.giverFinished = observation.playerFinished = true;
    Decision decision = Step(memory, config, observation);
    Check(decision.cancel && !decision.complete && !decision.transfer, "two stale finished flags cannot complete unstarted clips");
}

static void InterruptedClipsCancel()
{
    Config config;
    for (int stoppedActor = 0; stoppedActor != 2; ++stoppedActor)
    {
        Memory memory;
        Step(memory, config, Observe(0, 0.1f, 0.1f));
        Observation observation = Observe(100, 1.0f, 1.0f);
        if (stoppedActor == 0)
            observation.giverPlaying = false;
        else
            observation.playerPlaying = false;
        Decision decision = Step(memory, config, observation);
        Check(decision.cancel && !decision.complete && !decision.transfer,
            "premature disappearance cancels even if inactive native phase is stale");
    }
    Memory memory;
    Step(memory, config, Observe(0, 0.6f, 0.6f));
    Observation observation = Observe(100);
    observation.giverPlaying = observation.playerPlaying = false;
    Check(Step(memory, config, observation).cancel, "both disappearing after transfer still cancels inspection");
}

static void SkippedEndFrames()
{
    Config config;
    Memory memory;
    Step(memory, config, Observe(0, 0.1f, 0.1f));
    Observation observation = Observe(100);
    observation.giverPlaying = observation.playerPlaying = false;
    observation.giverFinished = observation.playerFinished = true;
    Decision decision = Step(memory, config, observation);
    Check(decision.transfer && decision.complete && !decision.cancel,
        "native completion bridges skipped contact and end frames in transfer-before-open order");

    memory = Memory{};
    Step(memory, config, Observe(0, 0.1f, 0.1f));
    decision = Step(memory, config, Observe(100, 0.97f, 0.97f));
    Check(decision.transfer && decision.complete && !decision.cancel, "phase jump can transfer and finish together");

    memory = Memory{};
    Step(memory, config, Observe(0, 0.1f, 0.1f));
    observation = Observe(100, 0.0f, 0.4f);
    observation.giverPlaying = false;
    observation.giverFinished = true;
    Check(Idle(Step(memory, config, observation)), "finished donor waits for receiver and cannot transfer alone");
    observation.nowMs = 200;
    observation.giverFinished = false;
    observation.playerFinished = true;
    observation.playerPlaying = false;
    decision = Step(memory, config, observation);
    Check(decision.transfer && decision.complete && !decision.cancel,
        "native completion remains latched while waiting for the other actor");

    memory = Memory{};
    Step(memory, config, Observe(0, 0.98f, 0.8f));
    observation = Observe(100, 0.0f, 0.98f);
    observation.giverPlaying = false;
    decision = Step(memory, config, observation);
    Check(decision.complete && !decision.cancel, "previously witnessed finish phase survives clip ending");
}

static void InvalidActorsAndTimeout()
{
    Config config;
    Memory memory;
    Observation observation = Observe(0, 0.99f, 0.99f);
    observation.actorsValid = false;
    Decision decision = Step(memory, config, observation);
    Check(decision.cancel && !decision.complete && !decision.transfer, "invalid actors cancel before card movement");

    config.timeoutMs = 6200;
    memory.startedMs = 1000;
    observation = Observe(1000, 0.1f, 0.1f);
    Step(memory, config, observation);
    observation.nowMs = 7199;
    Check(Idle(Step(memory, config, observation)), "dynamic timeout allows long clips up to deadline");
    observation.nowMs = 7200;
    decision = Step(memory, config, observation);
    Check(decision.cancel && !decision.transfer && !decision.complete, "dynamic timeout cancels stuck clips at deadline");

    memory = Memory{};
    memory.startedMs = 1000;
    Check(Idle(Step(memory, config, Observe(999))), "earlier clock value does not underflow into timeout");
}

static void BadPhaseCannotComplete()
{
    Config config;
    Memory memory;
    const float invalidPhases[] = { -1.0f, 1.1f, std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::quiet_NaN() };
    for (float phase : invalidPhases)
        Check(Idle(Step(memory, config, Observe(0, phase, phase))), "invalid phase cannot transfer or finish");
    Check(memory.maxGiverPhase == 0.0f && memory.maxPlayerPhase == 0.0f, "invalid phases cannot poison maximum phase");
}

int main()
{
    SuccessfulHandoff();
    PayoutUsesPlayerDonor();
    BothMustActuallyStart();
    InterruptedClipsCancel();
    SkippedEndFrames();
    InvalidActorsAndTimeout();
    BadPhaseCannotComplete();
    std::puts("Handoff logic tests passed.");
    return EXIT_SUCCESS;
}
