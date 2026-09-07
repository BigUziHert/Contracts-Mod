#pragma once
#include "routine_debug_blips.h"
#include "routine_debug_view.h"

// Testing display only. Observe the existing controller; never assign tasks, select
// destinations, request streaming, or modify discovery to obtain debug information.
static bool routineDebugEnabled = true;
static ULONGLONG routineDebugNextSampleMs = 0;
static RoutineDebugView::Lines routineDebugLines{};

static const char* ObserveRoutineDebugActivity(RoutineDebugView::Snapshot& snapshot)
{
    const Ped ped = C.target;
    if (PED::IS_PED_HOGTIED(ped)) return "Hogtied";
    if (PED::IS_PED_BEING_HOGTIED(ped)) return "Being hogtied";
    if (PED::IS_PED_LASSOED(ped)) return "Lassoed";
    if (PED::IS_PED_RAGDOLL(ped)) return "Ragdoll";
    if (TASK::IS_PED_GETTING_UP(ped)) return "Getting up";
    if (C.ai.state == TargetAI::State::Engaged) return "Fighting";
    if (C.ai.state == TargetAI::State::Search) return "Searching for player";
    if (C.ai.pendingEngagement) return "Preparing to fight";
    const int combat = TASK::GET_SCRIPT_TASK_STATUS(ped, joaat("SCRIPT_TASK_COMBAT"), true);
    if (combat == 0 || combat == 1) return "Fighting";
    // Mirror the AI bridge: the player's stale native combat flag alone does not
    // undo a completed search or prove that the target is currently fighting.
    if (PED::IS_PED_IN_COMBAT(ped, 0) && !PED::IS_PED_IN_COMBAT(ped, pedMe)) return "Fighting another actor";
    if (PED::IS_PED_IN_ANY_VEHICLE(ped, false)) return "In a vehicle";
    if (!snapshot.loaded) return "Paused: area not loaded";
    if (R.controller.state == Routine::State::Suspended) return "Routine paused";
    if (R.selectPending || R.resumeRequested) return "Choosing destination";
    if (PED::IS_PED_USING_ANY_SCENARIO(ped))
    {
        snapshot.taskActive = RoutineTaskActive(ped);
        if (PED::IS_PED_USING_SCENARIO_HASH(ped, Joaat("WORLD_HUMAN_SMOKE"))) return "Smoking (ambient)";
        if (PED::IS_PED_USING_SCENARIO_HASH(ped, Joaat("WORLD_HUMAN_DRINKING"))) return "Drinking (ambient)";
        return "Ambient scenario";
    }
    if (R.controller.state == Routine::State::Waiting) return "Waiting for a usable destination";
    snapshot.taskActive = RoutineTaskActive(ped);
    if (R.controller.state == Routine::State::Travelling)
        return snapshot.taskActive ? "Walking to destination" : "Travel task pending / recovery";
    return snapshot.taskActive ? "Wandering near destination" : "Wander task pending / recovery";
}

static RoutineDebugView::Snapshot ObserveRoutineDebug()
{
    RoutineDebugView::Snapshot snapshot;
    snapshot.minute = RoutineMinute();
    snapshot.active = ContractActive();
    if (!snapshot.active) return snapshot;
    const bool routine = C.def && IsRoutine(*C.def) && RoutinePlan::Valid(R.plan);
    if (routine)
    {
        snapshot.town = RoutineData::kTowns[R.plan.townIndex].name;
        snapshot.occupation = RoutinePlan::OccupationName(R.plan.occupation);
    }
    snapshot.targetDead = g_state == CONTRACT_DEAD;
    snapshot.targetExists = TargetExists();
    if (!snapshot.targetExists) return snapshot;
    const Vector3 target = ENTITY::GET_ENTITY_COORDS(C.target, true, false);
    const Vector3 player = ENTITY::GET_ENTITY_COORDS(pedMe, true, false);
    snapshot.playerDistance = std::sqrt(DistSq(player, target));
    snapshot.x = target.x; snapshot.y = target.y; snapshot.z = target.z;
    snapshot.targetDead = snapshot.targetDead || PED::IS_PED_DEAD_OR_DYING(C.target, true) != 0;
    if (snapshot.targetDead) return snapshot;
    if (!routine) { snapshot.doing = "No town routine"; return snapshot; }
    snapshot.loaded = RoutineSpawn::Loaded(target);
    snapshot.inside = INTERIOR::GET_INTERIOR_FROM_COLLISION(target) != 0 || !INTERIOR::IS_COLLISION_MARKED_OUTSIDE(target);
    snapshot.wanderRadius = R.wanderRadius;
    const int phase = static_cast<int>(Routine::PhaseAt(snapshot.minute, R.plan.offsetMinutes));
    const int nextPhase = (phase + 1) % 4;
    const int nextBoundary[] = {840, 1080, 1440, 360};
    snapshot.nextMinute = Routine::NormalizeMinute(nextBoundary[phase] + R.plan.offsetMinutes);
    snapshot.nextDestination = RoutineData::kLocations[R.plan.route[nextPhase]].name;
    snapshot.hasDestination = R.destination >= 0 && R.destination < RoutineData::kLocationCount;
    if (snapshot.hasDestination)
    {
        const auto& location = RoutineData::kLocations[R.destination];
        snapshot.destination = location.name;
        snapshot.destinationDistance = std::sqrt(DistSq(target, R.centre));
        snapshot.destinationValid = R.destinationValid;
        snapshot.destinationOpen = Routine::IsOpen({location.openMinute, location.closeMinute}, snapshot.minute);
        snapshot.fallback = location.kind == RoutineData::PlaceKind::Rest && phase != static_cast<int>(Routine::Phase::Rest);
    }
    snapshot.doing = ObserveRoutineDebugActivity(snapshot);
    return snapshot;
}

static void ToggleRoutineDebug()
{
    routineDebugEnabled = !routineDebugEnabled;
    routineDebugNextSampleMs = 0;
    if (!routineDebugEnabled) RoutineDebugBlips::Clear();
}

static void UpdateRoutineDebug()
{
    if (!routineDebugEnabled) return;
    const bool routine = ContractActive() && C.def && IsRoutine(*C.def);
    const Vector3* spawn = routine ? &C.def->spawn : nullptr;
    const Vector3* destination = routine && R.destination >= 0 && R.destination < RoutineData::kLocationCount && R.destinationValid
        ? &R.centre : nullptr;
    RoutineDebugBlips::Update(true, spawn, destination);
    const ULONGLONG now = GetTickCount64();
    if (now >= routineDebugNextSampleMs)
    {
        routineDebugNextSampleMs = now + 250;
        routineDebugLines = RoutineDebugView::Format(ObserveRoutineDebug());
    }
    // The existing UIDEBUG helper draws screen text independently of the card's
    // selected render target. Do not draw a GRAPHICS rectangle into that target.
    for (std::size_t index = 0; index < routineDebugLines.size(); ++index)
    {
        const auto& line = routineDebugLines[index];
        if (line.empty()) continue;
        const float y = .075f + static_cast<float>(index) * .024f;
        DrawTextToScreen(line.c_str(), .0258f, y + .001f, .29f, 0, 0, 0, 245);
        DrawTextToScreen(line.c_str(), .025f, y, .29f, 255, index == 0 ? 105 : 255, index == 0 ? 105 : 255, 255);
    }
}
