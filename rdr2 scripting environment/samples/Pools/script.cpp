/*
	THIS FILE IS A PART OF RDR 2 SCRIPT HOOK SDK
				http://dev-c.com
			(C) Alexander Blade 2019
*/

#include "global.h"
#include "keyboard.h"

// ===== [ FORWARD DECLARATIONS ] =====
static void ResetContract();
static void createClerkPrompt();
static void CreateCameraPrompt();
static void UpdateClerkPrompt();
static void PlayAnimOnPed(Ped, const char*, const char*, float, float, int, int);
static Ped spawnPed(Hash, float, float, float);
static void pickContract();
static void startPedCombat(Ped);
static void EnterCombat(Ped);
static bool TargetCanSeePlayer();
static float TargetPlayerDistSq();
static void StartPedWander(Ped);
static void AIHandler();
static void unknownBlip();
static void targetFindCheck();
static bool IsInHandheldCamera();
static void targetDeathCheck();

// ===== [ PROMPT AND HASH ] =====
static Ped clerkPed = 0;
static Prompt clerkPrompt = 0;
static Prompt camPrompt = 0;
static Hash camGroup = 0;

// ===== [ PLAYER & CONTRACT ] =====
static Blip searchBlip = 0;
static Blip bountyBlip = 0;
static bool blipAdd = false;
static bool trailsActive = false;
static float searchRadius = 0.0f;

static bool RetrySpawn = false;

// PLAYER
static Ped pedMe;
static Player me;
static Vector3 playerPos = { 0.0f, 0.0f, 0.0f };
// CONTRACT
static Ped contractTarget = 0;
static Vector3 targetPos = { 0.0f, 0.0f, 0.0f };

static bool targetInTask = false;
bool  gAmbientPedActive = false;

float targetSpawnX = 0.0f;
float targetSpawnY = 0.0f;
float targetSpawnZ = 0.0f;

// ===== [ CONTRACT STATES ] =====
enum ContractState {
	CONTRACT_NONE,
	CONTRACT_UNKNOWN,
	CONTRACT_FOUND,
	CONTRACT_DEAD
};
static ContractState g_state = CONTRACT_NONE;

// ===== [ CONTRACT TASKS ] =====
enum ContractTasks {
	CONTRACT_WANDER,
	CONTRACT_AGGRO,
};
static ContractTasks g_task = CONTRACT_WANDER;

// ===== [ AGGRO / PERCEPTION TUNABLES ] =====
// Of every aggro, how often the target draws a weapon vs. throwing fists.
static const int   kArmedChancePct   = 90;     // 90% armed, 10% fists
static const int   kGunVsKnifePct    = 75;     // of the armed rolls: 75% gun, 25% knife
// How far he can still "see"/notice the player (also his perception range + re-aggro range).
// Must be LARGER than kDeAggroDist for line of sight to matter while chasing.
static const float kReAggroSightDist = 45.0f;
// Within this distance he stays engaged no matter what (no LOS needed); beyond it, staying
// engaged requires line of sight, up to kReAggroSightDist.
static const float kDeAggroDist      = 20.0f;
// ...continuously for this long (ms), so a brief LOS break doesn't reset him.
static const DWORD kDeAggroGraceMs   = 8000;

static bool  targetRemembersPlayer = false; // once aggroed, re-aggros on sight alone
static ULONGLONG lastContactMs     = 0;     // last time target saw / was near the player

// ===== [ STRUCT CONTRACT SPAWNS + MODELS ] =====
struct SpawnDef
{
	//Spawn Location
	float x; //Spawn X
	float y; //Spawn Y
	float z; //Spawn Z

	//Model Information
	Hash* models; //Target Model
	int modelCount;

	//Search Radius
	float searchRadius;
};

// ===== [ Saint Denis Models ] =====
Hash SDDOCKMODELS[] = {
	joaat("a_m_m_sddockworkers_02"),
	joaat("a_m_m_nbxdockworkers_01"),
};
Hash SDRICHBARMODELS[] = {
	joaat("a_m_m_nbxupperclass_01"),
	joaat("a_m_m_middlesdtownfolk_01"),
};
Hash SDPOORBARMODELS[] = {
	joaat("a_m_m_middlesdtownfolk_01"),
};

// ===== [ Black Water Models ] =====
Hash BLWBARMODELS[] = {
	joaat("A_M_M_BLWTOWNFOLK_01"),
	joaat("A_M_M_BLWLABORER_02"),
	joaat("a_m_m_cardgameplayers_01"),
	joaat("a_m_o_blwupperclass_01"),
	joaat("a_m_m_blwupperclass_01"),
	joaat("s_m_m_blwcowpoke_01"),
	joaat("a_m_m_blwforeman_01"),
	joaat("a_m_m_blwlaborer_01"),
};
Hash BLWDOCKMODELSRIGHT[] = {
	joaat("A_M_M_BLWLABORER_02"),
	joaat("a_m_m_blwforeman_01"),
	joaat("s_m_m_blwcowpoke_01"),
};
Hash BLWDOCKMODELSLEFT[] = {
	joaat("A_M_M_BLWLABORER_02"),
	joaat("a_m_m_blwlaborer_01"),
	joaat("a_m_m_blwforeman_01"),
	joaat("s_m_m_blwcowpoke_01"),
	joaat("a_m_o_blwupperclass_01"),
	joaat("a_m_m_blwupperclass_01"),
	joaat("A_M_M_BLWTOWNFOLK_01"),
};
Hash BLWCONSTRUCTIONMODELS[] = {
	joaat("a_m_m_blwlaborer_01"),
	joaat("a_m_m_blwforeman_01"),
	joaat("s_m_m_blwcowpoke_01"),
};

// ===== [ Strawberry Models ] =====
Hash STRWBRYTOWN[] = {
	joaat("a_m_m_strlaborer_01"),
	joaat("s_m_m_liveryworker_01"),
	joaat("a_m_m_strtownfolk_01"),
	joaat("s_m_m_strcowpoke_01"),
	joaat("s_m_m_valcowpoke_01"),
	joaat("g_m_m_unicriminals_02"),
	joaat("a_m_m_strfancytourist_01"),
	joaat("s_m_m_ambientlawrural_01"),
};

// ===== [ Valentine Models ] =====
Hash VALRICHBARMODELS[] = {
	joaat("a_m_m_vallaborer_01"),
	joaat("a_m_m_valtownfolk_02"),
	joaat("a_m_m_valfarmer_01"),
	joaat("s_m_m_valcowpoke_01"),
	joaat("a_m_m_valtownfolk_01"),
	joaat("s_m_m_asbcowpoke_01"),
	joaat("re_townrobbery_males_01"),
	joaat("a_m_m_cardgameplayers_01"),
};
Hash VALPOORBARMODELS[] = {
	joaat("a_m_m_vallaborer_01"),
	joaat("a_m_m_valtownfolk_02"),
	joaat("a_m_m_valfarmer_01"),
	joaat("s_m_m_valcowpoke_01"),
	joaat("a_m_m_valtownfolk_01"),
	joaat("s_m_m_asbcowpoke_01"),
	joaat("re_townrobbery_males_01"),
};
Hash VALAUCTIONBARMODELS[] = {
	joaat("a_m_m_vallaborer_01"),
	joaat("a_m_m_valtownfolk_02"),
	joaat("a_m_m_valfarmer_01"),
	joaat("s_m_m_valcowpoke_01"),
	joaat("a_m_m_valtownfolk_01"),
	joaat("s_m_m_asbcowpoke_01"),
};

