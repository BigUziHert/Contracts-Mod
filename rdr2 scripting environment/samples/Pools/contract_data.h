#pragma once
/*
	Contracts mod — DATA. Where targets spawn, what they look like, what they pay, who hands out the work.
	All logic lives in script.cpp; this header is included by script.cpp only.

	Adding a town target  : add a Town(...) row to kContracts.
	Adding a one-off spot : add a row whose onSpawned hook dresses the location (camp props, extra peds)
	                        and whose onCleanup hook removes them when the contract ends.
	Adding a target type  : write a TargetBehavior { setup, update } in script.cpp and point the row at it
	                        (animals are peds too — only the behaviour differs).
	Adding a giver type   : add a GiverKind, add rows to kGivers, handle the kind in script.cpp.
*/

#include "global.h"

// ===== [ COMPILE-TIME JOAAT ] =====
// Same hash as MISC::GET_HASH_KEY (Jenkins one-at-a-time over lower-cased ASCII), evaluated by the
// compiler so the tables below never call a game native during DLL static-init.
constexpr Hash Joaat(const char* s)
{
	unsigned int h = 0;
	for (; *s; ++s)
	{
		unsigned int c = (unsigned char)*s;
		if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
		h += c;
		h += h << 10;
		h ^= h >> 6;
	}
	h += h << 3;
	h ^= h >> 11;
	h += h << 15;
	return h;
}
// Proven against hashes this mod already relied on. A wrong algorithm fails the build, not the game.
static_assert(Joaat("s_m_m_trainstationworker_01")      == 0xE9694F3F, "Joaat mismatch");
static_assert(Joaat("u_m_m_rhdtrainstationworker_01")   == 0x3F0EC349, "Joaat mismatch");
static_assert(Joaat("u_m_m_strfreightstationowner_01")  == 0x1173F849, "Joaat mismatch");
static_assert(Joaat("u_m_m_vhtstationclerk_01")         == 0xC606A445, "Joaat mismatch");
static_assert(Joaat("a_m_m_rhdtownfolk_02")             == 0x6833EBEE, "Joaat mismatch");
static_assert(Joaat("A_M_M_RHDTOWNFOLK_02")             == 0x6833EBEE, "Joaat must be case-insensitive");
static_assert(Joaat("a_m_m_rhdtownfolk_01_laborer")     == 0x009F6A48, "Joaat mismatch");
static_assert(Joaat("a_m_m_rhdforeman_01")              == 0x94EAA58F, "Joaat mismatch");
static_assert(Joaat("a_m_m_rhdupperclass_01")           == 0xAFCAF759, "Joaat mismatch");
static_assert(Joaat("g_m_y_uniexconfeds_01")            == 0xDA82E29B, "Joaat mismatch");
static_assert(Joaat("a_m_m_cardgameplayers_01")         == 0xC7458219, "Joaat mismatch");
static_assert(Joaat("s_m_m_rhdcowpoke_01")              == 0xEDF61C81, "Joaat mismatch");
static_assert(Joaat("g_m_m_unigrays_01")                == 0x15EB41F6, "Joaat mismatch");

