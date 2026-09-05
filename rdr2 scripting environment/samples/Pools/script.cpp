/*
	Contracts mod — bounty-style contracts handed out by station clerks.
	Built on Alexander Blade's RDR2 ScriptHook SDK (http://dev-c.com).

	Flow:  NONE --[Get Contract]--> UNKNOWN --[spot / hurt / fight the target]--> FOUND
	       FOUND --[target dies, photograph the corpse]--> DEAD --[Collect Payment]--> NONE
	       UNKNOWN / FOUND --[End Contract at a giver]--> NONE

	Data (spawns, models, givers, tunables) lives in contract_data.h.
*/

#include "global.h"
#include "keyboard.h"
#include "contract_data.h"

// ===== [ DEBUG TOGGLES ] ===== (overridable from the build: set CL=/DCONTRACTS_DEBUG_HUD=1)
#ifndef CONTRACTS_DEBUG_KEYS
#define CONTRACTS_DEBUG_KEYS 1   // U: skip the clerk and roll a new contract
#endif
#ifndef CONTRACTS_DEBUG_HUD
#define CONTRACTS_DEBUG_HUD  0   // on-screen state / aggro / LOS readout
#endif

// ===== [ STATE ] =====
enum ContractState { CONTRACT_NONE, CONTRACT_UNKNOWN, CONTRACT_FOUND, CONTRACT_DEAD };
enum TargetTask    { TARGET_WANDER, TARGET_AGGRO };

struct ActiveContract
{
	const ContractDef* def = nullptr;
	Ped        target = 0;
	Vector3    targetPos;
	Blip       searchBlip = 0;          // radius blip while the target is unknown
	Blip       targetBlip = 0;          // entity blip once found, corpse blip once dead
	bool       corpseBlipPlaced = false;
	bool       trailsActive = false;
	TargetTask task = TARGET_WANDER;
	bool       remembersPlayer = false; // once aggroed, re-aggros on sight alone
	ULONGLONG  lastContactMs = 0;       // last time the target saw / was near the player
};

static ActiveContract C;
static ContractState  g_state = CONTRACT_NONE;

static Player  me = 0;
static Ped     pedMe = 0;
static Vector3 playerPos;

static Prompt giverPrompt = 0;
static Prompt camPrompt = 0;
static Hash   camGroup = 0;

// ===== [ SMALL HELPERS ] =====
static float DistSq(const Vector3& a, const Vector3& b)
{
	float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
	return dx * dx + dy * dy + dz * dz;
}
static bool Within(const Vector3& a, const Vector3& b, float dist) { return DistSq(a, b) <= dist * dist; }

static bool TargetExists() { return C.target && ENTITY::DOES_ENTITY_EXIST(C.target); }

static const char* Literal(const char* text) { return MISC::VAR_STRING(10, "LITERAL_STRING", text); }

static void DisplaySubtitle(const char* message) { DisplayObjective(message); }

static void RemoveBlip(Blip& blip)
{
	if (blip && MAP::DOES_BLIP_EXIST(blip)) MAP::REMOVE_BLIP(&blip);
	blip = 0;
}

static void ShowPrompt(Prompt prompt, bool show)
{
	HUD::_UI_PROMPT_SET_ENABLED(prompt, show);
	HUD::_UI_PROMPT_SET_VISIBLE(prompt, show);
}
// Hide and rewind a prompt but keep its handle valid.
static void ResetPrompt(Prompt prompt)
{
	if (!prompt || !HUD::_UI_PROMPT_IS_VALID(prompt)) return;
	ShowPrompt(prompt, false);
	HUD::_UI_PROMPT_RESTART_MODES(prompt);
}

// Waits for a streaming request, bounded so a bad name can't hang the script forever.
template<typename IsLoaded> static bool WaitLoaded(IsLoaded isLoaded)
{
	ULONGLONG deadline = GetTickCount64() + Tune::kStreamTimeoutMs;
	while (!isLoaded() && GetTickCount64() < deadline) WAIT(0);
	return isLoaded();
}