// ===== [ Rhodes Models ] =====
Hash RHODESBARMODELS[] = {
	joaat("a_m_m_rhdtownfolk_02"), //0x6833EBEE
	joaat("a_m_m_rhdtownfolk_01_laborer"), //0x009F6A48
	joaat("a_m_m_rhdforeman_01"), //0x94EAA58F
	joaat("a_m_m_rhdupperclass_01"), //0xAFCAF759
	joaat("g_m_y_uniexconfeds_01"), //0xDA82E29B
	joaat("a_m_m_cardgameplayers_01"), //0xC7458219
	joaat("s_m_m_rhdcowpoke_01"), //0xEDF61C81
	joaat("g_m_m_unigrays_01"), //0x15EB41F6
};
Hash RHODESSHOPSMODELS[] = {
	joaat("a_m_m_rhdtownfolk_02"), //0x6833EBEE
	joaat("a_m_m_rhdtownfolk_01_laborer"), //0x009F6A48
	joaat("a_m_m_rhdforeman_01"), //0x94EAA58F
	joaat("a_m_m_rhdupperclass_01"), //0xAFCAF759
	joaat("g_m_y_uniexconfeds_01"), //0xDA82E29B
	joaat("a_m_m_cardgameplayers_01"), //0xC7458219
	joaat("s_m_m_rhdcowpoke_01"), //0xEDF61C81
	joaat("g_m_m_unigrays_01"), //0x15EB41F6
};
Hash RHODESMILLMODELS[] = {
	joaat("a_m_m_rhdtownfolk_02"), //0x6833EBEE
	joaat("a_m_m_rhdtownfolk_01_laborer"), //0x009F6A48
	joaat("a_m_m_rhdforeman_01"), //0x94EAA58F
	joaat("a_m_m_rhdupperclass_01"), //0xAFCAF759
	joaat("g_m_y_uniexconfeds_01"), //0xDA82E29B
	joaat("a_m_m_cardgameplayers_01"), //0xC7458219
	joaat("s_m_m_rhdcowpoke_01"), //0xEDF61C81
	joaat("g_m_m_unigrays_01"), //0x15EB41F6
};
Hash RHODESFENCEMODELS[] = {
	joaat("a_m_m_rhdtownfolk_02"), //0x6833EBEE
	joaat("a_m_m_rhdtownfolk_01_laborer"), //0x009F6A48
	joaat("a_m_m_rhdforeman_01"), //0x94EAA58F
	joaat("a_m_m_rhdupperclass_01"), //0xAFCAF759
	joaat("g_m_y_uniexconfeds_01"), //0xDA82E29B
	joaat("a_m_m_cardgameplayers_01"), //0xC7458219
	joaat("s_m_m_rhdcowpoke_01"), //0xEDF61C81
	joaat("g_m_m_unigrays_01"), //0x15EB41F6
};

SpawnDef g_ContractSpawns[] = {
	// Rhodes Bar
	{ 1370.0f, -1354.0f, 78.0f, RHODESBARMODELS,(int)(sizeof(RHODESBARMODELS) / sizeof(RHODESBARMODELS[0])), 65.0f },

	// Rhodes Shops
	{ 1309.5f, -1292.5f, 76.0f, RHODESSHOPSMODELS,(int)(sizeof(RHODESSHOPSMODELS) / sizeof(RHODESSHOPSMODELS[0])), 50.0f },

	// Rhodes Mill
	{ 1388.5f, -1313.0f, 77.5f, RHODESMILLMODELS,(int)(sizeof(RHODESMILLMODELS) / sizeof(RHODESMILLMODELS[0])), 60.0f },

	// Rhodes Trapper
	{ 1341.0f, -1148.5f, 82.5f, RHODESFENCEMODELS,(int)(sizeof(RHODESFENCEMODELS) / sizeof(RHODESFENCEMODELS[0])), 50.0f },

	// BLW Bar
	{ -814.0f, -1318.5f, 44.0f, BLWBARMODELS,(int)(sizeof(BLWBARMODELS) / sizeof(BLWBARMODELS[0])), 45.0f },

	// BLW Harbor Right
	{ -730.0f, -1275.0f, 44.0f, BLWDOCKMODELSRIGHT,(int)(sizeof(BLWDOCKMODELSRIGHT) / sizeof(BLWDOCKMODELSRIGHT[0])), 60.0f },

	// BLW Harbor Left
	{ -732.0f, -1243.0f, 45.0f, BLWDOCKMODELSLEFT,(int)(sizeof(BLWDOCKMODELSLEFT) / sizeof(BLWDOCKMODELSLEFT[0])), 60.0f },

	// BLW Construction
	{ -809.0f, -1220.0f, 43.5f, BLWCONSTRUCTIONMODELS,(int)(sizeof(BLWCONSTRUCTIONMODELS) / sizeof(BLWCONSTRUCTIONMODELS[0])), 55.0f },

	// VAL Rich Bar
	{ -309.0f, 808.5f, 119.0f, VALRICHBARMODELS,(int)(sizeof(VALRICHBARMODELS) / sizeof(VALRICHBARMODELS[0])), 40.0f },

	// VAL Poor Bar
	{ -243.0f, 769.0f, 118.0f, VALPOORBARMODELS,(int)(sizeof(VALPOORBARMODELS) / sizeof(VALPOORBARMODELS[0])), 50.0f },

	// VAL Auction Yard
	{ -236.0f, 652.0f, 113.0f, VALAUCTIONBARMODELS,(int)(sizeof(VALAUCTIONBARMODELS) / sizeof(VALAUCTIONBARMODELS[0])), 80.0f },

	// STRWBRRY Town Bottom Left
	{ -1828.0f, -413.5f, 161.0f, STRWBRYTOWN,(int)(sizeof(STRWBRYTOWN) / sizeof(STRWBRYTOWN[0])), 55.0f },

	// STRWBRRY Town Bottom Right
	{ -1789.0f, -427.0f, 115.5f, STRWBRYTOWN,(int)(sizeof(STRWBRYTOWN) / sizeof(STRWBRYTOWN[0])), 55.0f },

	// STRWBRRY Town Top Left
	{ -1795.0f, -364.0f, 162.0f, STRWBRYTOWN,(int)(sizeof(STRWBRYTOWN) / sizeof(STRWBRYTOWN[0])), 55.0f },

	// STRWBRRY Town Top Right
	{ -1775.0f, -387.5f, 157.0f, STRWBRYTOWN,(int)(sizeof(STRWBRYTOWN) / sizeof(STRWBRYTOWN[0])), 55.0f },

	// Saint Denis Dock 1
	{ 2778.5f, -1462.5f, 45.5f, SDDOCKMODELS,(int)(sizeof(SDDOCKMODELS) / sizeof(SDDOCKMODELS[0])), 35.0f },

	// Saint Denis Dock 2
	{ 2787.5f, -1439.5f, 45.5f, SDDOCKMODELS,(int)(sizeof(SDDOCKMODELS) / sizeof(SDDOCKMODELS[0])), 35.0f },

	// Saint Denis Cool Bar
	{ 2615.5f, -1212.5f, 53.5f, SDRICHBARMODELS,(int)(sizeof(SDRICHBARMODELS) / sizeof(SDRICHBARMODELS[0])), 35.0f }

};
const int g_ContractSpawnCount = sizeof(g_ContractSpawns) / sizeof(g_ContractSpawns[0]);

