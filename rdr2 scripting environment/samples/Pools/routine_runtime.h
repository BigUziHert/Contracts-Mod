#pragma once
#include "routine_plan.h"
#include "routine_spawn.h"

// Included after WaitUntil. The generated definition has stable storage; the cleanup
// hook resets only our routine data, never an ambient actor, game or vehicle.
struct RoutineRuntime
{
    bool enabled = false;
    RoutinePlan::Plan plan;
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
    bool destinationValid = true;
};
static RoutineRuntime R;
static void ResetRoutine() { R = RoutineRuntime{}; }
static int RoutineMinute() { return CLOCK::GET_CLOCK_HOURS() * 60 + CLOCK::GET_CLOCK_MINUTES(); }
static bool IsRoutine(const ContractDef& def) { return R.enabled && &def == &R.definition; }

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
    const bool stock = occupation == RoutineData::LivestockHand;
    switch (town)
    {
    case 0: return stock ? Models(rhdStock) : Models(rhdLabor);
    case 1: return stock ? Models(blwStock) : Models(blwLabor);
    case 2: return stock ? Models(valStock) : Models(valLabor);
    case 3: return stock ? Models(strStock) : Models(strLabor);
    default: return Models(SD_DOCK);
    }
}

static bool PrepareRoutineContract(RoutineRuntime& prepared)
{
    const unsigned seed = (static_cast<unsigned>(rand()) << 16) ^ static_cast<unsigned>(rand());
    const int town = static_cast<int>(seed % RoutineData::kTownCount);
    const unsigned occupation = town == 4 ? RoutineData::DockWorker :
        ((seed >> 8) & 1) ? RoutineData::Laborer : RoutineData::LivestockHand;
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
        Vector3 point;
        if (RoutineSpawn::Prepare(location.anchor, location.candidateRadius, location.maxHeightDelta, seed, point) &&
            Routine::CanArriveAndStay({location.openMinute, location.closeMinute}, RoutineMinute(), 0, 15))
        {
            prepared.enabled = true;
            prepared.destination = id;
            prepared.centre = point;
            prepared.wanderRadius = location.wanderRadius;
            const auto& area = RoutineData::kTowns[town];
            prepared.definition = { area.name, RoutinePlan::OccupationName(occupation), area.name,
                point, Tune::kReAggroSightDist, RoutineModels(town, occupation), &kHumanTarget, nullptr, ResetRoutine };
            return true;
        }
        for (auto& candidate : candidates) if (candidate.id == id) candidate.available = false;
    }
    return false;
}

static bool ValidateRoutineDeployment(Ped ped, const ContractDef& def)
{
    if (!IsRoutine(def)) return true;
    const auto& location = RoutineData::kLocations[R.destination];
    Vector3 point;
    // Capture can outlast distant streaming residency. Reload once, bounded, while
    // the photographed subject remains hidden; recheck both actors after any yield.
    if (!RoutineSpawn::Loaded(def.spawn) &&
        !RoutineSpawn::Prepare(location.anchor, location.candidateRadius, location.maxHeightDelta, R.plan.seed, point)) return false;
    return LivingPed(ped) && CanStartInteraction() && Routine::CanArriveAndStay({location.openMinute, location.closeMinute}, RoutineMinute(), 0, 15) &&
        RoutineSpawn::Validate(location.anchor, location.candidateRadius, location.maxHeightDelta, def.spawn, point, ped) &&
        Within(point, def.spawn, .5f);
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
    return Within(actual, def.spawn, .75f) && RoutineSpawn::Loaded(actual);
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
        if (R.selectPending) SelectRoutineDestination(ped, position, observation.minute, observation.nowMs);
        observation.reevaluate = R.resumeRequested || R.pauseSnapshotMs != pausedDurationMs;
        R.pauseSnapshotMs = pausedDurationMs;
        R.resumeRequested = false;
        if (R.destination >= 0)
        {
            const auto& location = RoutineData::kLocations[R.destination];
            if (observation.nowMs >= R.nextValidationMs)
            {
                Vector3 checked;
                R.destinationValid = location.enabled && RoutineSpawn::Validate(location.anchor, location.candidateRadius,
                    location.maxHeightDelta, R.centre, checked, ped) && Within(checked, R.centre, .5f);
                R.nextValidationMs = observation.nowMs + 1000;
            }
            observation.destinationOpen = Routine::CanArriveAndStay({location.openMinute, location.closeMinute},
                observation.minute, R.controller.state == Routine::State::Travelling ? RoutineTravelMinutes(position, R.centre) : 0, 0);
            const int preferred = R.plan.route[static_cast<int>(Routine::PhaseAt(observation.minute, R.plan.offsetMinutes))];
            if (R.destination != preferred && R.controller.state != Routine::State::Travelling &&
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
        TASK::TASK_WANDER_IN_AREA(ped, R.centre, R.wanderRadius, 0.0f, 0.0f, 1);
        PED::SET_PED_KEEP_TASK(ped, true);
        break;
    case Routine::Action::Wait:
        TASK::TASK_STAND_STILL(ped, -1); // act_hunting_2.c:9931; replaced by the next valid routine task
        break;
    case Routine::Action::Activity: // enabled only after the verified activity stage
    case Routine::Action::None: break;
    }
}