static void PlayAnimOnPed(Ped ped, const char* dict, const char* name, float blendIn, float blendOut, int duration, int flags)
{
	STREAMING::REQUEST_ANIM_DICT(dict);
	if (!WaitLoaded([&] { return STREAMING::HAS_ANIM_DICT_LOADED(dict) != 0; })) return;
	TASK::CLEAR_PED_SECONDARY_TASK(ped);
	TASK::TASK_PLAY_ANIM(ped, dict, name, blendIn, blendOut, duration, flags, 0.0f, 0,
		AIK_DISABLE_ARM_IK | AIK_DISABLE_TORSO_REACT_IK | AIK_DISABLE_TORSO_IK | AIK_DISABLE_HEAD_IK | AIK_DISABLE_LEG_IK, 0, 0, 0);
	STREAMING::REMOVE_ANIM_DICT(dict);
}

static Ped SpawnPed(Hash model, const Vector3& pos)
{
	STREAMING::REQUEST_MODEL(model, true);
	if (!WaitLoaded([&] { return STREAMING::HAS_MODEL_LOADED(model) != 0; }))
	{
		STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
		return 0;
	}
	Ped ped = PED::CREATE_PED(model, pos, 0.0f, 0, 1, 1, 1);
	PED::_SET_RANDOM_OUTFIT_VARIATION(ped, true);
	ENTITY::PLACE_ENTITY_ON_GROUND_PROPERLY(ped, 1);
	STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
	return ENTITY::DOES_ENTITY_EXIST(ped) ? ped : 0;
}

// ===== [ BLIPS ] =====
// Every contract blip shares the bounty sprite and name; only colour / scale / pulse differ.
static void StyleTargetBlip(Blip blip, const char* colorModifier, bool large, bool pulse)
{
	MAP::SET_BLIP_SPRITE(blip, joaat("blip_ambient_bounty_target"), true);
	if (large) MAP::BLIP_ADD_MODIFIER(blip, joaat("BLIP_MODIFIER_SCALE_2"));
	MAP::BLIP_ADD_MODIFIER(blip, joaat(colorModifier));
	if (pulse) MAP::BLIP_ADD_MODIFIER(blip, joaat("BLIP_MODIFIER_PULSE_FOREVER"));
	MAP::BLIP_ADD_MODIFIER(blip, joaat("BLIP_MODIFIER_VERYHIGH_CATEGORY"));
	MAP::_SET_BLIP_NAME(blip, Literal("Contract Target"));
}
static void AddSearchBlip()
{
	C.searchBlip = MAP::BLIP_ADD_FOR_RADIUS(BLIP_STYLE_MP_MISSION_GIVER, C.def->spawn, C.def->searchRadius);
	StyleTargetBlip(C.searchBlip, "BLIP_MODIFIER_MP_COLOR_32", true, false);
}
static void AddFoundBlip()
{
	C.targetBlip = MAP::BLIP_ADD_FOR_ENTITY(joaat("BLIP_STYLE_CREATOR_DEFAULT"), C.target);
	StyleTargetBlip(C.targetBlip, "BLIP_MODIFIER_MP_COLOR_32", false, false);
}
static void AddCorpseBlip()
{
	C.targetBlip = MAP::BLIP_ADD_FOR_COORDS(joaat("BLIP_STYLE_CREATOR_DEFAULT"), C.targetPos);
	StyleTargetBlip(C.targetBlip, "BLIP_MODIFIER_MP_COLOR_6", false, true);
}

// ===== [ HUMAN TARGET: SETUP ] =====
static void StartWander(Ped ped, const ContractDef& def)
{
	TASK::CLEAR_PED_TASKS(ped, true, true);
	TASK::SET_PED_PATH_PREFER_TO_AVOID_WATER(ped, true, def.searchRadius);
	TASK::SET_PED_PATH_MAY_ENTER_WATER(ped, false);
	TASK::TASK_WANDER_IN_AREA(ped, def.spawn, def.searchRadius, 0.0f, 0.0f, 1);
	C.task = TARGET_WANDER;
}