// ===== [ STRUCT CLERK SPOTS + MODEL ] =====
struct ClerkSpot
{
	Vector3    pos;
	Hash       model;
	const char* modelName;
};
static ClerkSpot gClerkSpots[] =
{
	// Saint Denis
	{ { 2748.79f, -1398.22f, 46.18f }, 0xE9694F3F, "s_m_m_trainstationworker_01"}, // outside
	{ { 2747.82f, -1396.43f, 46.18f }, 0xE9694F3F, "s_m_m_trainstationworker_01"}, // inside

	// Rhodes
	{ { 1230.25f, -1298.60f, 76.90f }, 0x3F0EC349, "u_m_m_rhdtrainstationworker_01"}, // right
	{ { 1226.71f, -1295.08f, 76.91f }, 0x3F0EC349, "u_m_m_rhdtrainstationworker_01"}, // left

	// Blackwater
	{ { -875.03f, -1327.09f, 43.97f },  0xE9694F3F, "s_m_m_trainstationworker_01"}, // inside
	{ { -875.02f, -1326.71f, 43.97f },  0xE9694F3F, "s_m_m_trainstationworker_01"}, // outside

	// Strawberry
	{ { -1763.84f, -385.08f, 157.73f }, 0x1173F849, "u_m_m_strfreightstationowner_01"},

	// Valentine
	{ { -175.06f, 631.76f, 114.09f },  0xE9694F3F, "s_m_m_trainstationworker_01"}, // left
	{ { -178.00f, 628.11f, 114.09f },  0xE9694F3F, "s_m_m_trainstationworker_01"}, // right

	// Riggs
	{ { -1094.0f, -577.5f, 82.5f },  0xE9694F3F, "s_m_m_trainstationworker_01"}, // Right
	{ { -1093.0f, -575.5f, 82.5f },  0xE9694F3F, "s_m_m_trainstationworker_01"}, // Left

	// Emerald
	{ { 1523.5f, 442.5f, 90.5f },  0xE9694F3F, "s_m_m_trainstationworker_01"}, // Open
	{ { 1522.0f, 441.0f, 90.5f },  0xE9694F3F, "s_m_m_trainstationworker_01"}, // Shelter

	// Van Horn
	{ { 2986.0f, 570.0f, 44.5f },  0xC606A445, "u_m_m_vhtstationclerk_01"}, // Front
	{ { 2987.0f, 575.0f, 44.5f },  0xC606A445, "u_m_m_vhtstationclerk_01"}, // Back

	// Annesburg
	{ { 2933.0f, 1282.5f, 44.5f },  0xE9694F3F, "s_m_m_trainstationworker_01"}, // Middle
	{ { 2939.0f, 1287.0f, 44.5f },  0xE9694F3F, "s_m_m_trainstationworker_01"}, // Left
};
static const int gNumClerkSpots = sizeof(gClerkSpots) / sizeof(gClerkSpots[0]);

static void DisplaySubtitle(const char* message)
{
	// Thin wrapper over the shared DisplayObjective() in global.cpp
	DisplayObjective(message);
}

// ===== [ RESET CONTRACT ] =====
static void ResetContract() {
	clerkPed = 0;
	blipAdd = false;
	RetrySpawn = false;
	searchRadius = 0.0f;
	if (MAP::DOES_BLIP_EXIST(searchBlip) || MAP::DOES_BLIP_EXIST(bountyBlip)) {
		MAP::REMOVE_BLIP(&searchBlip);
		MAP::REMOVE_BLIP(&bountyBlip);

		searchBlip = 0;
		bountyBlip = 0;
	}
	PLAYER::_CLEAR_PED_EAGLE_EYE_TRAILS_FOR_PLAYER(me);
	PLAYER::_UNREGISTER_EAGLE_EYE_FOR_ENTITY(me, contractTarget);
	trailsActive = false;

	ENTITY::SET_ENTITY_AS_NO_LONGER_NEEDED(&contractTarget);
	contractTarget = 0;
	targetPos = { 0.f,0.f,0.f };

	targetSpawnX = 0.0f;
	targetSpawnY = 0.0f;
	targetSpawnZ = 0.0f;

	targetInTask = false;
	gAmbientPedActive = false;
	targetRemembersPlayer = false;
	lastContactMs = 0;


	// reset / hide prompts but KEEP their handles valid
	if (clerkPrompt && HUD::_UI_PROMPT_IS_VALID(clerkPrompt)) {
		HUD::_UI_PROMPT_SET_ENABLED(clerkPrompt, false);
		HUD::_UI_PROMPT_SET_VISIBLE(clerkPrompt, false);
		HUD::_UI_PROMPT_RESTART_MODES(clerkPrompt);
	}

	if (camPrompt && HUD::_UI_PROMPT_IS_VALID(camPrompt)) {
		HUD::_UI_PROMPT_SET_ENABLED(camPrompt, false);
		HUD::_UI_PROMPT_SET_VISIBLE(camPrompt, false);
		HUD::_UI_PROMPT_RESTART_MODES(camPrompt);
	}

	g_task = CONTRACT_WANDER;
	g_state = CONTRACT_NONE;
}

// ===== [ PROMPT SYSTEM ] =====
static void createClerkPrompt()
{
	clerkPrompt = HUD::_UI_PROMPT_REGISTER_BEGIN();
	HUD::_UI_PROMPT_SET_CONTROL_ACTION(clerkPrompt, 0x620A6C5E);
	//HUD::_UI_PROMPT_SET_TEXT(clerkPrompt, CREATE_VAR_STRING(10, "LITERAL_STRING", "Get Contract"));
	HUD::_UI_PROMPT_SET_STANDARDIZED_HOLD_MODE(clerkPrompt, SHORT_TIMED_EVENT_MP);
	HUD::_UI_PROMPT_SET_ENABLED(clerkPrompt, false);
	HUD::_UI_PROMPT_SET_VISIBLE(clerkPrompt, false);
	HUD::_UI_PROMPT_REGISTER_END(clerkPrompt);
}
static void CreateCameraPrompt()
{
	if (camPrompt) return;
	camGroup = joaat("CAMERA_ITEM_GROUP"); // the native camera group — required for the prompt to render inside the camera UI

	camPrompt = HUD::_UI_PROMPT_REGISTER_BEGIN();
	HUD::_UI_PROMPT_SET_CONTROL_ACTION(camPrompt, INPUT_CAMERA_TAKE_PHOTO);
	HUD::_UI_PROMPT_SET_TEXT(camPrompt, MISC::VAR_STRING(10, "LITERAL_STRING", "Take Photo"));
	HUD::_UI_PROMPT_SET_STANDARD_MODE(camPrompt, true);
	HUD::_UI_PROMPT_SET_PRIORITY(camPrompt, 3);
	HUD::_UI_PROMPT_SET_GROUP(camPrompt, camGroup, 0);
	HUD::_UI_PROMPT_SET_ENABLED(camPrompt, false);
	HUD::_UI_PROMPT_SET_VISIBLE(camPrompt, false);
	HUD::_UI_PROMPT_REGISTER_END(camPrompt);
}

