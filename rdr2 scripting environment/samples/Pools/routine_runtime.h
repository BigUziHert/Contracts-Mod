#pragma once
#include "routine_plan.h"
#include "routine_spawn.h"
#include "routine_activity_bridge.h"

// Included after WaitUntil. The generated definition has stable storage; the cleanup
// hook resets only our routine data, never an ambient actor, game or vehicle.
struct RoutineRuntime
{
    bool enabled = false;
    RoutinePlan::Plan plan;
    std::array<std::string, 6> cardLines{};
    ContractDef definition{};
    int destination = -1;
    Vector3 centre{};
    float wanderRadius = RoutineData::kWanderRadius;
    int fallbackDestination = -1;
    Vector3 fallbackCentre{};
    bool ambientFallback = false;
    Routine::Controller controller;
    RoutineActivity::Controller activity;
    bool activityFallback = false;
    int activityPhase = -1;
    bool activityPointValid = false;
    ULONGLONG nextActivityValidationMs = 0;
    bool selectPending = false;
    bool resumeRequested = false;
    ULONGLONG nextValidationMs = 0;
    ULONGLONG nextStreamRequestMs = 0, fallbackRecheckMs = 0;
    ULONGLONG pauseSnapshotMs = 0;
    bool destinationValid = true;
};
static RoutineRuntime R;
static void ResetRoutine() { R = RoutineRuntime{}; }
static int RoutineMinute() { return CLOCK::GET_CLOCK_HOURS() * 60 + CLOCK::GET_CLOCK_MINUTES(); }
static bool IsRoutine(const ContractDef& def) { return R.enabled && &def == &R.definition; }

struct RoutineStartDiagnostic
{
    const char* stage = "none";
    const char* location = "none";
    Vector3 expected{}, actual{};
    int placementAttempts = 0;
    bool placementResult = false;
};
static RoutineStartDiagnostic routineStartDiagnostic;
static void ResetRoutineStartDiagnostic() { routineStartDiagnostic = {}; RoutineSpawn::diagnostic = {}; }
[[maybe_unused]] static void WriteRoutineStartDiagnostic(FILE* file)
{
    const auto& d = routineStartDiagnostic;
    const auto& s = RoutineSpawn::diagnostic;
    fprintf(file, "routine-start-v2 stage=%s location=%s check=%s attempts=%d placeResult=%d expected=%.3f,%.3f,%.3f actual=%.3f,%.3f,%.3f candidate=%.3f,%.3f,%.3f projected=%.3f,%.3f,%.3f ground=%.3f\n",
        d.stage, d.location, s.check, d.placementAttempts, d.placementResult ? 1 : 0,
        d.expected.x, d.expected.y, d.expected.z, d.actual.x, d.actual.y, d.actual.z,
        s.candidate.x, s.candidate.y, s.candidate.z, s.projected.x, s.projected.y, s.projected.z, s.ground);
}