static void SetupHumanTarget(Ped ped, const ContractDef& def)
{
	PED::SET_PED_COMBAT_ABILITY(ped, CAL_PROFESSIONAL);
	PED::SET_PED_CAN_BE_INCAPACITATED(ped, false); // never surrender / give up — fights to the death

	// Ped config flags (Halen84 RDR3-Native-Flags-And-Enums)
	static const struct { int flag; bool on; } kConfig[] = {
		{ 130, false }, // DisableTalkTo
		{ 211, true  }, // GiveAmbientDefaultTaskIfMissionPed
		{ 43,  true  }, // DisableLadderClimbing
		{ 259, true  }, // CanAmbientHeadtrack
		{ 421, true  }, // AllowDoorBargingUnderCombat
		{ 435, true  }, // AlwaysRejectPlayerRobberyAttempt
		{ 467, true  }, // DisableHonorModifiers
		{ 0,   true  }, // AllowMedicsToAttend
		{ 289, true  }, // TreatDislikeAsHateWhenInCombat
		{ 225, false }, // ??
		{ 437, false }, // DisableWeatherConditionPerceptionChecks
		{ 442, true  },
		{ 233, true  }, // PedIsEnemyToPlayer — ON on purpose: off makes him a robbable civilian that cowers when
		                //   aimed at. De-aggro clears it; re-aggro / the combat bridge set it again.
		{ 351, true  }, // DisableIntimidationBackingAway
		{ 356, true  }, // BlockRobberyInteractionEscape
	};
	for (const auto& f : kConfig) PED::SET_PED_CONFIG_FLAG(ped, f.flag, f.on);

	PED::SET_PED_COMBAT_RANGE(ped, CR_VERY_FAR);
	PED::SET_PED_COMBAT_MOVEMENT(ped, 1);

	static const struct { int attr; bool on; } kCombat[] = {
		{ 114, true  }, // CA_CAN_EXECUTE_TARGET
		{ 41,  true  }, // CA_CAN_COMMANDEER_VEHICLES
		{ 125, true  }, // CA_QUIT_WHEN_TARGET_FLEES_INTERACTION_FIGHT
		{ 21,  false }, // CA_CAN_CHASE_TARGET_ON_FOOT
		{ 93,  false }, // CA_PREFER_MELEE
		{ 54,  true  }, // CA_ALWAYS_EQUIP_BEST_WEAPON
		{ 12,  true  }, // CA_BLIND_FIRE_IN_COVER
		{ 5,   true  }, // CA_ALWAYS_FIGHT
		{ 17,  false }, // CA_ALWAYS_FLEE
		{ 14,  false }, // CA_CAN_INVESTIGATE
		{ 58,  true  }, // CA_DISABLE_FLEE_FROM_COMBAT
		{ 46,  true  }, // CA_CAN_FIGHT_ARMED_PEDS_WHEN_NOT_ARMED
		{ 28,  true  }, // CA_CAN_USE_FRUSTRATED_ADVANCE — push in instead of giving up
		{ 50,  true  }, // CA_CAN_CHARGE — charge instead of fleeing
	};
	for (const auto& a : kCombat) PED::SET_PED_COMBAT_ATTRIBUTES(ped, a.attr, a.on);

	PED::SET_PED_FLEE_ATTRIBUTES(ped, 1024, true);
	PED::SET_PED_FLEE_ATTRIBUTES(ped, 512, true);
	PED::SET_PED_FLEE_ATTRIBUTES(ped, 16384, true);
	PED::SET_PED_FLEE_ATTRIBUTES(ped, 32768, false); // on = lets him cower / flee; off so he fights (rdr2mods forum)

	WEAPON::GIVE_WEAPON_TO_PED(ped, WEAPON_REVOLVER_CATTLEMAN, 60, false, true, 0, true, 0.0f, 0.0f, ADD_REASON_DEFAULT, false, 0.0f, false);
	WEAPON::GIVE_WEAPON_TO_PED(ped, WEAPON_MELEE_KNIFE, 0, false, true, 0, true, 0.0f, 0.0f, ADD_REASON_DEFAULT, false, 0.0f, false);

	StartWander(ped, def);
}