// ===== [ CLERK PROMPT ] =====
static void UpdateClerkPrompt()
{
	Entity target = 0;
	if (!PLAYER::GET_PLAYER_TARGET_ENTITY(me, &target) || !ENTITY::IS_ENTITY_A_PED(target))
	{
		// Not aiming at a ped — don't leave the clerk prompt lingering on screen.
		HUD::_UI_PROMPT_SET_ENABLED(clerkPrompt, false);
		HUD::_UI_PROMPT_SET_VISIBLE(clerkPrompt, false);
		return;
	}
	clerkPed = ENTITY::GET_PED_INDEX_FROM_ENTITY_INDEX(target);
	if (!ENTITY::DOES_ENTITY_EXIST(clerkPed))
	{
		HUD::_UI_PROMPT_SET_ENABLED(clerkPrompt, false);
		HUD::_UI_PROMPT_SET_VISIBLE(clerkPrompt, false);
		return;
	}
	Hash model = ENTITY::GET_ENTITY_MODEL(clerkPed);
	Vector3 clerkPos = ENTITY::GET_ENTITY_COORDS(clerkPed, true, false);
	bool  found = false;
	int   bestIndex = -1;
	float bestDistSq = 0.0f;

	for (int i = 0; i < gNumClerkSpots; ++i)
	{
		if (gClerkSpots[i].model != model)
			continue; // only consider spots for this model

		const Vector3& spotPos = gClerkSpots[i].pos;

		float dx = playerPos.x - spotPos.x;
		float dy = playerPos.y - spotPos.y;
		float dz = playerPos.z - spotPos.z;

		float distSq = dx * dx + dy * dy + dz * dz;

		if (!found || distSq < bestDistSq)
		{
			found = true;
			bestDistSq = distSq;
			bestIndex = i;
		}
	}

	if (!found || bestIndex < 0)
	{
		HUD::_UI_PROMPT_SET_ENABLED(clerkPrompt, false);
		HUD::_UI_PROMPT_SET_VISIBLE(clerkPrompt, false);
		return;
	}

	const Vector3& spotPos = gClerkSpots[bestIndex].pos;

	// Player must be within 2.0f
	if (bestDistSq > (1.8f * 1.8f))
	{
		HUD::_UI_PROMPT_SET_ENABLED(clerkPrompt, false);
		HUD::_UI_PROMPT_SET_VISIBLE(clerkPrompt, false);
		return;
	}

	// Clerk must ALSO be within 2.0f of spot
	float cdx = clerkPos.x - spotPos.x;
	float cdy = clerkPos.y - spotPos.y;
	float cdz = clerkPos.z - spotPos.z;

	float clerkDistSq = cdx * cdx + cdy * cdy + cdz * cdz;

	//Default:: 1.0f
	if (clerkDistSq > (0.5f * 0.5f))
	{
		HUD::_UI_PROMPT_SET_ENABLED(clerkPrompt, false);
		HUD::_UI_PROMPT_SET_VISIBLE(clerkPrompt, false);
		return;
	}


	// Group the prompt with this clerk
	int groupId = HUD::_UI_PROMPT_GET_GROUP_ID_FOR_TARGET_ENTITY(clerkPed);
	HUD::_UI_PROMPT_SET_GROUP(clerkPrompt, groupId, 0);

	if (g_state == CONTRACT_NONE) {
		HUD::_UI_PROMPT_SET_TEXT(clerkPrompt, MISC::VAR_STRING(10, "LITERAL_STRING", "Get Contract"));
		HUD::_UI_PROMPT_SET_ENABLED(clerkPrompt, true);
		HUD::_UI_PROMPT_SET_VISIBLE(clerkPrompt, true);
		if (HUD::_UI_PROMPT_HAS_HOLD_MODE_COMPLETED(clerkPrompt)) {
			DisplaySubtitle("FIND THE TARGET");
			//PlayAnimOnPed(clerkPed, "script_rc@chrb@ig1_visit_clerk", "arthur_gives_money_player", 8.0f, 1.0f, -1, AF_FORCE_START | AF_NOT_INTERRUPTABLE | AF_USE_KINEMATIC_PHYSICS | AF_USE_MOVER_EXTRACTION | AF_UPPERBODY | AF_SECONDARY);
			PlayAnimOnPed(clerkPed, "script_rc@chrb@ig1_visit_clerk", "arthur_gives_money_player", 8.0f, 1.0f, -1, AF_FORCE_START | AF_NOT_INTERRUPTABLE | AF_USE_KINEMATIC_PHYSICS | AF_USE_MOVER_EXTRACTION | AF_UPPERBODY | AF_SECONDARY);
			PlayAnimOnPed(pedMe, "script_rc@chrb@ig_1_waitinginline", "gives_money_trainworker", 1.0f, 1.0f, -1, AF_FORCE_START | AF_NOT_INTERRUPTABLE);
			pickContract();
			HUD::_UI_PROMPT_RESTART_MODES(clerkPrompt);
			HUD::_UI_PROMPT_SET_ENABLED(clerkPrompt, false);
			HUD::_UI_PROMPT_SET_VISIBLE(clerkPrompt, false);
		}
	}
	else if (g_state == CONTRACT_UNKNOWN || g_state == CONTRACT_FOUND) {
		HUD::_UI_PROMPT_SET_TEXT(clerkPrompt, MISC::VAR_STRING(10, "LITERAL_STRING", "End Contract"));
		HUD::_UI_PROMPT_SET_ENABLED(clerkPrompt, true);
		HUD::_UI_PROMPT_SET_VISIBLE(clerkPrompt, true);
		if (HUD::_UI_PROMPT_HAS_HOLD_MODE_COMPLETED(clerkPrompt)) {
			DisplaySubtitle("CONTRACT ENDED");
			if (ENTITY::DOES_ENTITY_EXIST(contractTarget)) {
				PED::DELETE_PED(&contractTarget);
			}
			ResetContract();
			HUD::_UI_PROMPT_SET_ENABLED(clerkPrompt, false);
			HUD::_UI_PROMPT_SET_VISIBLE(clerkPrompt, false);
		}
	}
	else if (g_state == CONTRACT_DEAD) {
		HUD::_UI_PROMPT_SET_TEXT(clerkPrompt, MISC::VAR_STRING(10, "LITERAL_STRING", "Collect Payment"));
		HUD::_UI_PROMPT_SET_ENABLED(clerkPrompt, true);
		HUD::_UI_PROMPT_SET_VISIBLE(clerkPrompt, true);
		if (HUD::_UI_PROMPT_HAS_HOLD_MODE_COMPLETED(clerkPrompt)) {
			DisplaySubtitle("REWARD RECIEVED");
			PlayAnimOnPed(pedMe, "script_rc@chrb@ig1_visit_clerk", "arthur_gives_money_player", 8.0f, 1.0f, -1, AF_FORCE_START | AF_NOT_INTERRUPTABLE);
			PlayAnimOnPed(clerkPed, "script_rc@chrb@ig_1_waitinginline", "gives_money_trainworker", 1.0f, 1.0f, -1, AF_FORCE_START | AF_NOT_INTERRUPTABLE | AF_USE_KINEMATIC_PHYSICS | AF_USE_MOVER_EXTRACTION | AF_UPPERBODY | AF_SECONDARY);
			MONEY::_MONEY_INCREMENT_CASH_BALANCE(2500, 0);
			HUD::_UI_PROMPT_SET_ENABLED(clerkPrompt, false);
			HUD::_UI_PROMPT_SET_VISIBLE(clerkPrompt, false);
			ResetContract();
		}
	}
}