static ModelSet RoutineModels(int town, unsigned occupation)
{
    // Reuse verified archetypes, narrowed to the occupation printed on the card.
    static constexpr Hash rhdLabor[] = { Joaat("a_m_m_rhdtownfolk_01_laborer"), Joaat("a_m_m_rhdforeman_01") };
    static constexpr Hash rhdStock[] = { Joaat("s_m_m_rhdcowpoke_01") };
    static constexpr Hash blwLabor[] = { Joaat("a_m_m_blwlaborer_01"), Joaat("a_m_m_blwlaborer_02") };
    static constexpr Hash blwStock[] = { Joaat("s_m_m_blwcowpoke_01") };
    static constexpr Hash valLabor[] = { Joaat("a_m_m_vallaborer_01") };
    static constexpr Hash valStock[] = { Joaat("s_m_m_valcowpoke_01"), Joaat("a_m_m_valfarmer_01") };
    static constexpr Hash strLabor[] = { Joaat("a_m_m_strlaborer_01") };
    static constexpr Hash strStock[] = { Joaat("s_m_m_strcowpoke_01"), Joaat("s_m_m_liveryworker_01") };
    static constexpr Hash sdLabor[] = { Joaat("a_m_m_sdlaborers_02"), Joaat("a_m_m_nbxlaborers_01") };
    static constexpr Hash vhtDock[] = { Joaat("a_m_m_vhtboatcrew_01") };
    static constexpr Hash asbLabor[] = { Joaat("a_m_m_asbtownfolk_01_laborer") };
    if (town < 0 || town >= RoutineData::kTownCount) return { nullptr, 0 };
    const bool stock = occupation == RoutineData::LivestockHand;
    switch (RoutineData::kTowns[town].id)
    {
    case RoutineData::TownId::Rhodes: return stock ? Models(rhdStock) : Models(rhdLabor);
    case RoutineData::TownId::Blackwater: return stock ? Models(blwStock) : Models(blwLabor);
    case RoutineData::TownId::Valentine: return stock ? Models(valStock) : Models(valLabor);
    case RoutineData::TownId::Strawberry: return stock ? Models(strStock) : Models(strLabor);
    case RoutineData::TownId::SaintDenis: return occupation == RoutineData::Laborer ? Models(sdLabor) : Models(SD_DOCK);
    case RoutineData::TownId::VanHorn: return Models(vhtDock);
    case RoutineData::TownId::Annesburg: return Models(asbLabor);
    }
    return { nullptr, 0 };
}