// ===== [ HUMAN TARGET: COMBAT ] =====
// Picks a weapon (or fists) per the tunables and commits the target to fighting the player.
static void EnterCombat(Ped ped)
{
	TASK::CLEAR_PED_SECONDARY_TASK(ped);
	PED::SET_PED_CONFIG_FLAG(ped, 233, true);     // PedIsEnemyToPlayer — so his own AI presses the attack
	PED::SET_PED_COMBAT_ATTRIBUTES(ped, 5, true); // CA_ALWAYS_FIGHT

	Hash weapon;
	bool melee;
	if ((rand() % 100) >= Tune::kArmedChancePct)    { weapon = joaat("WEAPON_UNARMED");   melee = true;  }
	else if ((rand() % 100) < Tune::kGunVsKnifePct) { weapon = WEAPON_REVOLVER_CATTLEMAN; melee = false; }
	else                                            { weapon = WEAPON_MELEE_KNIFE;        melee = true;  }
	PED::SET_PED_COMBAT_ATTRIBUTES(ped, 93, melee);  // CA_PREFER_MELEE: keep the knife / fists out
	PED::SET_PED_COMBAT_ATTRIBUTES(ped, 54, !melee); // CA_ALWAYS_EQUIP_BEST_WEAPON: don't auto-swap to the revolver
	WEAPON::SET_CURRENT_PED_WEAPON(ped, weapon, true, WEAPON_ATTACH_POINT_HAND_PRIMARY, false, false);

	PED::SET_COMBAT_FLOAT(ped, 20, 0.0f); // far
	PED::SET_COMBAT_FLOAT(ped, 21, 0.0f); // near
	TASK::TASK_COMBAT_PED(ped, pedMe, 0, 0);
	PED::SET_PED_KEEP_TASK(ped, true); // the combat task sticks so he can't drop into flee / cower

	C.task = TARGET_AGGRO;
	C.remembersPlayer = true;
	C.lastContactMs = GetTickCount64();
}

// True if the target has a clear line of sight to the player within his sight range.
// NOTE: CAN_PED_SEE_ENTITY reported "seen" with no line of sight, and HAS_ENTITY_CLEAR_LOS_TO_ENTITY
// reports clear at any distance (~190 m) — so: raw LOS trace, capped to sight range.
static bool TargetCanSeePlayer()
{
	return Within(playerPos, C.targetPos, Tune::kReAggroSightDist) &&
	       ENTITY::HAS_ENTITY_CLEAR_LOS_TO_ENTITY(C.target, pedMe, 17) != 0;
}

// First contact: he turns hostile when the player aims at or intimidates him while he is looking
// at the player, or hurts him.
static bool PlayerProvoked(Ped ped)
{
	bool looking = PED::IS_PED_HEADTRACKING_PED(ped, pedMe) != 0;
	return (looking && (PLAYER::IS_PLAYER_FREE_AIMING_AT_ENTITY(me, ped) || PED::_IS_PED_INTIMIDATED(ped)))
	    || ENTITY::HAS_ENTITY_BEEN_DAMAGED_BY_ENTITY(ped, pedMe, true, true);
}

