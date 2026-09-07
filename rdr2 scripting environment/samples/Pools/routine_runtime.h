#pragma once
#include "routine_plan.h"
#include "routine_spawn.h"

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
    float wanderRadius = 20.0f;
    Routine::Controller controller;
    bool selectPending = false;
    bool resumeRequested = false;
    ULONGLONG nextValidationMs = 0;
    ULONGLONG nextStreamRequestMs = 0, fallbackRecheckMs = 0;
    ULONGLONG pauseSnapshotMs = 0;
    ULONGLONG ambientClearanceUntilMs = 0;
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
    const unsigned seed = (static_cast<unsigned>(rand()) << 16) ^ static_cast<unsigned>(rand());
    const int town = static_cast<int>(seed % RoutineData::kTownCount);
    const unsigned occupation = RoutinePlan::GeneratedOccupation(town, seed);
    if (!RoutinePlan::Build(prepared.plan, town, occupation, seed)) return false;
    Routine::Candidate candidates[4];
    if (!RoutinePlan::Candidates(prepared.plan, candidates)) return false;
    // Only one preferred site and the explicit all-day fallback: no unlimited town rerolls.
    for (int attempt = 0; attempt < 2 && PlayerAvailable(); ++attempt)
    {
        const int minute = RoutineMinute();
        const int id = Routine::SelectDestination(candidates, 4,
            Routine::PhaseAt(minute, prepared.plan.offsetMinutes), occupation, minute, 15, seed);
        if (id < 0) break;
        const auto& location = RoutineData::kLocations[id];
        routineStartDiagnostic.location = location.id;
        routineStartDiagnostic.expected = location.anchor;
        StartupTrace::Record("candidate_prepare_begin", 0, 0, &location.anchor, -1, location.id);
        Vector3 point;
        if (RoutineSpawn::Prepare(location.anchor, location.candidateRadius, location.maxHeightDelta, seed, point) &&
            Routine::CanArriveAndStay({location.openMinute, location.closeMinute}, RoutineMinute(), 0, 15))
        {
            prepared.enabled = true;
            prepared.cardLines = RoutinePlan::CardLines(prepared.plan);
            prepared.destination = id;
            prepared.centre = point;
            routineStartDiagnostic.expected = point;
            prepared.wanderRadius = location.wanderRadius;
            const auto& area = RoutineData::kTowns[town];
            prepared.definition = { area.name, RoutinePlan::OccupationName(occupation), area.name,
                point, Tune::kReAggroSightDist, RoutineModels(town, occupation), &kHumanTarget, nullptr, ResetRoutine };
            StartupTrace::Record("candidate_prepared", 0, 0, &point, -1, location.id);
            return true;
        }
        StartupTrace::Record("candidate_rejected", 0, 0, &location.anchor, -1,
            std::strcmp(RoutineSpawn::diagnostic.check, "ok") == 0 ? "visiting_window_closed" : RoutineSpawn::diagnostic.check);
        for (auto& candidate : candidates) if (candidate.id == id) candidate.available = false;
    }
    return false;
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

static int RoutineTravelMinutes(const Vector3& from, const Vector3& to)
{
    // Read the actual game-clock rate (act_caunc_rustling.c:24960), never change it.
    const int rate = CLOCK::GET_MILLISECONDS_PER_GAME_MINUTE();
    if (rate <= 0) return Routine::kMinutesPerDay; // only all-day fallbacks remain eligible
    const double distance = std::sqrt(static_cast<double>(DistSq(from, to)));
    const double estimate = std::ceil(distance * 1500.0 / rate) + 5.0; // walking + detour allowance
    return static_cast<int>(estimate > Routine::kMinutesPerDay ? Routine::kMinutesPerDay : estimate);
}

static void SelectRoutineDestination(Ped ped, const Vector3& position, int minute, ULONGLONG now)
{
    Routine::Candidate candidates[4];
    RoutinePlan::Candidates(R.plan, candidates);
    for (auto& candidate : candidates)
    {
        if (candidate.id < 0) continue;
        const auto& location = RoutineData::kLocations[candidate.id];
        candidate.available = candidate.available && !R.controller.IsCoolingDown(candidate.id, now);
        candidate.travelMinutes = RoutineTravelMinutes(position, location.anchor);
    }
    R.destination = -1;
    R.destinationValid = false;
    R.ambientClearanceUntilMs = 0; // Prop grace belongs only to the previous validated stop.
    // At most the authored phase and its all-day fallback. No waits or scene takeover
    // in the per-frame bridge; an unloaded preferred area receives a collision request.
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        const int id = Routine::SelectDestination(candidates, 4, Routine::PhaseAt(minute, R.plan.offsetMinutes),
            R.plan.occupation, minute, 15, R.plan.seed);
        if (id < 0) break;
        const auto& location = RoutineData::kLocations[id];
        if (!RoutineSpawn::Loaded(location.anchor))
        {
            STREAMING::REQUEST_COLLISION_AT_COORD(location.anchor);
            PATH::ADD_NAVMESH_REQUIRED_REGION(location.anchor.x, location.anchor.y, 50.0f);
        }
        Vector3 point;
        if (RoutineSpawn::Find(location.anchor, location.candidateRadius, location.maxHeightDelta, R.plan.seed, point, ped))
        {
            R.destination = id;
            R.centre = point;
            R.wanderRadius = location.wanderRadius;
            R.destinationValid = true;
            break;
        }
        for (auto& candidate : candidates) if (candidate.id == id) candidate.available = false;
    }
    R.nextValidationMs = now + 1000;
    R.fallbackRecheckMs = now + 60000;
    R.selectPending = false;
}