static bool PrepareRoutineContract(RoutineRuntime& prepared)
{
    ResetRoutineStartDiagnostic();
    routineStartDiagnostic.stage = "select_location";
    if (!CanStartInteraction()) return RoutineSpawn::Reject("interaction_interrupted");
    const unsigned seed = (static_cast<unsigned>(rand()) << 16) ^ static_cast<unsigned>(rand());
    const int firstTown = static_cast<int>(seed % RoutineData::kTownCount);
    constexpr unsigned kMaximumCandidatePrepares = 8;
    constexpr ULONGLONG kCandidatePassMs = 6000;
    const ULONGLONG started = GetTickCount64();
    unsigned attempts = 0;
    bool attempted[RoutineData::kLocationCount]{};
    // Exhaust distinct authored choices before retrying in a later startup pass.
    // Each pass has finite native work; individual streaming waits remain bounded.
    for (int townStep = 0; townStep < RoutineData::kTownCount; ++townStep)
    {
        const int town = (firstTown + townStep) % RoutineData::kTownCount;
        const unsigned townSeed = townStep == 0 ? seed : RoutinePlan::Mix(seed ^ static_cast<unsigned>(townStep));
        const unsigned occupation = RoutinePlan::GeneratedOccupation(town, townSeed);
        RoutineRuntime candidate;
        if (!RoutinePlan::Build(candidate.plan, town, occupation, townSeed)) continue;
        const int first = static_cast<int>(townSeed % RoutineData::kLocationCount);
        int minute = RoutineMinute();
        for (int choice = 0; choice < RoutineData::kLocationCount + 2; ++choice)
        {
            const int phase = static_cast<int>(Routine::PhaseAt(minute, candidate.plan.offsetMinutes));
            const int id = choice == 0 ? candidate.plan.route[phase] : choice == 1
                ? candidate.plan.route[static_cast<int>(Routine::Phase::Rest)]
                : (first + choice - 2) % RoutineData::kLocationCount;
            const auto& location = RoutineData::kLocations[id];
            const bool allDayRest = location.kind == RoutineData::PlaceKind::Rest && location.openMinute == location.closeMinute;
            if (attempted[id] || !location.enabled || location.town != RoutineData::kTowns[town].id ||
                (location.occupations & occupation) == 0 ||
                (!RoutineData::SupportsPhase(location, static_cast<Routine::Phase>(phase)) && !allDayRest) ||
                !Routine::CanArriveAndStay({location.openMinute, location.closeMinute}, minute, 0, 15)) continue;
            if (!CanStartInteraction()) return RoutineSpawn::Reject("interaction_interrupted");
            if (attempts >= kMaximumCandidatePrepares || Routine::Elapsed(GetTickCount64(), started) >= kCandidatePassMs)
                return RoutineSpawn::Reject("candidate_budget_exhausted");
            attempted[id] = true;
            ++attempts;
            routineStartDiagnostic.location = location.id;
            routineStartDiagnostic.expected = location.anchor;
            StartupTrace::Record("candidate_prepare_begin", 0, 0, &location.anchor, -1, location.id);
            Vector3 point;
            const bool safe = RoutineSpawn::Prepare(location.anchor, location.candidateRadius,
                location.maxHeightDelta, townSeed, point);
            if (!CanStartInteraction()) return RoutineSpawn::Reject("interaction_interrupted");
            minute = RoutineMinute(); // Streaming can advance the clock before validation finishes.
            const bool stillScheduled = allDayRest || RoutineData::SupportsPhase(location, Routine::PhaseAt(minute));
            if (safe && stillScheduled &&
                Routine::CanArriveAndStay({location.openMinute, location.closeMinute}, minute, 0, 15))
            {
                // Startup may replace a habit only before publishing the immutable
                // route, so the card always names the destination actually accepted.
                const auto acceptedPhase = Routine::PhaseAt(minute);
                candidate.plan.route[RoutineData::SupportsPhase(location, acceptedPhase)
                    ? static_cast<int>(acceptedPhase) : static_cast<int>(Routine::Phase::Rest)] = id;
                candidate.enabled = true;
                candidate.cardLines = RoutinePlan::CardLines(candidate.plan);
                candidate.destination = id;
                candidate.centre = point;
                candidate.fallbackDestination = id;
                candidate.fallbackCentre = point;
                routineStartDiagnostic.expected = point;
                candidate.wanderRadius = location.wanderRadius;
                const auto& area = RoutineData::kTowns[town];
                candidate.definition = { area.name, RoutinePlan::OccupationName(occupation), area.name,
                    point, location.wanderRadius, RoutineModels(town, occupation), &kHumanTarget, nullptr, ResetRoutine };
                prepared = candidate;
                StartupTrace::Record("candidate_prepared", 0, 0, &point, -1, location.id);
                return true;
            }
            if (safe) RoutineSpawn::Reject(stillScheduled ? "visiting_window_closed" : "schedule_changed");
            StartupTrace::Record("candidate_rejected", 0, 0, &location.anchor, -1, RoutineSpawn::diagnostic.check);
        }
    }
    return RoutineSpawn::Reject("no_available_locations");
}

static bool ValidateRoutineDeployment(Ped ped, const ContractDef& def)
{
    if (!IsRoutine(def)) return true;
    const auto& location = RoutineData::kLocations[R.destination];
    routineStartDiagnostic.stage = "revalidate_destination";
    // Capture can outlast distant streaming residency. Reload once, bounded, while
    // the photographed subject remains hidden; recheck both actors after any yield.
    if (!RoutineSpawn::EnsureLoaded(def.spawn)) return false;
    if (!LivingPed(ped) || !CanStartInteraction()) return RoutineSpawn::Reject("interaction_interrupted");
    if (!Routine::CanArriveAndStay({location.openMinute, location.closeMinute}, RoutineMinute(), 0, 15))
        return RoutineSpawn::Reject("visiting_window_closed");
    return RoutineSpawn::ValidatePoint(location.anchor, location.candidateRadius, location.maxHeightDelta, def.spawn, ped);
}

static bool StartRoutineWander(Ped ped, const ContractDef& def)
{
    (void)ped;
    if (!IsRoutine(def)) return false;
    // Setup and the end of search ask the controller to reconsider the current clock.
    // Do not send an obsolete spawn-centred task before the new destination is selected.
    R.resumeRequested = true;
    return true;
}