// ===== [ HUMAN TARGET: PER-FRAME AI ] =====
// Ped tasks are issued on state transitions only — never re-issued per frame (that thrashes the ped AI).
static void UpdateHumanTarget(Ped ped, const ContractDef& def)
{
	switch (C.task)
	{
	case TARGET_WANDER:
		if (PED::IS_PED_IN_COMBAT(ped, pedMe))
		{
			// His own AI is already fighting the player (antagonised into it): commit him and hand control
			// to AGGRO so de-aggro governs it. No weapon re-roll — he's already armed and shooting.
			PED::SET_PED_CONFIG_FLAG(ped, 233, true);
			PED::SET_PED_COMBAT_ATTRIBUTES(ped, 5, true);
			PED::SET_PED_KEEP_TASK(ped, true);
			C.remembersPlayer = true;
			C.lastContactMs = GetTickCount64();
			C.task = TARGET_AGGRO;
		}
		else if (C.remembersPlayer ? TargetCanSeePlayer() : PlayerProvoked(ped))
		{
			EnterCombat(ped);
		}
		break;

	case TARGET_AGGRO:
		if (TargetCanSeePlayer() || Within(playerPos, C.targetPos, Tune::kDeAggroDist))
		{
			C.lastContactMs = GetTickCount64();
		}
		else if (GetTickCount64() - C.lastContactMs > Tune::kDeAggroGraceMs)
		{
			// Lost the player long enough: drop hostility so his combat AI stops hunting, holster and
			// wander. He still remembers the player, so coming back into sight re-triggers combat.
			PED::SET_PED_CONFIG_FLAG(ped, 233, false);
			PED::SET_PED_COMBAT_ATTRIBUTES(ped, 5, false);
			StartWander(ped, def);
		}
		break;
	}
}

const TargetBehavior kHumanTarget = { SetupHumanTarget, UpdateHumanTarget };

// ===== [ CONTRACT LIFECYCLE ] =====
static void ClearContract(bool deleteTarget)
{
	RemoveBlip(C.searchBlip);
	RemoveBlip(C.targetBlip);
	PLAYER::_CLEAR_PED_EAGLE_EYE_TRAILS_FOR_PLAYER(me);
	PLAYER::_UNREGISTER_EAGLE_EYE_FOR_ENTITY(me, C.target);
	if (C.def && C.def->onCleanup) C.def->onCleanup();
	if (deleteTarget && TargetExists()) PED::DELETE_PED(&C.target);
	ENTITY::SET_ENTITY_AS_NO_LONGER_NEEDED(&C.target);
	C = ActiveContract();
	ResetPrompt(giverPrompt);
	ResetPrompt(camPrompt);
	g_state = CONTRACT_NONE;
}

// Rolls a random contract and spawns its target. False if nothing could be spawned.
static bool StartContract()
{
	ClearContract(true);
	for (int attempt = 0; attempt < Tune::kSpawnAttempts; ++attempt)
	{
		const ContractDef& def = kContracts[rand() % kContractCount];
		Hash model = def.models.list[rand() % def.models.count];
		Ped ped = SpawnPed(model, def.spawn);
		if (!ped) continue;

		C.def = &def;
		C.target = ped;
		C.targetPos = def.spawn;
		ENTITY::SET_ENTITY_AS_MISSION_ENTITY(ped, true, true);
		def.behavior->setup(ped, def);
		if (def.onSpawned) def.onSpawned(def);
		AddSearchBlip();
		g_state = CONTRACT_UNKNOWN;
		return true;
	}
	return false;
}

static void UpdateTargetAI()
{
	if (PED::IS_PED_DEAD_OR_DYING(C.target, true)) return;
	C.def->behavior->update(C.target, *C.def);
}

// ===== [ EAGLE-EYE TRAIL ] =====
static void UpdateTrails()
{
	bool want = (g_state == CONTRACT_UNKNOWN || g_state == CONTRACT_FOUND) &&
	            Within(playerPos, C.targetPos, Tune::kTrailEnableDist);
	if (want == C.trailsActive) return;
	if (want)
	{
		PLAYER::_REGISTER_EAGLE_EYE_FOR_ENTITY(me, C.target, false);
		PLAYER::EAGLE_EYE_SET_CUSTOM_ENTITY_TINT(C.target, 255, 255, 0);
	}
	else
	{
		PLAYER::_CLEAR_PED_EAGLE_EYE_TRAILS_FOR_PLAYER(me);
		PLAYER::_UNREGISTER_EAGLE_EYE_FOR_ENTITY(me, C.target);
	}
	C.trailsActive = want;
}