// ===== [ TUNABLES ] =====
namespace Tune
{
	// --- target combat ---
	constexpr int   kArmedChancePct   = 90;      // of every aggro: 90% draws a weapon, 10% fists
	constexpr int   kGunVsKnifePct    = 75;      // of the armed rolls: 75% gun, 25% knife
	constexpr float kReAggroSightDist = 45.0f;   // how far he can see / notice the player. Must exceed kDeAggroDist.
	constexpr float kDeAggroDist      = 20.0f;   // inside this he stays engaged with no line of sight needed
	constexpr DWORD kDeAggroGraceMs   = 8000;    // contact must be lost this long before he gives up the chase
	// --- player feedback ---
	constexpr float kTrailEnableDist  = 12.0f;   // eagle-eye trail on the target within this range
	// --- giver interaction ---
	constexpr float kGiverPlayerDist  = 1.8f;    // player must be this close to the giver's spot
	constexpr float kGiverSpotDist    = 0.5f;    // giver must be this close to their own spot
	// --- payout (all money is in CENTS: 2500 = $25.00) ---
	constexpr int   kPayoutMinCents   = 2500;    // $25 floor
	constexpr int   kPayoutMaxCents   = 17500;   // $175 ceiling
	constexpr float kFullPayMinutes   = 20.0f;   // a contract this long (or longer) pays the ceiling; shorter pays proportionally less
	constexpr float kWantedPayoutMult = 0.5f;    // multiplier if the player got wanted after the crime
	constexpr int   kPayoutStepCents  = 25;      // payout is rounded down to this step
	// --- hand-in: photo + money on the counter ---
	constexpr DWORD kCashSpawnDelayMs = 1500;    // clerk anim runs this long before the cash appears
	constexpr DWORD kHandInCardMs     = 2500;    // the corpse photo stays in the player's hand this long
	constexpr float kCounterHeight    = 1.0f;    // cash spawns this far above the clerk's feet (drops onto the counter)
	constexpr DWORD kCashTimeoutMs    = 120000;  // if the player never takes the cash, the contract closes anyway
	// --- contract card ---
	constexpr DWORD kCardOpenDelayMs  = 2500;    // wait for the clerk handoff anim before the player examines the card
	constexpr int   kInspectCardKey   = 0x49;    // 'I' — look at the card again mid-contract (photograph recipe, as Contracts Remastered)
	constexpr int   kInspectCardAltKey = 0x4F;   // 'O' — same card through the cigarette-card recipe (debug comparison; flips natively)
	constexpr bool  kCardFaceRenderTarget = true;// draw the target's photo onto the card prop (render target). Back panel always draws.
	constexpr bool  kPedshotHidden    = true;    // hide the target while his photo is taken next to the player
	constexpr int   kPedshotReadyMs   = 4000;    // wait for the ped's assets to stream before the photo
	constexpr int   kPedshotSettleMs  = 750;     // give the photo renderer this long before moving the ped away
	// --- streaming ---
	constexpr DWORD kStreamTimeoutMs  = 5000;    // give up on a model / anim that won't load instead of hanging the script
	constexpr int   kSpawnAttempts    = 3;       // contract rerolls before "no contracts available"
}

// ===== [ TYPES ] =====
struct ModelSet { const Hash* list; int count; };
template<size_t N> constexpr ModelSet Models(const Hash (&arr)[N]) { return { arr, (int)N }; }

struct ContractDef;

// How a target behaves. setup runs once right after spawn (flags, weapons, first task);
// update runs every frame while the target is alive. Issue ped tasks on transitions only.
struct TargetBehavior
{
	void (*setup)(Ped target, const ContractDef& def);
	void (*update)(Ped target, const ContractDef& def);
};
extern const TargetBehavior kHumanTarget;   // script.cpp — armed townsman: wanders, aggros, de-aggros

struct ContractDef
{
	const char*           name;
	const char*           targetDesc;   // card back, line 1: "The target is a dock worker"
	const char*           hint;         // card back, line 2: "Works the docks in Saint Denis"
	Vector3               spawn;
	float                 searchRadius;
	ModelSet              models;
	const TargetBehavior* behavior;
	void (*onSpawned)(const ContractDef& def);  // optional: dress the location (camp, props, extra peds)
	void (*onCleanup)();                        // optional: undo onSpawned when the contract ends
};

// A townsman wandering a radius around the spawn point.
inline ContractDef Town(const char* name, const char* targetDesc, const char* hint,
                        float x, float y, float z, float searchRadius, ModelSet models)
{
	return { name, targetDesc, hint, Vector3(x, y, z), searchRadius, models, &kHumanTarget, nullptr, nullptr };
}

enum GiverKind { GIVER_CLERK };   // future: GIVER_SHERIFF, GIVER_BARTENDER ...

struct GiverSpot
{
	Vector3   pos;
	Hash      model;
	GiverKind kind;
};