static bool ValidateRoutinePlacement(Ped ped, const ContractDef& def)
{
    if (!IsRoutine(def)) return true;
    const Vector3 actual = ENTITY::GET_ENTITY_COORDS(ped, true, false);
    routineStartDiagnostic.actual = actual;
    const float dx = actual.x - def.spawn.x, dy = actual.y - def.spawn.y, height = actual.z - def.spawn.z;
    // Entity coordinates describe the ped's origin, not the surface under his feet.
    // camp_horseshoeoverlook.c:4372 uses separate .5/.5/2.0 ped/scenario tolerances.
    if (!std::isfinite(actual.x) || !std::isfinite(actual.y) || !std::isfinite(actual.z) ||
        dx * dx + dy * dy > .75f * .75f || height < -.25f || height > 2.0f)
        return RoutineSpawn::Reject("ped_position_out_of_bounds");
    if (!RoutineSpawn::Loaded(actual)) return RoutineSpawn::Reject("ped_collision_or_nav_unloaded");
    const auto& location = RoutineData::kLocations[R.destination];
    return RoutineSpawn::ValidatePoint(location.anchor, location.candidateRadius, location.maxHeightDelta,
        Vector3(actual.x, actual.y, def.spawn.z), ped);
}

static bool WaitForRoutinePlacement(Ped ped, const ContractDef& def)
{
    routineStartDiagnostic.stage = "settle_ped";
    routineStartDiagnostic.expected = def.spawn;
    ULONGLONG nextAttemptMs = 0;
    bool accepted = false;
    WaitUntil(1500, [&] {
        if (!LivingPed(ped) || !CanStartInteraction())
        {
            RoutineSpawn::Reject("interaction_interrupted");
            return true; // stop waiting; the caller still receives failure
        }
        if (ped != ownedPed.ped || !OwnedPedIdentityMatches())
        {
            RoutineSpawn::Reject("subject_ownership_changed");
            return true;
        }
        const ULONGLONG now = GetTickCount64();
        if (now < nextAttemptMs) return false;
        nextAttemptMs = now + 100;
        ++routineStartDiagnostic.placementAttempts;
        routineStartDiagnostic.placementResult = ENTITY::PLACE_ENTITY_ON_GROUND_PROPERLY(ped, 1) != 0;
        // The return is useful diagnostics, not proof of invalid geometry. Some R*
        // callers ignore it; others retry. Actual settled position decides success.
        accepted = ValidateRoutinePlacement(ped, def);
        return accepted;
    });
    return accepted;
}

static int RoutineTravelMinutes(const Vector3& from, const Vector3& to, float arrivalRadius = RoutineData::kWanderRadius)
{
    const double centreDistance = std::sqrt(static_cast<double>(DistSq(from, to)));
    // A visit starts at the edge of its wandering area, so its visiting window
    // only needs to allow the walk to that area, not an extra trip to the centre.
    if (centreDistance <= arrivalRadius) return 0;
    // Read the actual game-clock rate (act_caunc_rustling.c:24960), never change it.
    const int rate = CLOCK::GET_MILLISECONDS_PER_GAME_MINUTE();
    if (rate <= 0) return Routine::kMinutesPerDay; // only all-day fallbacks remain eligible
    const double distance = centreDistance - arrivalRadius;
    const double estimate = std::ceil(static_cast<double>(Routine::TravelEstimateMs(distance)) / rate);
    return static_cast<int>(estimate > Routine::kMinutesPerDay ? Routine::kMinutesPerDay : estimate);
}