// ===== [ PLAY ANIM PED ] =====
static void PlayAnimOnPed(Ped ped, const char* dict, const char* name, float blendInSpeed, float blendOutSpeed, int duration, int flags) {
	STREAMING::REQUEST_ANIM_DICT(dict);
	while (!STREAMING::HAS_ANIM_DICT_LOADED(dict)) WAIT(0);
	TASK::CLEAR_PED_SECONDARY_TASK(ped);
	TASK::TASK_PLAY_ANIM(ped, dict, name, blendInSpeed, blendOutSpeed, duration, flags, 0.0f, 0, AIK_DISABLE_ARM_IK | AIK_DISABLE_TORSO_REACT_IK | AIK_DISABLE_TORSO_IK | AIK_DISABLE_HEAD_IK | AIK_DISABLE_LEG_IK, 0, 0, 0);

	STREAMING::REMOVE_ANIM_DICT(dict);
	// CLERK FLAGS AF_FORCE_START | AF_NOT_INTERRUPTABLE | AF_USE_KINEMATIC_PHYSICS | AF_USE_MOVER_EXTRACTION | AF_UPPERBODY | AF_SECONDARY
}

// ===== [ SPAWN PED ] =====
static Ped spawnPed(Hash pedModel, float coordX, float coordY, float coordZ)
{
	STREAMING::REQUEST_MODEL(pedModel, true);
	while (!STREAMING::HAS_MODEL_LOADED(pedModel)) WAIT(0);

	Ped pedSpawn = PED::CREATE_PED(pedModel, coordX, coordY, coordZ, 0.0f, 0, 1, 1, 1);
	PED::_SET_RANDOM_OUTFIT_VARIATION(pedSpawn, true);
	ENTITY::PLACE_ENTITY_ON_GROUND_PROPERLY(pedSpawn, 1);
	STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(pedModel);

	return pedSpawn;
}
static void pickContract() {
	ResetContract();
	int spawnIndex = rand() % g_ContractSpawnCount;
	SpawnDef chosen = g_ContractSpawns[spawnIndex];

	searchRadius = chosen.searchRadius;
	targetSpawnX = chosen.x;
	targetSpawnY = chosen.y;
	targetSpawnZ = chosen.z;

	Hash pedModel = chosen.models[rand() % chosen.modelCount];

	contractTarget = spawnPed(pedModel, targetSpawnX, targetSpawnY, targetSpawnZ);

	gAmbientPedActive = ENTITY::DOES_ENTITY_EXIST(contractTarget);

	if (!gAmbientPedActive) {
		RetrySpawn = true;
	}

	if (gAmbientPedActive)
	{
		RetrySpawn = false;
		ENTITY::SET_ENTITY_AS_MISSION_ENTITY(contractTarget, true, true);

		PED::SET_PED_COMBAT_ABILITY(contractTarget, CAL_PROFESSIONAL);
		PED::SET_PED_CAN_BE_INCAPACITATED(contractTarget, false); // never surrender / give up — fight to the death

		PED::SET_PED_CONFIG_FLAG(contractTarget, 130, false); //DisableTalkTo
		PED::SET_PED_CONFIG_FLAG(contractTarget, 211, true); //GiveAmbientDefaultTaskIfMissionPed
		PED::SET_PED_CONFIG_FLAG(contractTarget, 43, true); //DisableLadderClimbing
		PED::SET_PED_CONFIG_FLAG(contractTarget, 259, true); //CanAmbientHeadtrack
		PED::SET_PED_CONFIG_FLAG(contractTarget, 421, true); //AllowDoorBargingUnderCombat
		PED::SET_PED_CONFIG_FLAG(contractTarget, 435, true); //AlwaysRejectPlayerRobberyAttempt
		PED::SET_PED_CONFIG_FLAG(contractTarget, 467, true); //DisableHonorModifiers
		PED::SET_PED_CONFIG_FLAG(contractTarget, 0, true); //AllowMedicsToAttend
		PED::SET_PED_CONFIG_FLAG(contractTarget, 289, true); //TreatDislikeAsHateWhenInCombat
		PED::SET_PED_CONFIG_FLAG(contractTarget, 225, false); //??
		PED::SET_PED_CONFIG_FLAG(contractTarget, 437, false); //PCF_DisableWeatherConditionPerceptionChecks
		PED::SET_PED_CONFIG_FLAG(contractTarget, 442, true);
		PED::SET_PED_CONFIG_FLAG(contractTarget, 233, true); //PCF_PedIsEnemyToPlayer — true ON PURPOSE: false makes him a robbable civilian that cowers when aimed at. De-aggro turns it off; the bridge/re-aggro turns it back on.
		PED::SET_PED_CONFIG_FLAG(contractTarget, 351, true); //	PCF_DisableIntimidationBackingAway
		PED::SET_PED_CONFIG_FLAG(contractTarget, 356, true); //	PCF_BlockRobberyInteractionEscape

		PED::SET_PED_COMBAT_RANGE(contractTarget, CR_VERY_FAR);
		PED::SET_PED_COMBAT_MOVEMENT(contractTarget, 1);

		PED::SET_PED_COMBAT_ATTRIBUTES(contractTarget, 114, true); //CA_CAN_EXECUTE_TARGET
		PED::SET_PED_COMBAT_ATTRIBUTES(contractTarget, 41, true); //CA_CAN_COMMANDEER_VEHICLES
		PED::SET_PED_COMBAT_ATTRIBUTES(contractTarget, 125, true); //CA_QUIT_WHEN_TARGET_FLEES_INTERACTION_FIGHT
		PED::SET_PED_COMBAT_ATTRIBUTES(contractTarget, 21, false); //CA_CAN_CHASE_TARGET_ON_FOOT

		PED::SET_PED_COMBAT_ATTRIBUTES(contractTarget, 93, false); //CA_PREFER_MELEE
		PED::SET_PED_COMBAT_ATTRIBUTES(contractTarget, 54, true); //CA_ALWAYS_EQUIP_BEST_WEAPON
		PED::SET_PED_COMBAT_ATTRIBUTES(contractTarget, 12, true); //BLIND_FIRE_IN_COVER
		PED::SET_PED_COMBAT_ATTRIBUTES(contractTarget, 5, true); //ALWAYS_FIGHT — enabled in EnterCombat
		PED::SET_PED_COMBAT_ATTRIBUTES(contractTarget, 17, false); //ALWAYS_FLEE
		PED::SET_PED_COMBAT_ATTRIBUTES(contractTarget, 14, false); //CAN_INVESTIGATE
		PED::SET_PED_COMBAT_ATTRIBUTES(contractTarget, 58, true); //DISABLE_FLEE_FROM_COMBAT
		PED::SET_PED_COMBAT_ATTRIBUTES(contractTarget, 46, true); //CAN_FIGHT_ARMED_PEDS_WHEN_NOT_ARMED
		PED::SET_PED_COMBAT_ATTRIBUTES(contractTarget, 28, true); //CA_CAN_USE_FRUSTRATED_ADVANCE — push in instead of giving up
		PED::SET_PED_COMBAT_ATTRIBUTES(contractTarget, 50, true); //CA_CAN_CHARGE — charge instead of fleeing

		PED::SET_PED_FLEE_ATTRIBUTES(contractTarget, 1024, true);
		PED::SET_PED_FLEE_ATTRIBUTES(contractTarget, 512, true);
		PED::SET_PED_FLEE_ATTRIBUTES(contractTarget, 16384, true);
		PED::SET_PED_FLEE_ATTRIBUTES(contractTarget, 32768, false); // enabled = lets him cower/flee; off so he fights (rdr2mods forum)

		WEAPON::GIVE_WEAPON_TO_PED(contractTarget, WEAPON_REVOLVER_CATTLEMAN, 60, false, true, 0, true, 0.0f, 0.0f, ADD_REASON_DEFAULT, false, 0.0f, false);
		WEAPON::GIVE_WEAPON_TO_PED(contractTarget, WEAPON_MELEE_KNIFE, 0, false, true, 0, true, 0.0f, 0.0f, ADD_REASON_DEFAULT, false, 0.0f, false);

		//Hash relationshipGroup;
		//PED::ADD_RELATIONSHIP_GROUP("CONTRACT_TARGET_GROUP", &relationshipGroup);
		//PED::SET_PED_RELATIONSHIP_GROUP_HASH(contractTarget, relationshipGroup);
		//PED::SET_RELATIONSHIP_BETWEEN_GROUPS(6, relationshipGroup, PED::GET_PED_RELATIONSHIP_GROUP_HASH(pedMe)); // Hate

		StartPedWander(contractTarget);
		unknownBlip();
		g_task = CONTRACT_WANDER;
		g_state = CONTRACT_UNKNOWN;
	}
}