// ===== [ TARGET MODELS ] =====
static constexpr Hash SD_DOCK[] = {
	Joaat("a_m_m_sddockworkers_02"),
	Joaat("a_m_m_nbxdockworkers_01"),
};
static constexpr Hash SD_RICH_BAR[] = {
	Joaat("a_m_m_nbxupperclass_01"),
	Joaat("a_m_m_middlesdtownfolk_01"),
};

static constexpr Hash BLW_BAR[] = {
	Joaat("a_m_m_blwtownfolk_01"),
	Joaat("a_m_m_blwlaborer_02"),
	Joaat("a_m_m_cardgameplayers_01"),
	Joaat("a_m_o_blwupperclass_01"),
	Joaat("a_m_m_blwupperclass_01"),
	Joaat("s_m_m_blwcowpoke_01"),
	Joaat("a_m_m_blwforeman_01"),
	Joaat("a_m_m_blwlaborer_01"),
};
static constexpr Hash BLW_DOCK_RIGHT[] = {
	Joaat("a_m_m_blwlaborer_02"),
	Joaat("a_m_m_blwforeman_01"),
	Joaat("s_m_m_blwcowpoke_01"),
};
static constexpr Hash BLW_DOCK_LEFT[] = {
	Joaat("a_m_m_blwlaborer_02"),
	Joaat("a_m_m_blwlaborer_01"),
	Joaat("a_m_m_blwforeman_01"),
	Joaat("s_m_m_blwcowpoke_01"),
	Joaat("a_m_o_blwupperclass_01"),
	Joaat("a_m_m_blwupperclass_01"),
	Joaat("a_m_m_blwtownfolk_01"),
};
static constexpr Hash BLW_CONSTRUCTION[] = {
	Joaat("a_m_m_blwlaborer_01"),
	Joaat("a_m_m_blwforeman_01"),
	Joaat("s_m_m_blwcowpoke_01"),
};

static constexpr Hash STRAWBERRY[] = {
	Joaat("a_m_m_strlaborer_01"),
	Joaat("s_m_m_liveryworker_01"),
	Joaat("a_m_m_strtownfolk_01"),
	Joaat("s_m_m_strcowpoke_01"),
	Joaat("s_m_m_valcowpoke_01"),
	Joaat("g_m_m_unicriminals_02"),
	Joaat("a_m_m_strfancytourist_01"),
	Joaat("s_m_m_ambientlawrural_01"),
};

static constexpr Hash VAL_RICH_BAR[] = {
	Joaat("a_m_m_vallaborer_01"),
	Joaat("a_m_m_valtownfolk_02"),
	Joaat("a_m_m_valfarmer_01"),
	Joaat("s_m_m_valcowpoke_01"),
	Joaat("a_m_m_valtownfolk_01"),
	Joaat("s_m_m_asbcowpoke_01"),
	Joaat("re_townrobbery_males_01"),
	Joaat("a_m_m_cardgameplayers_01"),
};
static constexpr Hash VAL_POOR_BAR[] = {
	Joaat("a_m_m_vallaborer_01"),
	Joaat("a_m_m_valtownfolk_02"),
	Joaat("a_m_m_valfarmer_01"),
	Joaat("s_m_m_valcowpoke_01"),
	Joaat("a_m_m_valtownfolk_01"),
	Joaat("s_m_m_asbcowpoke_01"),
	Joaat("re_townrobbery_males_01"),
};
static constexpr Hash VAL_AUCTION[] = {
	Joaat("a_m_m_vallaborer_01"),
	Joaat("a_m_m_valtownfolk_02"),
	Joaat("a_m_m_valfarmer_01"),
	Joaat("s_m_m_valcowpoke_01"),
	Joaat("a_m_m_valtownfolk_01"),
	Joaat("s_m_m_asbcowpoke_01"),
};

static constexpr Hash RHODES[] = {
	Joaat("a_m_m_rhdtownfolk_02"),
	Joaat("a_m_m_rhdtownfolk_01_laborer"),
	Joaat("a_m_m_rhdforeman_01"),
	Joaat("a_m_m_rhdupperclass_01"),
	Joaat("g_m_y_uniexconfeds_01"),
	Joaat("a_m_m_cardgameplayers_01"),
	Joaat("s_m_m_rhdcowpoke_01"),
	Joaat("g_m_m_unigrays_01"),
};