static void SelectRoutineDestination(Ped ped, const Vector3& position, int minute, ULONGLONG now, bool ambientActive = false)
{
    Routine::Candidate candidates[Routine::kPhaseCount];
    RoutinePlan::Candidates(R.plan, candidates);
    for (auto& candidate : candidates)
    {
        if (candidate.id < 0) continue;
        if (candidate.phases == Routine::PhaseMask(Routine::Phase::Work))
            candidate.hours = minute < 660 ? Routine::Window{360, 660} : Routine::Window{720, 960};
        if (candidate.phases == Routine::PhaseMask(Routine::Phase::Shops)) candidate.hours = {960, 1140};
        const auto& location = RoutineData::kLocations[candidate.id];
        candidate.available = candidate.available && !R.controller.IsCoolingDown(candidate.id, now);
        candidate.travelMinutes = RoutineTravelMinutes(position, location.anchor, location.wanderRadius);
    }
    const int previousDestination = R.destination;
    bool accepted = false;
    // At most the authored phase and its all-day fallback. No waits or scene takeover
    // in the per-frame bridge; an unloaded preferred area receives a collision request.
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        const int id = Routine::SelectDestination(candidates, Routine::kPhaseCount, Routine::PhaseAt(minute),
            R.plan.occupation, minute, 0, R.plan.seed);
        if (id < 0) break;
        const auto& location = RoutineData::kLocations[id];
        if (!RoutineSpawn::Loaded(location.anchor))
        {
            STREAMING::REQUEST_COLLISION_AT_COORD(location.anchor);
            PATH::ADD_NAVMESH_REQUIRED_REGION(location.anchor.x, location.anchor.y, 50.0f);
        }
        // An already running wander/scenario is stronger evidence that the visit
        // can continue than another spawn-point query at its now-distant centre.
        const bool heldAmbient = ambientActive && id == R.fallbackDestination &&
            DistSq(R.centre, R.fallbackCentre) < .01f;
        if (id == previousDestination && (heldAmbient || RoutineSpawn::ValidatePoint(location.anchor, location.candidateRadius,
            location.maxHeightDelta, R.centre, ped, R.controller.state != Routine::State::Wandering)))
        {
            accepted = true;
            break; // Keep the exact centre/radius and the healthy task using them.
        }
        Vector3 point;
        // A rejected endpoint does not condemn its entire authored area. Try the
        // other bounded points even when this was the previous destination.
        if (RoutineSpawn::Find(location.anchor, location.candidateRadius, location.maxHeightDelta, R.plan.seed, point, ped))
        {
            R.destination = id;
            R.centre = point;
            R.wanderRadius = location.wanderRadius;
            accepted = true;
            break;
        }
        for (auto& candidate : candidates) if (candidate.id == id) candidate.available = false;
    }
    // Failed selection retains the authored location until the controller falls
    // back to the last arrived area. Never publish Stop: None for a live routine.
    R.destinationValid = accepted;
    R.ambientFallback = !accepted;
    R.nextValidationMs = now + 1000;
    R.fallbackRecheckMs = now + 60000;
    R.selectPending = false;
}

static bool RoutineTaskActive(Ped ped)
{
    if (R.activity.state == RoutineActivity::State::Entering) return true;
    if (R.activity.state == RoutineActivity::State::Active && RoutineActivityBridge::Active(ped, R.activity.point, R.activity.confirmed)) return true;
    // Area wandering can use world scenarios. That pause is healthy only after
    // arrival; an unrelated scenario must not hide failed travel indefinitely.
    if ((R.controller.state == Routine::State::Wandering || R.ambientFallback) && PED::IS_PED_USING_ANY_SCENARIO(ped)) return true;
    const Hash task = R.controller.state == Routine::State::Travelling
        ? joaat("SCRIPT_TASK_FOLLOW_NAV_MESH_TO_COORD") : joaat("SCRIPT_TASK_WANDER_IN_AREA");
    const int status = TASK::GET_SCRIPT_TASK_STATUS(ped, task, true);
    return status == 0 || status == 1;
}

static bool RoutineActivityObserved(Ped ped)
{
    return R.activity.state == RoutineActivity::State::Active && R.activityPointValid &&
        RoutineActivityBridge::Active(ped, R.activity.point, R.activity.confirmed);
}