// ===== [ FINDING THE TARGET ] =====
static void CheckTargetFound()
{
	if (ENTITY::IS_ENTITY_DEAD(C.target))
	{
		// Died before being spotted (long shot, another NPC, a train): straight to the corpse flow.
		RemoveBlip(C.searchBlip);
		RemoveBlip(C.targetBlip);
		g_state = CONTRACT_FOUND;
		return;
	}

	Entity interacted = 0;
	bool interacting = PLAYER::GET_PLAYER_INTERACTION_TARGET_ENTITY(me, &interacted, false, false) && interacted == (Entity)C.target;

	// Free-aim / lock-on register by camera direction at ANY distance, so only count them when the
	// player is inside the search area and actually has line of sight.
	bool nearWithLOS = Within(playerPos, C.targetPos, C.def->searchRadius) &&
	                   ENTITY::HAS_ENTITY_CLEAR_LOS_TO_ENTITY(pedMe, C.target, 17) != 0;
	bool aimed = nearWithLOS && (PLAYER::IS_PLAYER_FREE_AIMING_AT_ENTITY(me, C.target) || PLAYER::IS_PLAYER_TARGETTING_ENTITY(me, C.target, false));
	bool hurt  = ENTITY::HAS_ENTITY_BEEN_DAMAGED_BY_ENTITY(C.target, pedMe, true, true) != 0;
	bool fight = PED::IS_PED_IN_COMBAT(C.target, pedMe) != 0;

	if (interacting || aimed || hurt || fight)
	{
		DisplaySubtitle("TARGET FOUND");
		RemoveBlip(C.searchBlip);
		AddFoundBlip();
		g_state = CONTRACT_FOUND;
	}
}

// ===== [ CORPSE + PHOTO ] =====
static bool IsInHandheldCamera()
{
	return PAD::IS_CONTROL_ENABLED(0, INPUT_CAMERA_TAKE_PHOTO) ||
	       PAD::IS_CONTROL_ENABLED(0, INPUT_CAMERA_ADVANCED_TAKE_PHOTO);
}

static void CreateCameraPrompt()
{
	if (camPrompt) return;
	camGroup = joaat("CAMERA_ITEM_GROUP"); // the native camera group — required for the prompt to render inside the camera UI
	camPrompt = HUD::_UI_PROMPT_REGISTER_BEGIN();
	HUD::_UI_PROMPT_SET_CONTROL_ACTION(camPrompt, INPUT_CAMERA_TAKE_PHOTO);
	HUD::_UI_PROMPT_SET_TEXT(camPrompt, Literal("Take Photo"));
	HUD::_UI_PROMPT_SET_STANDARD_MODE(camPrompt, true);
	HUD::_UI_PROMPT_SET_PRIORITY(camPrompt, 3);
	HUD::_UI_PROMPT_SET_GROUP(camPrompt, camGroup, 0);
	ShowPrompt(camPrompt, false);
	HUD::_UI_PROMPT_REGISTER_END(camPrompt);
}

static void CheckTargetDeath()
{
	CreateCameraPrompt(); // lazily, on the first FOUND frame — as it always has been
	if (!PED::IS_PED_DEAD_OR_DYING(C.target, true)) return;

	if (!C.corpseBlipPlaced)
	{
		RemoveBlip(C.targetBlip);
		DisplaySubtitle("TAKE PICTURE OF CORPSE");
		AddCorpseBlip();
		C.corpseBlipPlaced = true;
	}
	MAP::SET_BLIP_COORDS(C.targetBlip, C.targetPos);

	// Making our prompt the ACTIVE prompt in the native CAMERA_ITEM_GROUP this frame renders it inside the
	// handheld camera AND intercepts the take-photo input: the corpse shot triggers our prompt instead of
	// the native capture, so the game never saves it. Once we reach CONTRACT_DEAD this stops running and
	// the native shutter works (and saves) normally again.
	if (!IsInHandheldCamera() || !ENTITY::IS_ENTITY_ON_SCREEN(C.target)) return;

	ShowPrompt(camPrompt, true);
	HUD::_UI_PROMPT_SET_ACTIVE_GROUP_THIS_FRAME(camGroup, "CAM_CONG_HC", 1, 0, 0, 0);
	if (HUD::_UI_PROMPT_IS_PRESSED(camPrompt))
	{
		DisplaySubtitle("RETURN TO CLERK");
		AUDIO::PLAY_SOUND_FRONTEND("take_photo", "Photo_Mode_Sounds", true, 0);
		ShowPrompt(camPrompt, false);
		RemoveBlip(C.targetBlip);
		g_state = CONTRACT_DEAD;
	}
}