static void RetryContractSpawn() {
	if (!RetrySpawn)
		return;

	pickContract();
}

// ===== [ AI HELPERS ] =====
// Picks a weapon (or fists) per the tunables and starts combat with the player.
static void EnterCombat(Ped ped) {
	if (!ENTITY::DOES_ENTITY_EXIST(ped))
		return;

	TASK::CLEAR_PED_SECONDARY_TASK(ped);

	// Become hostile for the fight (so his own AI presses the attack, not just our task).
	PED::SET_PED_CONFIG_FLAG(ped, 233, true);     // PCF_PedIsEnemyToPlayer
	PED::SET_PED_COMBAT_ATTRIBUTES(ped, 5, true); // CA_ALWAYS_FIGHT

	if ((rand() % 100) < kArmedChancePct) {
		bool useGun = (rand() % 100) < kGunVsKnifePct;
		if (useGun) {
			PED::SET_PED_COMBAT_ATTRIBUTES(ped, 93, false); // CA_PREFER_MELEE off
			PED::SET_PED_COMBAT_ATTRIBUTES(ped, 54, true);  // CA_ALWAYS_EQUIP_BEST_WEAPON
			WEAPON::SET_CURRENT_PED_WEAPON(ped, WEAPON_REVOLVER_CATTLEMAN, true, WEAPON_ATTACH_POINT_HAND_PRIMARY, false, false);
		}
		else {
			PED::SET_PED_COMBAT_ATTRIBUTES(ped, 93, true);  // prefer melee so he keeps the knife
			PED::SET_PED_COMBAT_ATTRIBUTES(ped, 54, false); // don't auto-swap to the revolver
			WEAPON::SET_CURRENT_PED_WEAPON(ped, WEAPON_MELEE_KNIFE, true, WEAPON_ATTACH_POINT_HAND_PRIMARY, false, false);
		}
	}
	else {
		PED::SET_PED_COMBAT_ATTRIBUTES(ped, 93, true);   // fists
		PED::SET_PED_COMBAT_ATTRIBUTES(ped, 54, false);  // don't draw a weapon
		WEAPON::SET_CURRENT_PED_WEAPON(ped, joaat("WEAPON_UNARMED"), true, WEAPON_ATTACH_POINT_HAND_PRIMARY, false, false);
	}

	PED::SET_COMBAT_FLOAT(ped, 20, 0.0f); // far
	PED::SET_COMBAT_FLOAT(ped, 21, 0.0f); // near
	TASK::TASK_COMBAT_PED(ped, pedMe, 0, 0);
	PED::SET_PED_KEEP_TASK(ped, true); // make the combat task stick so he can't drop into flee/cower

	g_task = CONTRACT_AGGRO;
	targetRemembersPlayer = true;
	lastContactMs = GetTickCount64();
}

// True if the target has a clear line of sight to the player.
// NOTE: CAN_PED_SEE_ENTITY returned non-zero even with NO line of sight, which kept the
// target permanently "in contact" and stopped it from ever de-aggroing. A raw LOS trace
// (blocked by world geometry) is the reliable check.
static bool TargetCanSeePlayer() {
	if (!ENTITY::DOES_ENTITY_EXIST(contractTarget))
		return false;
	// HAS_ENTITY_CLEAR_LOS_TO_ENTITY reports a clear line even at huge distances (it was
	// returning true at ~190 m). Cap it to his sight range so "he sees me" is realistic.
	if (TargetPlayerDistSq() > (kReAggroSightDist * kReAggroSightDist))
		return false;
	return ENTITY::HAS_ENTITY_CLEAR_LOS_TO_ENTITY(contractTarget, pedMe, 17) != 0;
}

static float TargetPlayerDistSq() {
	float dx = playerPos.x - targetPos.x;
	float dy = playerPos.y - targetPos.y;
	float dz = playerPos.z - targetPos.z;
	return dx * dx + dy * dy + dz * dz;
}