static void RoutineWanderAtAcceptedArea(Ped ped)
{
    TASK::SET_PED_PATH_PREFER_TO_AVOID_WATER(ped, true, RoutineData::kWanderRadius);
    TASK::SET_PED_PATH_MAY_ENTER_WATER(ped, false);
    TASK::TASK_WANDER_IN_AREA(ped, R.fallbackCentre, RoutineData::kWanderRadius, 0.0f, 0.0f, 1);
    PED::SET_PED_KEEP_TASK(ped, true);
}

// Returns true while activity entry/exit owns the task slot. Higher priorities
// bypass this dispatcher entirely, including clear/exit hints and discovery.
static bool ApplyRoutineActivity(Ped ped, RoutineActivity::Command command,
    const RoutineActivity::Observation& observation)
{
    switch (command)
    {
    case RoutineActivity::Command::Find:
    {
        const auto point = RoutineActivityBridge::Find(ped, RoutineData::kLocations[R.destination],
            observation.phase, R.plan.occupation, R.plan.seed, R.activity, observation.nowMs);
        R.activityFallback = !R.activity.Offer(point, observation);
        if (!R.activityFallback)
        {
            R.activityPointValid = true;
            R.nextActivityValidationMs = observation.nowMs + 1000;
            RoutineActivityBridge::Start(ped, point);
            PED::SET_PED_KEEP_TASK(ped, true);
            return true;
        }
        return false;
    }
    case RoutineActivity::Command::Exit:
        PED::SET_PED_SHOULD_PLAY_NORMAL_SCENARIO_EXIT(ped);
        // Ordinary clear permits the engine's exit; do not start walking in this frame.
        TASK::CLEAR_PED_TASKS(ped, true, false);
        return true;
    case RoutineActivity::Command::RecoverExit:
        PED::SET_PED_SHOULD_PLAY_NORMAL_SCENARIO_EXIT(ped);
        TASK::CLEAR_PED_TASKS(ped, true, false);
        return true;
    case RoutineActivity::Command::Resume:
        R.controller.state = Routine::State::Suspended;
        R.resumeRequested = false; // the suspended route requests selection below
        R.activityPointValid = false;
        return false;
    case RoutineActivity::Command::Wander:
        R.activityFallback = true;
        R.activityPointValid = false;
        RoutineWanderAtAcceptedArea(ped);
        R.controller.state = Routine::State::Suspended;
        R.resumeRequested = true;
        return true;
    case RoutineActivity::Command::None: break;
    }
    return R.activity.state == RoutineActivity::State::Exiting;
}