// ===== [ GIVER (CLERK) PROMPT ] =====
static void CreateGiverPrompt()
{
	giverPrompt = HUD::_UI_PROMPT_REGISTER_BEGIN();
	HUD::_UI_PROMPT_SET_CONTROL_ACTION(giverPrompt, 0x620A6C5E);
	HUD::_UI_PROMPT_SET_STANDARDIZED_HOLD_MODE(giverPrompt, SHORT_TIMED_EVENT_MP);
	ShowPrompt(giverPrompt, false);
	HUD::_UI_PROMPT_REGISTER_END(giverPrompt);
}

// The giver spot the player is standing at while aiming at `ped`, if any: the spot for this ped's
// model nearest the player, with both the player and the ped close enough to it.
static const GiverSpot* FindGiverSpot(Ped ped)
{
	Hash model = ENTITY::GET_ENTITY_MODEL(ped);
	const GiverSpot* best = nullptr;
	float bestDistSq = 0.0f;
	for (const GiverSpot& spot : kGivers)
	{
		if (spot.model != model) continue;
		float d = DistSq(playerPos, spot.pos);
		if (!best || d < bestDistSq) { best = &spot; bestDistSq = d; }
	}
	if (!best || bestDistSq > Tune::kGiverPlayerDist * Tune::kGiverPlayerDist) return nullptr;
	if (!Within(ENTITY::GET_ENTITY_COORDS(ped, true, false), best->pos, Tune::kGiverSpotDist)) return nullptr;
	return best;
}

// Clerk handoff animations. The clip names read as if assigned to the wrong ped — they are NOT:
// this pairing is what produces the correct visuals in game. Do not swap them.
static void PlayGiverHandoff(Ped giver, bool payout)
{
	const int kGiverFlags  = AF_FORCE_START | AF_NOT_INTERRUPTABLE | AF_USE_KINEMATIC_PHYSICS | AF_USE_MOVER_EXTRACTION | AF_UPPERBODY | AF_SECONDARY;
	const int kPlayerFlags = AF_FORCE_START | AF_NOT_INTERRUPTABLE;
	if (!payout)
	{
		PlayAnimOnPed(giver, "script_rc@chrb@ig1_visit_clerk",     "arthur_gives_money_player", 8.0f, 1.0f, -1, kGiverFlags);
		PlayAnimOnPed(pedMe, "script_rc@chrb@ig_1_waitinginline",  "gives_money_trainworker",   1.0f, 1.0f, -1, kPlayerFlags);
	}
	else
	{
		PlayAnimOnPed(pedMe, "script_rc@chrb@ig1_visit_clerk",     "arthur_gives_money_player", 8.0f, 1.0f, -1, kPlayerFlags);
		PlayAnimOnPed(giver, "script_rc@chrb@ig_1_waitinginline",  "gives_money_trainworker",   1.0f, 1.0f, -1, kGiverFlags);
	}
}