// First-contact trigger: he turns hostile when the player aims at or intimidates
// him while he is looking at the player.
static void startPedCombat(Ped ped) {
	if (!ENTITY::DOES_ENTITY_EXIST(ped))
		return;

	bool aimingAt    = PLAYER::IS_PLAYER_FREE_AIMING_AT_ENTITY(me, ped) && PED::IS_PED_HEADTRACKING_PED(ped, pedMe);
	bool intimidated = PED::_IS_PED_INTIMIDATED(ped) && PED::IS_PED_HEADTRACKING_PED(ped, pedMe);
	bool damaged     = ENTITY::HAS_ENTITY_BEEN_DAMAGED_BY_ENTITY(ped, pedMe, true, true);

	if (aimingAt || intimidated || damaged)
		EnterCombat(ped);
}
static void StartPedWander(Ped ped)
{
	if (!ENTITY::DOES_ENTITY_EXIST(ped))
		return;

	TASK::CLEAR_PED_TASKS(ped, true, true);
	TASK::SET_PED_PATH_PREFER_TO_AVOID_WATER(ped, true, searchRadius);
	TASK::SET_PED_PATH_MAY_ENTER_WATER(ped, false);
	TASK::TASK_WANDER_IN_AREA(
		ped,
		targetSpawnX, targetSpawnY, targetSpawnZ,
		searchRadius,
		0.0f,
		0.0f,
		1
	);
	targetInTask = true;
	g_task = CONTRACT_WANDER;
}
static void AIHandler()
{
	if (!gAmbientPedActive)
		return;

	if (!ENTITY::DOES_ENTITY_EXIST(contractTarget) ||
		PED::IS_PED_DEAD_OR_DYING(contractTarget, true))
	{
		gAmbientPedActive = false;
		return;
	}

	switch (g_task)
	{
	case CONTRACT_WANDER:
		if (!targetInTask)
		{
			StartPedWander(contractTarget);
		}

		// If his own AI is already fighting the player (e.g. he was antagonized into it),
		// commit him to the fight and hand control to our AGGRO state so de-aggro governs it.
		// No weapon re-roll here — he's already armed and shooting.
		if (PED::IS_PED_IN_COMBAT(contractTarget, pedMe))
		{
			PED::SET_PED_CONFIG_FLAG(contractTarget, 233, true);     // enemy
			PED::SET_PED_COMBAT_ATTRIBUTES(contractTarget, 5, true); // CA_ALWAYS_FIGHT
			PED::SET_PED_KEEP_TASK(contractTarget, true);
			targetRemembersPlayer = true;
			lastContactMs = GetTickCount64();
			g_task = CONTRACT_AGGRO;
			break;
		}

		if (targetRemembersPlayer)
		{
			// Already met the player: attack again the moment he is back in sight.
			if (TargetCanSeePlayer() &&
				TargetPlayerDistSq() <= (kReAggroSightDist * kReAggroSightDist))
			{
				EnterCombat(contractTarget);
			}
		}
		else
		{
			// First contact: aim / intimidation trigger.
			startPedCombat(contractTarget);
		}
		break;

	case CONTRACT_AGGRO:
	{
		bool inContact = TargetCanSeePlayer() ||
			TargetPlayerDistSq() <= (kDeAggroDist * kDeAggroDist);

		if (inContact)
		{
			lastContactMs = GetTickCount64();
		}
		else if (GetTickCount64() - lastContactMs > kDeAggroGraceMs)
		{
			// Lost the player long enough: drop hostility so his own combat AI stops
			// hunting the player, then holster and wander. Keep remembering him so coming
			// back into sight re-triggers combat.
			PED::SET_PED_CONFIG_FLAG(contractTarget, 233, false);     // no longer enemy
			PED::SET_PED_COMBAT_ATTRIBUTES(contractTarget, 5, false); // stop auto-fighting
			TASK::CLEAR_PED_TASKS(contractTarget, true, true);
			StartPedWander(contractTarget);
		}
		break;
	}
	}
}

// ===== [ BLIPS ] =====
static void unknownBlip() {
	searchBlip = MAP::BLIP_ADD_FOR_RADIUS(BLIP_STYLE_MP_MISSION_GIVER, targetSpawnX, targetSpawnY, targetSpawnZ, searchRadius);
	MAP::SET_BLIP_SPRITE(searchBlip, MISC::GET_HASH_KEY("blip_ambient_bounty_target"), true);
	MAP::BLIP_ADD_MODIFIER(searchBlip, MISC::GET_HASH_KEY("BLIP_MODIFIER_SCALE_2"));
	MAP::BLIP_ADD_MODIFIER(searchBlip, MISC::GET_HASH_KEY("BLIP_MODIFIER_MP_COLOR_32"));
	MAP::BLIP_ADD_MODIFIER(searchBlip, MISC::GET_HASH_KEY("BLIP_MODIFIER_VERYHIGH_CATEGORY"));
	MAP::_SET_BLIP_NAME(searchBlip, MISC::VAR_STRING(10, "LITERAL_STRING", "Contract Target"));
}
static void foundBlip() {
	bountyBlip = MAP::BLIP_ADD_FOR_ENTITY(MISC::GET_HASH_KEY("BLIP_STYLE_CREATOR_DEFAULT"), contractTarget);
	MAP::SET_BLIP_SPRITE(bountyBlip, MISC::GET_HASH_KEY("blip_ambient_bounty_target"), true);
	MAP::BLIP_ADD_MODIFIER(bountyBlip, MISC::GET_HASH_KEY("BLIP_MODIFIER_MP_COLOR_32"));
	MAP::BLIP_ADD_MODIFIER(bountyBlip, MISC::GET_HASH_KEY("BLIP_MODIFIER_VERYHIGH_CATEGORY"));
	MAP::_SET_BLIP_NAME(bountyBlip, MISC::VAR_STRING(10, "LITERAL_STRING", "Contract Target"));
}

// ===== [ TRACKING TRAILS ] =====
static void enableTrail() {
	if (g_state != CONTRACT_DEAD && g_state != CONTRACT_NONE) {
		float dx = playerPos.x - targetPos.x;
		float dy = playerPos.y - targetPos.y;
		float dz = playerPos.z - targetPos.z;

		float distSq = dx * dx + dy * dy + dz * dz;

		// 5 feet 1.524 meters squared
		const float kEnableDistSq = 12.0f * 12.0f;

		if (distSq <= kEnableDistSq)
		{
			// Player is close allow trails
			if (!trailsActive) {
				PLAYER::_REGISTER_EAGLE_EYE_FOR_ENTITY(me, contractTarget, false);
				PLAYER::EAGLE_EYE_SET_CUSTOM_ENTITY_TINT(contractTarget, 255, 255, 0);
				trailsActive = true;
			}
		}
		else
		{
			// Player is far nuke trails for THIS player only
			if (trailsActive) {
				PLAYER::_CLEAR_PED_EAGLE_EYE_TRAILS_FOR_PLAYER(me);
				PLAYER::_UNREGISTER_EAGLE_EYE_FOR_ENTITY(me, contractTarget);
				trailsActive = false;
			}
		}
	}
	else {

		if (trailsActive) {
			PLAYER::_CLEAR_PED_EAGLE_EYE_TRAILS_FOR_PLAYER(me);
			PLAYER::_UNREGISTER_EAGLE_EYE_FOR_ENTITY(me, contractTarget);
			trailsActive = false;
		}
	}
}