static void UpdateRoutine(Ped ped, const ContractDef& def, bool mayAct)
{
    if (!IsRoutine(def)) return;
    Routine::Config config;
    Routine::Observation observation;
    observation.nowMs = RuntimeNowMs();
    observation.minute = RoutineMinute();
    observation.scheduleOffset = R.plan.offsetMinutes;
    const Vector3 position = ENTITY::GET_ENTITY_COORDS(ped, true, false);
    const bool loaded = RoutineSpawn::Loaded(position);
    observation.blocked = !mayAct || !PlayerAvailable() || !loaded;
    RoutineActivity::Observation activityObservation;
    activityObservation.nowMs = observation.nowMs;
    activityObservation.phase = Routine::PhaseAt(observation.minute);
    activityObservation.blocked = observation.blocked;
    bool ambientScenario = false;
    if (!loaded && observation.nowMs >= R.nextStreamRequestMs)
    {
        STREAMING::REQUEST_COLLISION_AT_COORD(position);
        PATH::ADD_NAVMESH_REQUIRED_REGION(position.x, position.y, 50.0f);
        R.nextStreamRequestMs = observation.nowMs + 1000;
    }
    if (!observation.blocked)
    {
        ambientScenario = PED::IS_PED_USING_ANY_SCENARIO(ped);
        const int wanderStatus = TASK::GET_SCRIPT_TASK_STATUS(ped, joaat("SCRIPT_TASK_WANDER_IN_AREA"), true);
        const bool holdingArea = R.destination == R.fallbackDestination && R.fallbackDestination >= 0 &&
            DistSq(R.centre, R.fallbackCentre) < .01f && (R.controller.state == Routine::State::Wandering ||
                R.ambientFallback || R.controller.state == Routine::State::Suspended);
        // A scenario encountered partway through travel does not prove arrival
        // at the saved fallback. Only that area's owned ambient task is adopted.
        const bool ambientActive = holdingArea && (ambientScenario || wanderStatus == 0 || wanderStatus == 1);
        observation.fallbackTaskActive = ambientActive;
        const Vector3 previousCentre = R.centre;
        if (R.selectPending) SelectRoutineDestination(ped, position, observation.minute, observation.nowMs, ambientActive);
        const bool centreChanged = DistSq(previousCentre, R.centre) >= .01f;
        if (centreChanged) observation.fallbackTaskActive = false;
        observation.reevaluate = R.resumeRequested || R.pauseSnapshotMs != pausedDurationMs;
        R.pauseSnapshotMs = pausedDurationMs;
        R.resumeRequested = false;
        if (R.destination >= 0)
        {
            const auto& location = RoutineData::kLocations[R.destination];
            if (!R.ambientFallback && observation.nowMs >= R.nextValidationMs)
            {
                // The centre was validated before deployment/arrival. Once this
                // area's native wandering runs, don't stop it over a fresh query
                // of ground or collision at a point the ped has already left.
                const bool arrivedArea = R.destination == R.fallbackDestination &&
                    DistSq(R.centre, R.fallbackCentre) < .01f && R.controller.state == Routine::State::Wandering;
                R.destinationValid = location.enabled && (arrivedArea || RoutineSpawn::ValidatePoint(location.anchor,
                    location.candidateRadius, location.maxHeightDelta, R.centre, ped));
                R.nextValidationMs = observation.nowMs + 1000;
            }
            observation.destinationOpen = Routine::CanArriveAndStay({location.openMinute, location.closeMinute},
                observation.minute, R.controller.state == Routine::State::Travelling ? RoutineTravelMinutes(position, R.centre, R.wanderRadius) : 0, 0);
            const int preferred = R.plan.route[static_cast<int>(Routine::PhaseAt(observation.minute, R.plan.offsetMinutes))];
            // Optional availability retries can wait for a native ambient pause to
            // end. Real schedule changes, closed hours and priority still win.
            if (!R.ambientFallback && R.destination != preferred && R.controller.state != Routine::State::Travelling && !ambientScenario &&
                observation.nowMs >= R.fallbackRecheckMs)
            {
                observation.reevaluate = true;
                R.fallbackRecheckMs = observation.nowMs + 60000;
            }
        }
        // A new point inside the same authored area still needs a new movement
        // endpoint; an active task aimed at the rejected old point cannot be kept.
        observation.taskActive = !centreChanged && RoutineTaskActive(ped);
        activityObservation.destination = R.destination;
        activityObservation.eligible = !R.ambientFallback && R.destinationValid &&
            R.controller.state == Routine::State::Wandering &&
            R.controller.destinationId == R.destination && R.destination == R.fallbackDestination &&
            DistSq(R.centre, R.fallbackCentre) < .01f &&
            RoutineData::SupportsPhase(RoutineData::kLocations[R.destination], activityObservation.phase);
        activityObservation.anyScenario = RoutineActivityBridge::Busy(ped);
        activityObservation.exitingScenario = RoutineActivityBridge::Exiting(ped);
        activityObservation.pointActive = RoutineActivityBridge::Active(ped, R.activity.point, R.activity.confirmed);
        activityObservation.entryTaskActive = TASK::PED_HAS_USE_SCENARIO_TASK(ped);
        if (R.activity.point.id > 0 && observation.nowMs >= R.nextActivityValidationMs)
        {
            R.activityPointValid = RoutineActivityBridge::Valid(ped, R.activity.point, activityObservation.pointActive);
            R.nextActivityValidationMs = observation.nowMs + 1000;
        }
        activityObservation.pointValid = R.activityPointValid;
        // Also leave an ambient scenario that the wandering task chose itself
        // when its activity window changes. Its animation never proves a meal.
        activityObservation.forceLeave = R.activityPhase >= 0 &&
            R.activityPhase != static_cast<int>(activityObservation.phase) && activityObservation.anyScenario;
        R.activityPhase = static_cast<int>(activityObservation.phase);
    }
    const auto activityCommand = R.activity.Tick(activityObservation);
    if (!observation.blocked && ApplyRoutineActivity(ped, activityCommand, activityObservation)) return;
    observation.fallbackDestinationId = R.fallbackDestination;
    observation.destinationId = R.destination;
    observation.destinationAvailable = R.destinationValid && !R.ambientFallback;
    observation.distance = std::sqrt(DistSq(position, R.centre));
    // Hand control to native area wandering as soon as the target enters it.
    // There is no scripted requirement to visit the fixed centre first.
    config.arrivalDistance = R.wanderRadius;
    const Routine::Decision decision = R.controller.Tick(config, observation);
    if (decision.reevaluate) R.selectPending = true;
    if (R.controller.state == Routine::State::Waiting && R.fallbackDestination >= 0)
    {
        // This is an accepted spawn/arrived area, never the ped's moving position
        // or a rejected travel endpoint. Native navigation still avoids water.
        R.destination = R.fallbackDestination;
        R.centre = R.fallbackCentre;
        R.wanderRadius = RoutineData::kLocations[R.destination].wanderRadius;
        R.destinationValid = false; // A route check is pending; this is cached area wandering.
        R.ambientFallback = true;
    }
    if (!observation.blocked && decision.action == Routine::Action::Travel &&
        (activityObservation.anyScenario || activityObservation.exitingScenario || R.activity.point.id > 0))
    {
        ApplyRoutineActivity(ped, R.activity.Leave(activityObservation), activityObservation);
        return;
    }
    switch (decision.action)
    {
    case Routine::Action::Travel:
        TASK::SET_PED_PATH_PREFER_TO_AVOID_WATER(ped, true, R.wanderRadius);
        TASK::SET_PED_PATH_MAY_ENTER_WATER(ped, false);
        // act_hunting_2.c:10128 uses speed1, flags0, heading40000. The controller
        // supplies the finite deadline and bounded recovery for this longer town route.
        TASK::TASK_FOLLOW_NAV_MESH_TO_COORD(ped, R.centre, Routine::kWalkMetresPerSecond,
            static_cast<int>(R.controller.TripTimeoutMs()), 2.0f, 0, 40000.0f);
        PED::SET_PED_KEEP_TASK(ped, true);
        break;
    case Routine::Action::Wander:
        R.fallbackDestination = R.destination;
        R.fallbackCentre = R.centre;
        R.ambientFallback = false;
        activityObservation.destination = R.destination;
        activityObservation.eligible = RoutineData::SupportsPhase(RoutineData::kLocations[R.destination], activityObservation.phase);
        if (ApplyRoutineActivity(ped, R.activity.Tick(activityObservation), activityObservation)) break;
        [[fallthrough]];
    case Routine::Action::WanderFallback:
        TASK::SET_PED_PATH_PREFER_TO_AVOID_WATER(ped, true, R.wanderRadius);
        TASK::SET_PED_PATH_MAY_ENTER_WATER(ped, false);
        // Native area wandering decides its own pauses and ambient opportunities;
        // never force a smoking or drinking scenario at the arrival coordinate.
        // beat_public_hanging.c:16270 retains this 0/0/1 tail after navmesh travel.
        TASK::TASK_WANDER_IN_AREA(ped, R.centre, R.wanderRadius, 0.0f, 0.0f, 1);
        PED::SET_PED_KEEP_TASK(ped, true);
        break;
    case Routine::Action::None: break;
    }
}