static bool RoutineTaskActive(Ped ped)
{
    // Area wandering can use world scenarios. That pause is healthy only after
    // arrival; an unrelated scenario must not hide failed travel indefinitely.
    if (R.controller.state == Routine::State::Wandering && PED::IS_PED_USING_ANY_SCENARIO(ped)) return true;
    const Hash task = R.controller.state == Routine::State::Travelling
        ? joaat("SCRIPT_TASK_FOLLOW_NAV_MESH_TO_COORD") : joaat("SCRIPT_TASK_WANDER_IN_AREA");
    const int status = TASK::GET_SCRIPT_TASK_STATUS(ped, task, true);
    return status == 0 || status == 1;
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
    if (!loaded && observation.nowMs >= R.nextStreamRequestMs)
    {
        STREAMING::REQUEST_COLLISION_AT_COORD(position);
        PATH::ADD_NAVMESH_REQUIRED_REGION(position.x, position.y, 50.0f);
        R.nextStreamRequestMs = observation.nowMs + 1000;
    }
    if (!observation.blocked)
    {
        const bool ambientScenario = R.controller.state == Routine::State::Wandering && PED::IS_PED_USING_ANY_SCENARIO(ped);
        if (ambientScenario) R.ambientClearanceUntilMs = observation.nowMs + 5000;
        if (R.selectPending) SelectRoutineDestination(ped, position, observation.minute, observation.nowMs);
        observation.reevaluate = R.resumeRequested || R.pauseSnapshotMs != pausedDurationMs;
        R.pauseSnapshotMs = pausedDurationMs;
        R.resumeRequested = false;
        if (R.destination >= 0)
        {
            const auto& location = RoutineData::kLocations[R.destination];
            if (observation.nowMs >= R.nextValidationMs)
            {
                // Native ambient scenarios may hold bottles or other props. Preserve
                // the already-validated stop during that pause and its bounded exit
                // tail; opening hours and streaming still apply every update.
                R.destinationValid = location.enabled && (ambientScenario ||
                    observation.nowMs < R.ambientClearanceUntilMs
                    ? RoutineSpawn::Loaded(R.centre)
                    : RoutineSpawn::ValidatePoint(location.anchor, location.candidateRadius,
                        location.maxHeightDelta, R.centre, ped));
                R.nextValidationMs = observation.nowMs + 1000;
            }
            observation.destinationOpen = Routine::CanArriveAndStay({location.openMinute, location.closeMinute},
                observation.minute, R.controller.state == Routine::State::Travelling ? RoutineTravelMinutes(position, R.centre) : 0, 0);
            const int preferred = R.plan.route[static_cast<int>(Routine::PhaseAt(observation.minute, R.plan.offsetMinutes))];
            // Optional availability retries can wait for a native ambient pause to
            // end. Real schedule changes, closed hours and priority still win.
            if (R.destination != preferred && R.controller.state != Routine::State::Travelling && !ambientScenario &&
                observation.nowMs >= R.fallbackRecheckMs)
            {
                observation.reevaluate = true;
                R.fallbackRecheckMs = observation.nowMs + 60000;
            }
        }
        observation.taskActive = RoutineTaskActive(ped);
    }
    observation.destinationId = R.destination;
    observation.destinationAvailable = R.destinationValid;
    observation.distance = std::sqrt(DistSq(position, R.centre));
    const Routine::Decision decision = R.controller.Tick(config, observation);
    if (decision.reevaluate) R.selectPending = true;
    switch (decision.action)
    {
    case Routine::Action::Travel:
        TASK::SET_PED_PATH_PREFER_TO_AVOID_WATER(ped, true, R.wanderRadius);
        TASK::SET_PED_PATH_MAY_ENTER_WATER(ped, false);
        // act_hunting_2.c:10128 uses speed1, flags0, heading40000. The controller
        // supplies the finite deadline and bounded recovery for this longer town route.
        TASK::TASK_FOLLOW_NAV_MESH_TO_COORD(ped, R.centre, 1.0f, static_cast<int>(config.travelTimeoutMs), 2.0f, 0, 40000.0f);
        PED::SET_PED_KEEP_TASK(ped, true);
        break;
    case Routine::Action::Wander:
        TASK::SET_PED_PATH_PREFER_TO_AVOID_WATER(ped, true, R.wanderRadius);
        TASK::SET_PED_PATH_MAY_ENTER_WATER(ped, false);
        // Native area wandering decides its own pauses and ambient opportunities;
        // never force a smoking or drinking scenario at the arrival coordinate.
        // beat_public_hanging.c:16270 retains this 0/0/1 tail after navmesh travel.
        TASK::TASK_WANDER_IN_AREA(ped, R.centre, R.wanderRadius, 0.0f, 0.0f, 1);
        PED::SET_PED_KEEP_TASK(ped, true);
        break;
    case Routine::Action::Wait:
        TASK::TASK_STAND_STILL(ped, -1); // act_hunting_2.c:9931; replaced by the next valid routine task
        break;
    case Routine::Action::None: break;
    }
}