static void UpdateGiverPrompt()
{
	Entity aimed = 0;
	Ped giver = 0;
	const GiverSpot* spot = nullptr;
	if (PLAYER::GET_PLAYER_TARGET_ENTITY(me, &aimed) && ENTITY::IS_ENTITY_A_PED(aimed))
	{
		giver = ENTITY::GET_PED_INDEX_FROM_ENTITY_INDEX(aimed);
		if (ENTITY::DOES_ENTITY_EXIST(giver)) spot = FindGiverSpot(giver);
	}
	if (!spot)
	{
		ShowPrompt(giverPrompt, false);
		return;
	}

	HUD::_UI_PROMPT_SET_GROUP(giverPrompt, HUD::_UI_PROMPT_GET_GROUP_ID_FOR_TARGET_ENTITY(giver), 0);
	const char* text = g_state == CONTRACT_NONE ? "Get Contract"
	                 : g_state == CONTRACT_DEAD ? "Collect Payment"
	                 :                            "End Contract";
	HUD::_UI_PROMPT_SET_TEXT(giverPrompt, Literal(text));
	ShowPrompt(giverPrompt, true);
	if (!HUD::_UI_PROMPT_HAS_HOLD_MODE_COMPLETED(giverPrompt)) return;

	switch (g_state)
	{
	case CONTRACT_NONE:
		PlayGiverHandoff(giver, false);
		DisplaySubtitle(StartContract() ? "FIND THE TARGET" : "NO CONTRACTS AVAILABLE");
		break;

	case CONTRACT_UNKNOWN:
	case CONTRACT_FOUND:
		DisplaySubtitle("CONTRACT ENDED");
		ClearContract(true);
		break;

	case CONTRACT_DEAD:
		DisplaySubtitle("REWARD RECEIVED");
		PlayGiverHandoff(giver, true);
		MONEY::_MONEY_INCREMENT_CASH_BALANCE(C.def ? C.def->reward : Tune::kDefaultReward, 0);
		ClearContract(false);
		break;
	}
}

// ===== [ DEBUG ] =====
static void DebugUpdate()
{
#if CONTRACTS_DEBUG_KEYS
	if (IsKeyJustUp(0x55)) // U: skip the clerk and roll a fresh contract
		DisplaySubtitle(StartContract() ? "FIND THE TARGET" : "NO CONTRACTS AVAILABLE");
#endif
#if CONTRACTS_DEBUG_HUD
	// state 0=NONE 1=UNKNOWN 2=FOUND 3=DEAD   task 0=WANDER 1=AGGRO
	bool  have   = TargetExists();
	int   losRaw = have ? (ENTITY::HAS_ENTITY_CLEAR_LOS_TO_ENTITY(C.target, pedMe, 17) ? 1 : 0) : -1;
	int   sees   = have ? (TargetCanSeePlayer() ? 1 : 0) : -1;
	float dist   = have ? sqrtf(DistSq(playerPos, C.targetPos)) : -1.0f;
	char dbg[180];
	sprintf_s(dbg, "state=%d task=%d remember=%d losRaw=%d sees=%d dist=%.1f",
		(int)g_state, (int)C.task, C.remembersPlayer ? 1 : 0, losRaw, sees, dist);
	DrawTextToScreen(dbg, 0.05f, 0.08f, 0.4f, 255, 255, 0, 255);
#endif
}

// ===== [ MAIN LOOP ] =====
static void UpdatePlayer()
{
	me = PLAYER::PLAYER_ID();
	pedMe = PLAYER::PLAYER_PED_ID();
	playerPos = ENTITY::GET_ENTITY_COORDS(pedMe, true, false);
	if (TargetExists()) C.targetPos = ENTITY::GET_ENTITY_COORDS(C.target, true, false);
}

void ScriptMain()
{
	srand((unsigned)GetTickCount64());
	CreateGiverPrompt();
	while (true)
	{
		UpdatePlayer();
		DebugUpdate();

		// A contract whose target vanished (deleted by another script, fell out of the world) can never
		// be completed — end it instead of leaving stale blips on the map.
		if ((g_state == CONTRACT_UNKNOWN || g_state == CONTRACT_FOUND) && !TargetExists())
		{
			DisplaySubtitle("TARGET LOST");
			ClearContract(false);
		}

		switch (g_state)
		{
		case CONTRACT_NONE:
			UpdateGiverPrompt();
			break;
		case CONTRACT_UNKNOWN:
			UpdateGiverPrompt();
			UpdateTrails();
			UpdateTargetAI();
			CheckTargetFound();
			break;
		case CONTRACT_FOUND:
			UpdateGiverPrompt();
			UpdateTrails();
			UpdateTargetAI();
			CheckTargetDeath();
			break;
		case CONTRACT_DEAD:
			UpdateTrails();
			UpdateGiverPrompt();
			break;
		}
		WAIT(0);
	}
}