// ===== [ CONTRACTS ] =====
static const ContractDef kContracts[] = {
	Town("Rhodes Bar",              "The target is a regular at the saloon",   "Drinks at the saloon in Rhodes",              1370.0f,  -1354.0f,   78.0f, 65.0f, Models(RHODES)),
	Town("Rhodes Shops",            "The target runs errands in town",         "Seen around the shops in Rhodes",             1309.5f,  -1292.5f,   76.0f, 50.0f, Models(RHODES)),
	Town("Rhodes Mill",             "The target works at the lumber mill",     "Works the mill on the east side of Rhodes",   1388.5f,  -1313.0f,   77.5f, 60.0f, Models(RHODES)),
	Town("Rhodes Trapper",          "The target trades in pelts",              "Hangs around the trapper north of Rhodes",    1341.0f,  -1148.5f,   82.5f, 50.0f, Models(RHODES)),

	Town("Blackwater Bar",          "The target is a heavy drinker",           "Drinks at the saloon in Blackwater",          -814.0f,  -1318.5f,   44.0f, 45.0f, Models(BLW_BAR)),
	Town("Blackwater Harbor Right", "The target is a dock hand",               "Works the docks in Blackwater",               -730.0f,  -1275.0f,   44.0f, 60.0f, Models(BLW_DOCK_RIGHT)),
	Town("Blackwater Harbor Left",  "The target is a dock hand",               "Works the docks in Blackwater",               -732.0f,  -1243.0f,   45.0f, 60.0f, Models(BLW_DOCK_LEFT)),
	Town("Blackwater Construction", "The target is a laborer",                 "Works the building site in Blackwater",       -809.0f,  -1220.0f,   43.5f, 55.0f, Models(BLW_CONSTRUCTION)),

	Town("Valentine Rich Bar",      "The target is a gambler",                 "Drinks at the big saloon in Valentine",       -309.0f,    808.5f,  119.0f, 40.0f, Models(VAL_RICH_BAR)),
	Town("Valentine Poor Bar",      "The target is a drunk",                   "Drinks at the cheap saloon in Valentine",     -243.0f,    769.0f,  118.0f, 50.0f, Models(VAL_POOR_BAR)),
	Town("Valentine Auction Yard",  "The target is a livestock hand",          "Works the auction yard in Valentine",         -236.0f,    652.0f,  113.0f, 80.0f, Models(VAL_AUCTION)),

	Town("Strawberry Bottom Left",  "The target is a townsman",                "Hangs around the lower end of Strawberry",   -1828.0f,   -413.5f,  161.0f, 55.0f, Models(STRAWBERRY)),
	Town("Strawberry Bottom Right", "The target is a townsman",                "Hangs around the lower end of Strawberry",   -1789.0f,   -427.0f,  115.5f, 55.0f, Models(STRAWBERRY)),
	Town("Strawberry Top Left",     "The target is a townsman",                "Hangs around the upper end of Strawberry",   -1795.0f,   -364.0f,  162.0f, 55.0f, Models(STRAWBERRY)),
	Town("Strawberry Top Right",    "The target is a townsman",                "Hangs around the upper end of Strawberry",   -1775.0f,   -387.5f,  157.0f, 55.0f, Models(STRAWBERRY)),

	Town("Saint Denis Dock 1",      "The target is a dock worker",             "Works the docks in Saint Denis",              2778.5f,  -1462.5f,   45.5f, 35.0f, Models(SD_DOCK)),
	Town("Saint Denis Dock 2",      "The target is a dock worker",             "Works the docks in Saint Denis",              2787.5f,  -1439.5f,   45.5f, 35.0f, Models(SD_DOCK)),
	Town("Saint Denis Cool Bar",    "The target is a well-dressed gentleman",  "Drinks at the saloon in Saint Denis",         2615.5f,  -1212.5f,   53.5f, 35.0f, Models(SD_RICH_BAR)),
};
static const int kContractCount = (int)(sizeof(kContracts) / sizeof(kContracts[0]));

