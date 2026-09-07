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
    ULONGLONG nextValidationMs = 0;
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
    // Portrait capture yields long enough for distant collision or availability to change.
    // Keep the subject hidden unless the exact prepared point still passes validation.
    return CanStartInteraction() && Routine::CanArriveAndStay({location.openMinute, location.closeMinute}, RoutineMinute(), 0, 15) &&
        RoutineSpawn::Validate(location.anchor, location.candidateRadius, location.maxHeightDelta, def.spawn, point, ped) &&
        Within(point, def.spawn, .5f);
}

static bool StartRoutineWander(Ped ped, const ContractDef& def)
{
    if (!IsRoutine(def)) return false;
    TASK::SET_PED_PATH_PREFER_TO_AVOID_WATER(ped, true, R.wanderRadius);
    TASK::SET_PED_PATH_MAY_ENTER_WATER(ped, false);
    TASK::TASK_WANDER_IN_AREA(ped, R.centre, R.wanderRadius, 0.0f, 0.0f, 1);
    PED::SET_PED_KEEP_TASK(ped, true);
    return true;
}

static bool ValidateRoutinePlacement(Ped ped, const ContractDef& def)
{
    if (!IsRoutine(def)) return true;
    const Vector3 actual = ENTITY::GET_ENTITY_COORDS(ped, true, false);
    return Within(actual, def.spawn, .75f) && RoutineSpawn::Loaded(actual);
}