// ===== [ CONFIRM DEATH ] =====
static void targetFindCheck() {
	if (!contractTarget || !ENTITY::DOES_ENTITY_EXIST(contractTarget))
		return;
	Entity interacted = 0;
	bool interactingWithTarget = PLAYER::GET_PLAYER_INTERACTION_TARGET_ENTITY(me, &interacted, false, false) && (interacted == (Entity)contractTarget);
	bool freeAimingAt = PLAYER::IS_PLAYER_FREE_AIMING_AT_ENTITY(me, contractTarget);
	bool targetting = PLAYER::IS_PLAYER_TARGETTING_ENTITY(me, contractTarget, false);
	bool damageCheck = ENTITY::HAS_ENTITY_BEEN_DAMAGED_BY_ENTITY(contractTarget, pedMe, true, true);
	bool deadCheck = ENTITY::IS_ENTITY_DEAD(contractTarget);
	bool combatCheck = PED::IS_PED_IN_COMBAT(contractTarget, pedMe);

	// IS_PLAYER_FREE_AIMING_AT_ENTITY / IS_PLAYER_TARGETTING_ENTITY register by camera
	// direction at ANY distance — you can "aim at" the target from across the map. Only
	// treat that as finding the target when the player is genuinely near it AND has LOS.
	float fdx = playerPos.x - targetPos.x;
	float fdy = playerPos.y - targetPos.y;
	float fdz = playerPos.z - targetPos.z;
	bool nearTarget = (fdx * fdx + fdy * fdy + fdz * fdz) <= (searchRadius * searchRadius);
	bool playerHasLOS = ENTITY::HAS_ENTITY_CLEAR_LOS_TO_ENTITY(pedMe, contractTarget, 17) != 0;
	bool aimedAtTarget = (freeAimingAt || targetting) && nearTarget && playerHasLOS;

	if (deadCheck) {
		if (MAP::DOES_BLIP_EXIST(bountyBlip) || MAP::DOES_BLIP_EXIST(searchBlip)) {
			MAP::REMOVE_BLIP(&bountyBlip);
			bountyBlip = 0;
			MAP::REMOVE_BLIP(&searchBlip);
			searchBlip = 0;
			g_state = CONTRACT_FOUND;
			return;
		}
	}
	if (interactingWithTarget || aimedAtTarget || damageCheck || combatCheck) {
		DisplaySubtitle("TARGET FOUND");
		if (MAP::DOES_BLIP_EXIST(searchBlip)) {
			MAP::REMOVE_BLIP(&searchBlip);
			searchBlip = 0;
		}
		if (!MAP::DOES_BLIP_EXIST(bountyBlip)) {
			foundBlip();
		}
		g_state = CONTRACT_FOUND;
	}
}
static bool IsInHandheldCamera()
{
	return PAD::IS_CONTROL_ENABLED(0, INPUT_CAMERA_TAKE_PHOTO) ||
		PAD::IS_CONTROL_ENABLED(0, INPUT_CAMERA_ADVANCED_TAKE_PHOTO);
}
static void targetDeathCheck()
{
	if (!camPrompt) CreateCameraPrompt();
	if (!ENTITY::DOES_ENTITY_EXIST(contractTarget))
		return;
	if (PED::IS_PED_DEAD_OR_DYING(contractTarget, true)) {
		if (!blipAdd) {
			if (MAP::DOES_BLIP_EXIST(bountyBlip)) {
				MAP::REMOVE_BLIP(&bountyBlip);
				bountyBlip = 0;
			}
			DisplaySubtitle("TAKE PICTURE OF CORPSE");
			bountyBlip = MAP::BLIP_ADD_FOR_COORDS(MISC::GET_HASH_KEY("BLIP_STYLE_CREATOR_DEFAULT"), targetPos);
			MAP::SET_BLIP_SPRITE(bountyBlip, MISC::GET_HASH_KEY("blip_ambient_bounty_target"), true);
			MAP::BLIP_ADD_MODIFIER(bountyBlip, MISC::GET_HASH_KEY("BLIP_MODIFIER_MP_COLOR_6"));
			MAP::BLIP_ADD_MODIFIER(bountyBlip, MISC::GET_HASH_KEY("BLIP_MODIFIER_PULSE_FOREVER"));
			MAP::BLIP_ADD_MODIFIER(bountyBlip, MISC::GET_HASH_KEY("BLIP_MODIFIER_VERYHIGH_CATEGORY"));
			MAP::_SET_BLIP_NAME(bountyBlip, MISC::VAR_STRING(10, "LITERAL_STRING", "Contract Target"));
			blipAdd = true;
		}
		if (blipAdd) {
			MAP::SET_BLIP_COORDS(bountyBlip, targetPos);
		}

		// Making our prompt the ACTIVE prompt in the native CAMERA_ITEM_GROUP this frame
		// renders it inside the handheld camera AND makes it intercept the take-photo input:
		// the corpse shot triggers our prompt (IS_PRESSED) instead of the native capture, so
		// the game never saves it. Once we reach CONTRACT_DEAD this stops running and the
		// native shutter works (and saves) normally again.
		if (IsInHandheldCamera())
		{
			if (ENTITY::IS_ENTITY_ON_SCREEN(contractTarget)) {
				HUD::_UI_PROMPT_SET_ENABLED(camPrompt, true);
				HUD::_UI_PROMPT_SET_VISIBLE(camPrompt, true);
				HUD::_UI_PROMPT_SET_ACTIVE_GROUP_THIS_FRAME(
					camGroup,
					"CAM_CONG_HC",
					1, 0, 0, 0
				);
				if (HUD::_UI_PROMPT_IS_PRESSED(camPrompt))
				{
					DisplaySubtitle("RETURN TO CLERK");
					AUDIO::PLAY_SOUND_FRONTEND("take_photo", "Photo_Mode_Sounds", true, 0);
					HUD::_UI_PROMPT_SET_ENABLED(camPrompt, false);
					HUD::_UI_PROMPT_SET_VISIBLE(camPrompt, false);
					MAP::REMOVE_BLIP(&bountyBlip);
					bountyBlip = 0;
					g_state = CONTRACT_DEAD;
				}
			}
		}
	}
}
// ===== [ MAIN ] =====
void main() {
	createClerkPrompt();
	while (true) {
		me = PLAYER::PLAYER_ID();
		pedMe = PLAYER::PLAYER_PED_ID();
		playerPos = ENTITY::GET_ENTITY_COORDS(pedMe, true, false);
		if (contractTarget && ENTITY::DOES_ENTITY_EXIST(contractTarget)) {
			targetPos = ENTITY::GET_ENTITY_COORDS(contractTarget, true, false);
		}

		// --- TEMP DEBUG: aggro/LOS readout. state 0=NONE 1=UNK 2=FOUND 3=DEAD  task 0=WANDER 1=AGGRO
		{
			bool haveTarget = contractTarget && ENTITY::DOES_ENTITY_EXIST(contractTarget);
			int losRaw = haveTarget ? (ENTITY::HAS_ENTITY_CLEAR_LOS_TO_ENTITY(contractTarget, pedMe, 17) ? 1 : 0) : -1;
			int sees = haveTarget ? (TargetCanSeePlayer() ? 1 : 0) : -1; // raw LOS capped to sight range — what the logic actually uses
			float dist = -1.0f;
			if (haveTarget) {
				float dx = playerPos.x - targetPos.x, dy = playerPos.y - targetPos.y, dz = playerPos.z - targetPos.z;
				dist = sqrtf(dx * dx + dy * dy + dz * dz);
			}
			char dbg[180];
			sprintf(dbg, "state=%d task=%d remember=%d losRaw=%d sees=%d dist=%.1f", (int)g_state, (int)g_task,
				targetRemembersPlayer ? 1 : 0, losRaw, sees, dist);
			DrawTextToScreen(dbg, 0.05f, 0.08f, 0.4f, 255, 255, 0, 255);
		}
		// --- END TEMP DEBUG ---

		// Press U to skip the clerk and immediately roll a new contract (debug/testing)
		if (IsKeyJustUp(0x55)) {
			if (ENTITY::DOES_ENTITY_EXIST(contractTarget)) {
				PED::DELETE_PED(&contractTarget);
			}
			pickContract();
		}

		switch (g_state) {
		case CONTRACT_NONE:
			UpdateClerkPrompt();
			RetryContractSpawn();
			break;
		case CONTRACT_UNKNOWN:
			UpdateClerkPrompt();
			enableTrail();
			AIHandler();
			targetFindCheck();
			break;
		case CONTRACT_FOUND:
			UpdateClerkPrompt();
			AIHandler();
			enableTrail();
			targetDeathCheck();
			break;
		case CONTRACT_DEAD:
			enableTrail();
			UpdateClerkPrompt();
			break;
		}
		WAIT(0);
	}
}

void ScriptMain() {
	srand((unsigned)GetTickCount64());
	main();
}