// ===== [ GIVERS ] =====
static constexpr Hash CLERK_STATION  = Joaat("s_m_m_trainstationworker_01");
static constexpr Hash CLERK_RHODES   = Joaat("u_m_m_rhdtrainstationworker_01");
static constexpr Hash CLERK_STRWBRY  = Joaat("u_m_m_strfreightstationowner_01");
static constexpr Hash CLERK_VANHORN  = Joaat("u_m_m_vhtstationclerk_01");

static const GiverSpot kGivers[] = {
	// Saint Denis
	{ { 2748.79f, -1398.22f,  46.18f }, CLERK_STATION, GIVER_CLERK }, // outside
	{ { 2747.82f, -1396.43f,  46.18f }, CLERK_STATION, GIVER_CLERK }, // inside
	// Rhodes
	{ { 1230.25f, -1298.60f,  76.90f }, CLERK_RHODES,  GIVER_CLERK }, // right
	{ { 1226.71f, -1295.08f,  76.91f }, CLERK_RHODES,  GIVER_CLERK }, // left
	// Blackwater
	{ { -875.03f, -1327.09f,  43.97f }, CLERK_STATION, GIVER_CLERK }, // inside
	{ { -875.02f, -1326.71f,  43.97f }, CLERK_STATION, GIVER_CLERK }, // outside
	// Strawberry
	{ { -1763.84f, -385.08f, 157.73f }, CLERK_STRWBRY, GIVER_CLERK },
	// Valentine
	{ { -175.06f,   631.76f, 114.09f }, CLERK_STATION, GIVER_CLERK }, // left
	{ { -178.00f,   628.11f, 114.09f }, CLERK_STATION, GIVER_CLERK }, // right
	// Riggs
	{ { -1094.0f,  -577.5f,   82.5f  }, CLERK_STATION, GIVER_CLERK }, // right
	{ { -1093.0f,  -575.5f,   82.5f  }, CLERK_STATION, GIVER_CLERK }, // left
	// Emerald
	{ { 1523.5f,    442.5f,   90.5f  }, CLERK_STATION, GIVER_CLERK }, // open
	{ { 1522.0f,    441.0f,   90.5f  }, CLERK_STATION, GIVER_CLERK }, // shelter
	// Van Horn
	{ { 2986.0f,    570.0f,   44.5f  }, CLERK_VANHORN, GIVER_CLERK }, // front
	{ { 2987.0f,    575.0f,   44.5f  }, CLERK_VANHORN, GIVER_CLERK }, // back
	// Annesburg
	{ { 2933.0f,   1282.5f,   44.5f  }, CLERK_STATION, GIVER_CLERK }, // middle
	{ { 2939.0f,   1287.0f,   44.5f  }, CLERK_STATION, GIVER_CLERK }, // left
};

// ===== [ CONTRACT CARD ] =====
// The physical card the clerk hands over: a 4x6 photo card examined through the game's own item-inspect
// task (Zoom / Flip / Put Away). Item, prop, slot and states are the ones Contracts Remastered uses
// (read from its binary); state hashes cross-checked against femga rdr3_discoveries/tasks/TASK_ITEM_INTERACTION.
namespace Card
{
	constexpr Hash kItem        = Joaat("generic_photograph");                // the inventory item whose inspect animations we borrow
	constexpr Hash kPropModel   = Joaat("p_cs_photonudie05x_4x6");            // the photo card prop (its picture is replaced through a render target)
	constexpr Hash kPrimaryItem = Joaat("primaryItem");                       // the held-prop slot every card/book/document state uses
	constexpr Hash kStateIntro       = Joaat("DOCUMENT_INSPECT@Paper_w10-16_H15-24_INTRO");
	constexpr Hash kStateBase        = Joaat("DOCUMENT_INSPECT@Paper_w10-16_H15-24_BASE");
	constexpr Hash kStateFlipToBack  = Joaat("DOCUMENT_INSPECT@Paper_w10-16_H15-24_FLIP_TO_BACK");
	constexpr Hash kStateFlippedBase = Joaat("DOCUMENT_INSPECT@Paper_w10-16_H15-24_FLIPPED_BASE");
	constexpr Hash kStateFlipToFront = Joaat("DOCUMENT_INSPECT@Paper_w10-16_H15-24_FLIP_TO_FRONT");
	constexpr Hash kStateOutro       = Joaat("DOCUMENT_INSPECT@Paper_w10-16_H15-24_OUTRO");
	constexpr Hash kStartState       = kStateIntro;
	constexpr const char* kFlipBlackboard = "GENERIC_DOCUMENT_FLIP_AVAILABLE";  // player-ped blackboard bool that enables the Flip prompt (R* generic_document_inspection)
	constexpr DWORD kTaskStartWaitMs = 700;                                   // wait for the inspect task to report running before trying the fallback path
	constexpr const char* kTitle        = "Contract Information";
	constexpr const char* kTitleLabel   = "BC_CARD_TITLE";                    // GXT label from dist/lml/BountyContracts/strings.gxt2 (used when installed; literal text otherwise)
	constexpr const char* kRenderTarget = "contract_card";
	constexpr const char* kHandBone     = "PH_R_Hand";
	constexpr Hash kCashPickup = Joaat("PICKUP_MONEY_VARIABLE");

	// Alternate recipe (debug key O): the cigarette-card item + states, which flip natively.
	constexpr Hash kCigItem            = Joaat("document_cig_card_grl");
	constexpr Hash kCigIntro           = Joaat("CIGARETTE_CARD_W6-5_H10-7_SINGLE_INTRO");
	constexpr Hash kCigBase            = Joaat("CIGARETTE_CARD_W6-5_H10-7_SINGLE_BASE");
	constexpr Hash kCigFlipToBack      = Joaat("CIGARETTE_CARD_W6-5_H10-7_SINGLE_FLIP_TO_BACK");
	constexpr Hash kCigFlippedBase     = Joaat("CIGARETTE_CARD_W6-5_H10-7_SINGLE_FLIPPED_BASE");
	constexpr Hash kCigFlipToFront     = Joaat("CIGARETTE_CARD_W6-5_H10-7_SINGLE_FLIP_TO_FRONT");

	// --- target portrait (the game's persona-photo / pedshot pipeline) ---
	constexpr const char* kPhotoName  = "MINIGAME_PROFILE_PHOTO";   // one of the three names the MP pedshot flow accepts (MP_PROFILE_PHOTO, MP_MISSION_PHOTO, MINIGAME_PROFILE_PHOTO)
	constexpr const char* kPhotoCustomName = "BC_CONTRACT_TARGET"; // SP flow registers its own name (R* spd_agnesdowd1 uses "SPD_AGNES_DOWD_01")
	constexpr DWORD kPhotoLookupMs    = 1200;                       // per no-write lookup (texture by name / local backup)
	constexpr float kPhotoPedOffsetY  = 2.0f;                       // the ped is parked this far IN FRONT of the player (in view, hidden) while the portrait is taken
	constexpr DWORD kPhotoUploadMs    = 3000;                       // wait for a previous / current upload to finish
	constexpr DWORD kPhotoAvailMs     = 1500;                       // wait for PEDSHOT_IS_AVAILABLE after generating
	constexpr DWORD kPhotoWriteMs     = 2500;                       // poll _NETWORK_PERSONA_PHOTO_WRITE_LOCAL this long (cycling cache types)
	constexpr DWORD kPhotoNameMs      = 2000;                       // poll _REQUEST_PEDSHOT_TEXTURE_LOCAL_BACKUP_DOWNLOAD this long
}
// Hashes verified against femga's documented values.
static_assert(Card::kStateIntro       == 0x19FF3C2A, "card state hash");
static_assert(Card::kStateBase        == 0x7593F5A7, "card state hash");
static_assert(Card::kStateFlipToBack  == 0x0543087B, "card state hash");
static_assert(Card::kStateFlippedBase == 0xB7286003, "card state hash");
static_assert(Card::kStateFlipToFront == 0x0D9687E0, "card state hash");
static_assert(Card::kStateOutro       == 0x13AA268C, "card state hash");
