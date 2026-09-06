/*
	Contracts mod — bounty-style contracts handed out by station clerks.
	Built on Alexander Blade's RDR2 ScriptHook SDK (http://dev-c.com).

	Flow:  NONE --[Get Contract: clerk hands over a photo card]--> UNKNOWN
	       UNKNOWN --[spot / hurt / fight the target]--> FOUND
	       FOUND --[target dies, photograph the corpse]--> DEAD
	       DEAD --[Collect Payment: hand the clerk the photo, cash lands on the counter]--> PAID
	       PAID --[player takes the cash]--> NONE
	       UNKNOWN / FOUND --[End Contract at a giver]--> NONE

	Payout scales with how long the contract ran and drops if the player got wanted after the crime;
	see Tune::kPayout* in contract_data.h. Data (spawns, models, givers, card, tunables) lives there too.
*/

#include "global.h"
#include "keyboard.h"
#include "contract_data.h"

static const char* kBuildTag = "dev-8";   // shown on the HUD and in a banner at startup so an installed build is verifiable

// ===== [ DEBUG TOGGLES ] ===== (overridable from the build: set CL=/DCONTRACTS_DEBUG_HUD=0)
#ifndef CONTRACTS_DEBUG_KEYS
#define CONTRACTS_DEBUG_KEYS 1   // U: skip the clerk and roll a new contract
#endif
#ifndef CONTRACTS_DEBUG_HUD
#define CONTRACTS_DEBUG_HUD  1   // on-screen readout: state / aggro / photo / card / payout
#endif

// ===== [ STATE ] =====
enum ContractState { CONTRACT_NONE, CONTRACT_UNKNOWN, CONTRACT_FOUND, CONTRACT_DEAD, CONTRACT_PAID };
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

	// timeline + law, for the payout
	ULONGLONG  startMs = 0;             // Get Contract
	ULONGLONG  crimeMs = 0;             // first hostile contact between player and target (0 = none yet)
	ULONGLONG  photoMs = 0;             // corpse photographed
	int        bountyAtCrime = 0;
	bool       gotWanted = false;

	// target portrait
	bool        photoTaken = false;
	bool        photoPedWasReady = false;
	int         photoVariant = -1;      // which pipeline variant produced the texture (-1 = none)
	int         photoCacheType = -1;    // cache type a lookup / write succeeded with
	bool        photoGenOk = false, photoRegOk = false, photoSceneOk = false; // return values of GENERATE / _0xFD05 / _0x402E (debug)
	bool        photoAvailBefore = false, photoAvailAfter = false; // PEDSHOT_IS_AVAILABLE before / after generating (debug)
	char        photoTexture[64] = "";  // texture name to draw ("" = none)
	int         photoSlot = -1;         // player-slot value the write was accepted with (debug)
	char        photoProbe1[48] = "";   // printable return of _0x285438C26C732F9D(), if any (debug)
	char        photoProbe2[48] = "";   // printable return of _0x5C9C3A466B3296A8(0), if any (debug)
	const char* photoStatus = "-";      // how the texture was obtained, or the last step that failed (debug HUD)
	ULONGLONG   cardOpenAtMs = 0;       // deferred card examine (after the handoff anim)

	// hand-in
	int        payoutCents = 0;
	Ped        payingGiver = 0;
	ULONGLONG  handInStartMs = 0;
	bool       cashSpawned = false;
	Object     cashObj = 0;
};

// The card prop while it is out (being examined, or in hand during the hand-in).
struct CardRuntime
{
	Object      obj = 0;
	bool        ownsObj = false;     // we created it (delete on close); false = the game's own item prop
	bool        examining = false;
	bool        inHand = false;
	bool        cig = false;         // opened with the cigarette-card recipe (debug key O)
	ULONGLONG   openedMs = 0;
	int         renderId = 0;
	const char* rtName = "-";        // render-target name that actually linked to the card model (debug HUD)
	bool        customApplied = false; // SET_CUSTOM_TEXTURES_ON_OBJECT tried on this object
	int         path = 0;            // 0 none, 1 our photo card via _TASK_ITEM_INTERACTION_2, 2 game-spawned card via START_TASK_ITEM_INTERACTION
	const char* lastError = "";      // why the last OpenCard() failed (debug HUD)
};

static ActiveContract  C;
static CardRuntime     Cd;
static ContractState   g_state = CONTRACT_NONE;

static Player  me = 0;
static Ped     pedMe = 0;
static Vector3 playerPos;

static Prompt giverPrompt = 0;
static Prompt camPrompt = 0;
static Hash   camGroup = 0;

static const int kDefaultRenderId = 1;   // the script's normal (screen) render target

// ===== [ SMALL HELPERS ] =====
static float DistSq(const Vector3& a, const Vector3& b)
{
	float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
	return dx * dx + dy * dy + dz * dz;
}
static bool Within(const Vector3& a, const Vector3& b, float dist) { return DistSq(a, b) <= dist * dist; }

static bool TargetExists() { return C.target && ENTITY::DOES_ENTITY_EXIST(C.target); }
static bool ContractActive() { return g_state == CONTRACT_UNKNOWN || g_state == CONTRACT_FOUND || g_state == CONTRACT_DEAD; }

static const char* Literal(const char* text) { return MISC::VAR_STRING(10, "LITERAL_STRING", text); }

static void DisplaySubtitle(const char* message) { DisplayObjective(message); }

static void FormatMoney(char* out, size_t size, int cents) { sprintf_s(out, size, "$%d.%02d", cents / 100, cents % 100); }

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

// Yields frames until pred() holds or timeoutMs passes; bounded so a bad name can't hang the script.
template<typename Pred> static bool WaitUntil(DWORD timeoutMs, Pred pred)
{
	ULONGLONG deadline = GetTickCount64() + timeoutMs;
	while (!pred() && GetTickCount64() < deadline) WAIT(0);
	return pred();
}

static void PlayAnimOnPed(Ped ped, const char* dict, const char* name, float blendIn, float blendOut, int duration, int flags)
{
	STREAMING::REQUEST_ANIM_DICT(dict);
	if (!WaitUntil(Tune::kStreamTimeoutMs, [&] { return STREAMING::HAS_ANIM_DICT_LOADED(dict) != 0; })) return;
	TASK::CLEAR_PED_SECONDARY_TASK(ped);
	TASK::TASK_PLAY_ANIM(ped, dict, name, blendIn, blendOut, duration, flags, 0.0f, 0,
		AIK_DISABLE_ARM_IK | AIK_DISABLE_TORSO_REACT_IK | AIK_DISABLE_TORSO_IK | AIK_DISABLE_HEAD_IK | AIK_DISABLE_LEG_IK, 0, 0, 0);
	STREAMING::REMOVE_ANIM_DICT(dict);
}

static bool LoadModel(Hash model)
{
	STREAMING::REQUEST_MODEL(model, true);
	if (WaitUntil(Tune::kStreamTimeoutMs, [&] { return STREAMING::HAS_MODEL_LOADED(model) != 0; })) return true;
	STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
	return false;
}

static Ped SpawnPed(Hash model, const Vector3& pos)
{
	if (!LoadModel(model)) return 0;
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

// ===== [ TARGET PORTRAIT ] =====
// The game's own ped-portrait pipeline (the Online persona photo): generate -> write to the local
// persona-photo cache -> wait for the upload -> ask the cache for the texture name. Rockstar's MP script
// (persona_photos) and SP script (spd_agnesdowd1) drive it slightly differently, so the variants below are
// tried in order until one yields a texture; the HUD reports which.
// The MP persona_photos flow, replicated exactly: generate, then call _NETWORK_PERSONA_PHOTO_WRITE_LOCAL
// every frame WITH THE SAME ARGUMENTS until it returns true (it is a multi-frame operation — alternating
// the arguments per frame, as dev-4 did, restarts it), wait for the upload, ask the cache for the name.
// Variants differ only in the cache type the write targets.
// Generation works in single player (dev-6: gen=1, shot held). What does not is the Social Club-backed
// _NETWORK_PERSONA_PHOTO_WRITE_LOCAL. Variants try the other persona-photo types with the network write,
// and _0xA1A86055792FB249(cacheType) — a pedshot native that takes a local cache type and that no R* script
// uses, i.e. the plausible single-player "write to local cache". Whatever happens, the cache's texture name
// for that slot is taken so the card and the back panel can show whether it holds the new portrait.
struct PhotoVariant { int type; int cacheType; };
static const PhotoVariant kPhotoVariants[] = { { 1, 2 }, { 1, 0 }, { 2, 2 } };
static const int kPhotoSlots[] = { 0, 1, 2 };   // the write's playerSlot argument (Remastered logs a "slot" it picks)

// The persona-photo cache's texture name for the local player and cache type, copied out immediately —
// the native returns a pointer into a transient game buffer that later natives overwrite (dev-7 stored
// the pointer and read garbage). False when none.
static bool LookupPhotoTexture(int cacheType, char (&out)[64])
{
	const char* n = NETWORK::_REQUEST_PEDSHOT_TEXTURE_LOCAL_BACKUP_DOWNLOAD((int)me, cacheType);
	if (!n || !*n) return false;
	strcpy_s(out, n);
	return true;
}

// Copies a native's `Any` return into `out` if it looks like a readable printable string. Diagnostic only.
static void CopyIfString(Any value, char (&out)[48])
{
	out[0] = 0;
	if (value < 0x10000 || value > 0x7FFFFFFFFFFFull) return;
	const char* p = (const char*)value;
	if (IsBadReadPtr(p, 8)) return;
	for (int i = 0; i < 8; ++i) if (p[i] && (p[i] < 32 || p[i] > 126)) return;
	if (!p[0]) return;
	strncpy_s(out, p, sizeof(out) - 1);
}

static bool TakeTargetPhoto(Ped ped)
{
	C.photoStatus = "ped not ready";
	C.photoPedWasReady = WaitUntil(Tune::kPedshotReadyMs, [&] { return PED::IS_PED_READY_TO_RENDER(ped) != 0; });
	char fallbackName[64] = "";
	int  fallbackType = -1;

	for (int v = 0; v < (int)(sizeof(kPhotoVariants) / sizeof(kPhotoVariants[0])); ++v)
	{
		const int type = kPhotoVariants[v].type;
		const int ct   = kPhotoVariants[v].cacheType;
		C.photoCacheType = ct;

		C.photoStatus = "prev upload pending";
		WaitUntil(Card::kPhotoUploadMs, [] { return !NETWORK::_NETWORK_IS_PREVIOUS_UPLOAD_PENDING(); });
		GRAPHICS::_PEDSHOT_INIT_CLEANUP_DATA();
		GRAPHICS::_PEDSHOT_FINISH_CLEANUP_DATA();
		C.photoAvailBefore = GRAPHICS::PEDSHOT_IS_AVAILABLE() != 0;

		GRAPHICS::_PEDSHOT_PREVIOUS_PERSONA_PHOTO_DATA_CLEANUP();
		GRAPHICS::_PEDSHOT_SET_PERSONA_PHOTO_TYPE(type);
		C.photoGenOk = GRAPHICS::_PEDSHOT_GENERATE_PERSONA_PHOTO(Card::kPhotoName, ped, 0) != 0;
		PED::FORCE_PED_MOTION_STATE(ped, joaat("MotionState_DoNothing"), false, 0, false);
		C.photoTaken = true;

		C.photoStatus = "waiting for shot";
		C.photoAvailAfter = WaitUntil(Card::kPhotoAvailMs, [] { return GRAPHICS::PEDSHOT_IS_AVAILABLE() != 0; });

		// Single-player local-cache write (unverified native), then the network write, per player slot.
		GRAPHICS::_0xA1A86055792FB249(ct);
		C.photoStatus = "write failed";
		bool written = false;
		for (int slot : kPhotoSlots)
		{
			written = WaitUntil(Card::kPhotoWriteMs / 2, [&]
			{
				PED::FORCE_PED_MOTION_STATE(ped, joaat("MotionState_DoNothing"), false, 0, false);
				return NETWORK::_NETWORK_PERSONA_PHOTO_WRITE_LOCAL(Card::kPhotoName, slot, 1, ct) != 0;
			});
			if (written) { C.photoSlot = slot; break; }
		}
		if (written) WaitUntil(Card::kPhotoUploadMs, [] { return !NETWORK::_NETWORK_IS_PREVIOUS_UPLOAD_PENDING(); });

		char name[64] = "";
		WaitUntil(written ? Card::kPhotoNameMs : 500, [&] { return LookupPhotoTexture(ct, name); });
		if (written && name[0])
		{
			strcpy_s(C.photoTexture, name);
			C.photoVariant = v;
			C.photoStatus = "ok";
			return true;
		}
		if (name[0] && !fallbackName[0]) { strcpy_s(fallbackName, name); fallbackType = v; }
		C.photoStatus = written ? "written, no name" : "write failed";
	}

	// Diagnostic: two pedshot natives R*'s SP script calls with discarded returns — one may be a texture getter.
	CopyIfString(GRAPHICS::_0x285438C26C732F9D(), C.photoProbe1);
	CopyIfString(GRAPHICS::_0x5C9C3A466B3296A8(0), C.photoProbe2);

	// No confirmed write. Use the cache slot's name anyway: after _0xA1A8... it may hold the new portrait,
	// and the back panel / custom texture will show whether it does.
	if (fallbackName[0])
	{
		strcpy_s(C.photoTexture, fallbackName);
		C.photoVariant = fallbackType;
		C.photoStatus = "unverified slot";
	}
	return false;
}
static void ReleaseTargetPhoto()
{
	if (!C.photoTaken) return;
	if (C.photoTexture[0]) NETWORK::_TEXTURE_DOWNLOAD_RELEASE_BY_NAME(C.photoTexture);
	GRAPHICS::_PEDSHOT_INIT_CLEANUP_DATA();
	GRAPHICS::_PEDSHOT_FINISH_CLEANUP_DATA();
}
static bool TargetPhotoReady() { return C.photoTexture[0] != 0; }

// Spawns the target in front of the player first — hidden, frozen, no collision, exactly how the persona-
// photo script parks its clone — so his clothes and textures stream in and the portrait can be taken.
// Then moves him to his town.
static Ped SpawnTargetWithPhoto(Hash model, const ContractDef& def)
{
	Ped ped = SpawnPed(model, ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(pedMe, 0.0f, Card::kPhotoPedOffsetY, 0.0f));
	if (!ped) return 0;
	ENTITY::SET_ENTITY_AS_MISSION_ENTITY(ped, true, true);
	ENTITY::FREEZE_ENTITY_POSITION(ped, true);
	ENTITY::SET_ENTITY_COLLISION(ped, false, false);
	ENTITY::SET_ENTITY_HEADING(ped, ENTITY::GET_ENTITY_HEADING(pedMe) + 180.0f); // facing the player / camera
	PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(ped, false);
	if (Tune::kPedshotHidden) ENTITY::SET_ENTITY_VISIBLE(ped, false);
	TASK::CLEAR_PED_TASKS_IMMEDIATELY(ped, false, true);

	TakeTargetPhoto(ped);

	if (Tune::kPedshotHidden) ENTITY::SET_ENTITY_VISIBLE(ped, true);
	ENTITY::SET_ENTITY_COLLISION(ped, true, false);
	ENTITY::FREEZE_ENTITY_POSITION(ped, false);
	ENTITY::SET_ENTITY_COORDS(ped, def.spawn.x, def.spawn.y, def.spawn.z, false, false, false, true);
	ENTITY::PLACE_ENTITY_ON_GROUND_PROPERLY(ped, 1);
	return ped;
}

// ===== [ CONTRACT CARD ] =====
static int g_cardLastPath = 0;   // which OpenCard() path last succeeded (debug HUD)

// Register + link the way R*'s photo studio does: a name only counts if IS_NAMED_RENDERTARGET_LINKED
// confirms the model took it; otherwise release it and try the next candidate.
static void LinkCardRenderTarget(Hash cardModel)
{
	if (!Tune::kCardFaceRenderTarget) return;
	for (const char* name : Card::kRenderTargetNames)
	{
		if (!HUD::IS_NAMED_RENDERTARGET_REGISTERED(name)) HUD::REGISTER_NAMED_RENDERTARGET(name, false);
		HUD::LINK_NAMED_RENDERTARGET(cardModel);
		if (HUD::IS_NAMED_RENDERTARGET_LINKED(cardModel))
		{
			Cd.renderId = HUD::GET_NAMED_RENDERTARGET_RENDER_ID(name);
			Cd.rtName = name;
			return;
		}
		if (HUD::IS_NAMED_RENDERTARGET_REGISTERED(name)) HUD::RELEASE_NAMED_RENDERTARGET(name);
	}
	Cd.renderId = 0;
	Cd.rtName = "none linked";
}

// The name shown under the inspect prompts. A GXT label (installed through the dist/lml pack, the way
// Contracts Remastered does it) when present; literal text otherwise.
static void SetCardTitle(Object obj)
{
	if (HUD::DOES_TEXT_LABEL_EXIST(Card::kTitleLabel))
		OBJECT::_SET_OBJECT_PROMPT_NAME_FROM_GXT_ENTRY(obj, joaat(Card::kTitleLabel));
	else
		OBJECT::_SET_OBJECT_PROMPT_NAME(obj, Literal(Card::kTitle));
}

static bool CreateCardObject()
{
	if (Cd.obj) return true;
	if (!LoadModel(Card::kPropModel)) return false;
	Cd.obj = OBJECT::CREATE_OBJECT(Card::kPropModel, ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(pedMe, 0.0f, 0.5f, 0.0f), true, true, true, false, false);
	STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(Card::kPropModel);
	if (!ENTITY::DOES_ENTITY_EXIST(Cd.obj)) { Cd.obj = 0; return false; }
	Cd.ownsObj = true;
	SetCardTitle(Cd.obj);
	LinkCardRenderTarget(Card::kPropModel);
	return true;
}

static void DestroyCardObject()
{
	if (Cd.obj && Cd.ownsObj && ENTITY::DOES_ENTITY_EXIST(Cd.obj))
	{
		if (ENTITY::IS_ENTITY_ATTACHED(Cd.obj)) ENTITY::DETACH_ENTITY(Cd.obj, true, false);
		OBJECT::DELETE_OBJECT(&Cd.obj);
	}
	const char* keepError = Cd.lastError;
	Cd = CardRuntime();
	Cd.lastError = keepError;
}

static bool CardTaskRunning() { return TASK::IS_PED_RUNNING_TASK_ITEM_INTERACTION(pedMe) != 0; }

// Player takes the card out and examines it (Zoom / Flip / Put Away are the game's own prompts).
//  cig=false: the Remastered recipe — item generic_photograph, prop p_cs_photonudie05x_4x6, slot primaryItem,
//             paper-inspect states; Flip comes from the GENERIC_DOCUMENT_FLIP_AVAILABLE blackboard flag.
//  cig=true : the cigarette-card item + states on the same prop (flips natively). Debug key O.
//  Path 1 holds our own card through _TASK_ITEM_INTERACTION_2; if that task refuses to start, path 2 lets the
//  game spawn the item's own prop (how R* document scripts do it) and takes that prop over.
static bool OpenCard(bool cig)
{
	if (Cd.obj || Cd.examining) return false;
	Cd.lastError = "";
	Hash item  = cig ? Card::kCigItem  : Card::kItem;
	Hash state = cig ? Card::kCigIntro : Card::kStartState;
	PED::_SET_PED_BLACKBOARD_BOOL(pedMe, Card::kFlipBlackboard, true, -1);

	if (CreateCardObject())
	{
		// natives.h calls the first parameter propNameGxt: when our title label is installed, try it there
		// first (Remastered ships a label and an item name), then the item hash.
		Hash firstParam[2] = { item, item };
		int  tries = 1;
		if (!cig && HUD::DOES_TEXT_LABEL_EXIST(Card::kTitleLabel)) { firstParam[0] = joaat(Card::kTitleLabel); tries = 2; }
		for (int t = 0; t < tries; ++t)
		{
			TASK::_TASK_ITEM_INTERACTION_2(pedMe, firstParam[t], Cd.obj, Card::kPrimaryItem, state, 1, 0, -1.0f);
			if (WaitUntil(Card::kTaskStartWaitMs, CardTaskRunning))
			{
				Cd.examining = true;
				Cd.cig = cig;
				Cd.openedMs = GetTickCount64();
				Cd.path = g_cardLastPath = (t == 0 && tries == 2) ? 3 : 1;   // 3 = label as first param
				return true;
			}
		}
		DestroyCardObject();
		Cd.lastError = "p1: task did not start";
	}
	else
	{
		Cd.lastError = "p1: card object failed";
	}

	TASK::START_TASK_ITEM_INTERACTION(pedMe, item, state, 1, 0, -1.0f);
	if (!WaitUntil(Card::kTaskStartWaitMs, CardTaskRunning))
	{
		Cd.lastError = "p1+p2: task did not start";
		return false;
	}
	Entity held = 0;
	WaitUntil(500, [&] { held = TASK::_GET_ITEM_INTERACTION_ENTITY_FROM_PED(pedMe, Card::kPrimaryItem); return ENTITY::DOES_ENTITY_EXIST(held) != 0; });
	if (ENTITY::DOES_ENTITY_EXIST(held))
	{
		Cd.obj = (Object)held;   // the game's prop — it deletes it when the task ends
		Cd.ownsObj = false;
		SetCardTitle(Cd.obj);
		LinkCardRenderTarget(ENTITY::GET_ENTITY_MODEL(Cd.obj));
	}
	else
	{
		Cd.lastError = "p2: running, no primaryItem prop";
	}
	Cd.examining = true;
	Cd.cig = cig;
	Cd.openedMs = GetTickCount64();
	Cd.path = g_cardLastPath = 2;
	return true;
}

// Card in the player's hand for the hand-in animation (no examine UI).
static void AttachCardToHand()
{
	if (!CreateCardObject()) return;
	int bone = ENTITY::GET_ENTITY_BONE_INDEX_BY_NAME(pedMe, Card::kHandBone);
	if (bone < 0) { DestroyCardObject(); return; }
	ENTITY::ATTACH_ENTITY_TO_ENTITY(Cd.obj, pedMe, bone, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, false, false, false, true, 0, true, false, false);
	Cd.inHand = true;
}

// The target's portrait, drawn onto the card prop through its render target, the way R*'s photo studio
// draws onto its catalogue: select the target, clear it, select it again, draw. R* never "restores" the
// render id afterwards (RDR2 has no default-id getter and resetting to 1 hijacked every later screen draw),
// so this must be the LAST thing drawn in the frame. Corpse photo = the same portrait in a dead, faded tint.
static void DrawCardFace(bool corpse)
{
	if (!Cd.renderId || !TargetPhotoReady()) return;
	HUD::SET_TEXT_RENDER_ID(Cd.renderId);
	HUD::_0x9D37EB5003E0F2CF(Cd.renderId, 1);
	GRAPHICS::DRAW_RECT(0.5f, 0.5f, 2.0f, 2.0f, 0, 0, 0, 255, false, true);
	HUD::SET_TEXT_RENDER_ID(Cd.renderId);
	if (corpse) GRAPHICS::DRAW_SPRITE(C.photoTexture, C.photoTexture, 0.5f, 0.5f, 1.0f, 1.0f, 0.0f, 150, 105, 95, 255, false);
	else        GRAPHICS::DRAW_SPRITE(C.photoTexture, C.photoTexture, 0.5f, 0.5f, 1.0f, 1.0f, 0.0f, 255, 255, 255, 255, false);
}

// Second route onto the card: the object-level custom texture R* uses to put letter textures on paper props.
static void ApplyCardCustomTexture()
{
	if (!Card::kCardCustomTexture || Cd.customApplied || !Cd.obj || !TargetPhotoReady()) return;
	OBJECT::SET_CUSTOM_TEXTURES_ON_OBJECT(Cd.obj, joaat(C.photoTexture), 0, 0);
	Cd.customApplied = true;
}

// The "back" of the card: a screen-space panel with the portrait, who the target is, where he is, and the pay.
static void DrawCardBackPanel()
{
	if (!C.def) return;
	GRAPHICS::DRAW_RECT(0.50f, 0.50f, 0.40f, 0.34f, 18, 14, 11, 225, false, false);
	if (TargetPhotoReady())
		GRAPHICS::DRAW_SPRITE(C.photoTexture, C.photoTexture, 0.395f, 0.50f, 0.15f, 0.27f, 0.0f, 255, 255, 255, 255, false);
	// Diagnostic thumbnails along the panel's bottom edge: the persona-photo name drawn directly, and the
	// two probed strings. Whichever shows a face is the texture we want.
	if (C.photoTaken)      GRAPHICS::DRAW_SPRITE(Card::kPhotoName, Card::kPhotoName, 0.33f, 0.63f, 0.05f, 0.09f, 0.0f, 255, 255, 255, 255, false);
	if (C.photoProbe1[0])  GRAPHICS::DRAW_SPRITE(C.photoProbe1, C.photoProbe1, 0.39f, 0.63f, 0.05f, 0.09f, 0.0f, 255, 255, 255, 255, false);
	if (C.photoProbe2[0])  GRAPHICS::DRAW_SPRITE(C.photoProbe2, C.photoProbe2, 0.45f, 0.63f, 0.05f, 0.09f, 0.0f, 255, 255, 255, 255, false);

	char lo[16], hi[16], reward[48];
	FormatMoney(lo, sizeof lo, Tune::kPayoutMinCents);
	FormatMoney(hi, sizeof hi, Tune::kPayoutMaxCents);
	sprintf_s(reward, "%s - %s", lo, hi);
	DrawTextToScreen(C.def->targetDesc, 0.49f, 0.37f, 0.36f, 255, 255, 255, 255);
	DrawTextToScreen(C.def->hint,       0.49f, 0.43f, 0.36f, 255, 255, 255, 255);
	DrawTextToScreen(reward,            0.52f, 0.57f, 0.72f, 255, 255, 255, 255);
}

static bool CardIsFlipped(Hash state)
{
	return state == Card::kStateFlipToBack || state == Card::kStateFlippedBase ||
	       state == Card::kCigFlipToBack   || state == Card::kCigFlippedBase;
}

static void UpdateCard()
{
	ULONGLONG now = GetTickCount64();
	if (C.cardOpenAtMs && now >= C.cardOpenAtMs)
	{
		C.cardOpenAtMs = 0;
		OpenCard(false);
	}
	if (Cd.examining)
	{
		if (!CardTaskRunning() && now > Cd.openedMs + 1500) { DestroyCardObject(); return; } // put away (or the task ended)
		PED::_SET_PED_BLACKBOARD_BOOL(pedMe, Card::kFlipBlackboard, true, -1); // the inspect task reads this while it runs
		if (Cd.obj) SetCardTitle(Cd.obj);                                     // the task resets the prop's name on start
		ApplyCardCustomTexture();
		if (CardIsFlipped(TASK::GET_ITEM_INTERACTION_STATE(pedMe))) DrawCardBackPanel();
		DrawCardFace(false);   // last: it leaves the render target selected
	}
	else if (Cd.inHand)
	{
		ApplyCardCustomTexture();
		DrawCardFace(true);
	}
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

// ===== [ PAYOUT ] =====
// Longer contracts pay more (linear up to kFullPayMinutes); getting wanted after the crime cuts the pay.
static int ComputePayoutCents()
{
	ULONGLONG end = C.photoMs ? C.photoMs : GetTickCount64();
	float minutes = (float)(end - C.startMs) / 60000.0f;
	float t = minutes / Tune::kFullPayMinutes;
	if (t < 0.0f) t = 0.0f;
	if (t > 1.0f) t = 1.0f;
	float pay = Tune::kPayoutMinCents + (Tune::kPayoutMaxCents - Tune::kPayoutMinCents) * t;
	if (C.gotWanted) pay *= Tune::kWantedPayoutMult;
	int cents = ((int)pay / Tune::kPayoutStepCents) * Tune::kPayoutStepCents;
	if (cents < Tune::kPayoutMinCents) cents = Tune::kPayoutMinCents;
	if (cents > Tune::kPayoutMaxCents) cents = Tune::kPayoutMaxCents;
	return cents;
}

// "The crime" starts at the first hostile contact between the player and the target; from then on any
// law incident, wanted score or bounty increase counts as getting wanted.
static void UpdateCrimeTracking()
{
	if (!TargetExists()) return;
	if (!C.crimeMs)
	{
		if (PED::IS_PED_IN_COMBAT(C.target, pedMe) || ENTITY::HAS_ENTITY_BEEN_DAMAGED_BY_ENTITY(C.target, pedMe, true, true))
		{
			C.crimeMs = GetTickCount64();
			C.bountyAtCrime = LAW::GET_BOUNTY(me);
		}
		return;
	}
	if (!C.gotWanted &&
		(LAW::IS_LAW_INCIDENT_ACTIVE(me) || LAW::GET_WANTED_SCORE(me) > 0 || LAW::GET_BOUNTY(me) > C.bountyAtCrime))
	{
		C.gotWanted = true;
	}
}

// ===== [ CONTRACT LIFECYCLE ] =====
static void ClearContract(bool deleteTarget)
{
	RemoveBlip(C.searchBlip);
	RemoveBlip(C.targetBlip);
	PLAYER::_CLEAR_PED_EAGLE_EYE_TRAILS_FOR_PLAYER(me);
	PLAYER::_UNREGISTER_EAGLE_EYE_FOR_ENTITY(me, C.target);
	DestroyCardObject();
	ReleaseTargetPhoto();
	if (C.cashObj && ENTITY::DOES_ENTITY_EXIST(C.cashObj)) OBJECT::DELETE_OBJECT(&C.cashObj);
	if (C.def && C.def->onCleanup) C.def->onCleanup();
	if (deleteTarget && TargetExists()) PED::DELETE_PED(&C.target);
	ENTITY::SET_ENTITY_AS_NO_LONGER_NEEDED(&C.target);
	C = ActiveContract();
	ResetPrompt(giverPrompt);
	ResetPrompt(camPrompt);
	g_state = CONTRACT_NONE;
}

// Rolls a random contract, photographs and spawns its target. False if nothing could be spawned.
static bool StartContract()
{
	ClearContract(true);
	for (int attempt = 0; attempt < Tune::kSpawnAttempts; ++attempt)
	{
		const ContractDef& def = kContracts[rand() % kContractCount];
		Hash model = def.models.list[rand() % def.models.count];
		Ped ped = SpawnTargetWithPhoto(model, def);
		if (!ped) continue;

		C.def = &def;
		C.target = ped;
		C.targetPos = def.spawn;
		C.startMs = GetTickCount64();
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
		C.photoMs = GetTickCount64();
		g_state = CONTRACT_DEAD;
	}
}

// ===== [ HAND-IN: PHOTO FOR CASH ] =====
static void UpdatePayment()
{
	ULONGLONG now = GetTickCount64();

	if (Cd.inHand && now >= C.handInStartMs + Tune::kHandInCardMs) DestroyCardObject();

	if (!C.cashSpawned && now >= C.handInStartMs + Tune::kCashSpawnDelayMs)
	{
		C.cashSpawned = true;
		Vector3 giver = ENTITY::DOES_ENTITY_EXIST(C.payingGiver) ? ENTITY::GET_ENTITY_COORDS(C.payingGiver, true, false) : playerPos;
		Vector3 counter((giver.x + playerPos.x) * 0.5f, (giver.y + playerPos.y) * 0.5f, giver.z + Tune::kCounterHeight);
		C.cashObj = OBJECT::CREATE_AMBIENT_PICKUP(Card::kCashPickup, counter, 0, C.payoutCents, 0, true, true, 0, 0.0f);

		char money[16], msg[64];
		FormatMoney(money, sizeof money, C.payoutCents);
		if (!C.cashObj || !ENTITY::DOES_ENTITY_EXIST(C.cashObj))
		{
			// No pickup could be placed — pay directly rather than short the player.
			MONEY::_MONEY_INCREMENT_CASH_BALANCE(C.payoutCents, 0);
			sprintf_s(msg, "REWARD RECEIVED: %s", money);
			DisplaySubtitle(msg);
			ClearContract(false);
			return;
		}
		sprintf_s(msg, "TAKE YOUR PAYMENT: %s", money);
		DisplaySubtitle(msg);
		return;
	}

	if (!C.cashSpawned) return;
	bool taken    = !ENTITY::DOES_ENTITY_EXIST(C.cashObj);
	bool timedOut = now > C.handInStartMs + Tune::kCashSpawnDelayMs + Tune::kCashTimeoutMs;
	if (taken)
	{
		C.cashObj = 0;
		DisplaySubtitle("REWARD RECEIVED");
		ClearContract(false);
	}
	else if (timedOut)
	{
		MONEY::_MONEY_INCREMENT_CASH_BALANCE(C.payoutCents, 0); // left on the counter too long: pay it out anyway
		ClearContract(false);
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
	{
		ULONGLONG handoffMs = GetTickCount64();
		PlayGiverHandoff(giver, false);
		if (StartContract())
		{
			// The clerk is handing the card over; the player examines it once the handoff anim has played.
			ULONGLONG now = GetTickCount64();
			ULONGLONG at  = handoffMs + Tune::kCardOpenDelayMs;
			C.cardOpenAtMs = at > now ? at : now;
			DisplaySubtitle("FIND THE TARGET");
		}
		else
		{
			DisplaySubtitle("NO CONTRACTS AVAILABLE");
		}
		break;
	}

	case CONTRACT_UNKNOWN:
	case CONTRACT_FOUND:
		DisplaySubtitle("CONTRACT ENDED");
		ClearContract(true);
		break;

	case CONTRACT_DEAD:
		// Hand the clerk the corpse photo; he puts the money on the counter (UpdatePayment).
		C.payoutCents   = ComputePayoutCents();
		C.payingGiver   = giver;
		C.handInStartMs = GetTickCount64();
		PlayGiverHandoff(giver, true);
		AttachCardToHand();
		ShowPrompt(giverPrompt, false);
		g_state = CONTRACT_PAID;
		break;

	case CONTRACT_PAID:
		break;
	}
}

// ===== [ DEBUG ] =====
#if CONTRACTS_DEBUG_HUD
static const char* CardStateName(Hash h)
{
	if (h == Card::kStateIntro       || h == Card::kCigIntro)       return "INTRO";
	if (h == Card::kStateBase        || h == Card::kCigBase)        return "BASE";
	if (h == Card::kStateFlipToBack  || h == Card::kCigFlipToBack)  return "FLIP_TO_BACK";
	if (h == Card::kStateFlippedBase || h == Card::kCigFlippedBase) return "FLIPPED";
	if (h == Card::kStateFlipToFront || h == Card::kCigFlipToFront) return "FLIP_TO_FRONT";
	if (h == Card::kStateOutro)       return "OUTRO";
	return h ? "other" : "-";
}
#endif

static void DebugUpdate()
{
#if CONTRACTS_DEBUG_KEYS
	if (IsKeyJustUp(0x55)) // U: skip the clerk and roll a fresh contract, card in hand right away
	{
		if (StartContract()) { C.cardOpenAtMs = GetTickCount64(); DisplaySubtitle("FIND THE TARGET"); }
		else                 DisplaySubtitle("NO CONTRACTS AVAILABLE");
	}
#endif
#if CONTRACTS_DEBUG_HUD
	// state 0=NONE 1=UNKNOWN 2=FOUND 3=DEAD 4=PAID   task 0=WANDER 1=AGGRO
	bool  have   = TargetExists();
	int   losRaw = have ? (ENTITY::HAS_ENTITY_CLEAR_LOS_TO_ENTITY(C.target, pedMe, 17) ? 1 : 0) : -1;
	int   sees   = have ? (TargetCanSeePlayer() ? 1 : 0) : -1;
	float dist   = have ? sqrtf(DistSq(playerPos, C.targetPos)) : -1.0f;
	char line[260], money[16];
	sprintf_s(line, "build=%s state=%d task=%d remember=%d losRaw=%d sees=%d dist=%.1f",
		kBuildTag, (int)g_state, (int)C.task, C.remembersPlayer ? 1 : 0, losRaw, sees, dist);
	DrawTextToScreen(line, 0.05f, 0.08f, 0.4f, 255, 255, 0, 255);

	// Network / ROS natives are only queried once a contract exists — never during the loading screen,
	// before those subsystems are up (dev-5 called them from frame one and the game froze while loading).
	int online = -1, ros = -1, cloud = -1;
	if (C.startMs)
	{
		online = NETWORK::NETWORK_IS_SIGNED_ONLINE() ? 1 : 0;
		ros    = NETWORK::NETWORK_HAS_VALID_ROS_CREDENTIALS() ? 1 : 0;
		cloud  = NETWORK::NETWORK_IS_CLOUD_AVAILABLE() ? 1 : 0;
	}
	sprintf_s(line, "photo: pedReady=%d gen=%d avail=%d/%d online=%d ros=%d cloud=%d var=%d cache=%d slot=%d how=%s tex=%s p1=%s p2=%s",
		C.photoPedWasReady ? 1 : 0, C.photoGenOk ? 1 : 0, C.photoAvailBefore ? 1 : 0, C.photoAvailAfter ? 1 : 0,
		online, ros, cloud,
		C.photoVariant, C.photoCacheType, C.photoSlot, C.photoStatus, C.photoTexture[0] ? C.photoTexture : "-",
		C.photoProbe1[0] ? C.photoProbe1 : "-", C.photoProbe2[0] ? C.photoProbe2 : "-");
	DrawTextToScreen(line, 0.05f, 0.11f, 0.4f, 255, 255, 0, 255);

	Hash cardState = TASK::GET_ITEM_INTERACTION_STATE(pedMe);
	sprintf_s(line, "card: obj=%d exam=%d cig=%d task=%d st=%s rt=%s/%d custom=%d path=%d/%d item=%d/%d label=%d err=%s",
		Cd.obj ? 1 : 0, Cd.examining ? 1 : 0, Cd.cig ? 1 : 0,
		CardTaskRunning() ? 1 : 0, CardStateName(cardState), Cd.rtName, Cd.renderId, Cd.customApplied ? 1 : 0, Cd.path, g_cardLastPath,
		ITEMDATABASE::_ITEMDATABASE_IS_KEY_VALID(Card::kItem, 0) ? 1 : 0, ITEMDATABASE::_ITEMDATABASE_IS_KEY_VALID(Card::kCigItem, 0) ? 1 : 0,
		HUD::DOES_TEXT_LABEL_EXIST(Card::kTitleLabel) ? 1 : 0, Cd.lastError);
	DrawTextToScreen(line, 0.05f, 0.14f, 0.4f, 255, 255, 0, 255);

	float minutes = C.startMs ? (float)((C.photoMs ? C.photoMs : GetTickCount64()) - C.startMs) / 60000.0f : 0.0f;
	FormatMoney(money, sizeof money, C.startMs ? ComputePayoutCents() : 0);
	sprintf_s(line, "pay: minutes=%.1f crime=%d wanted=%d lawActive=%d score=%d bounty=%d est=%s cash=%d",
		minutes, C.crimeMs ? 1 : 0, C.gotWanted ? 1 : 0, LAW::IS_LAW_INCIDENT_ACTIVE(me) ? 1 : 0,
		LAW::GET_WANTED_SCORE(me), LAW::GET_BOUNTY(me), money, C.cashObj ? 1 : 0);
	DrawTextToScreen(line, 0.05f, 0.17f, 0.4f, 255, 255, 0, 255);
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
	ULONGLONG bannerAtMs = GetTickCount64() + 4000;   // one-time "this build is loaded" banner once the HUD is up
	while (true)
	{
		UpdatePlayer();
		DebugUpdate();

		if (bannerAtMs && GetTickCount64() >= bannerAtMs)
		{
			bannerAtMs = 0;
			char banner[64];
			sprintf_s(banner, "CONTRACTS BUILD %s LOADED", kBuildTag);
			DisplaySubtitle(banner);
		}

		// A contract whose target vanished (deleted by another script, fell out of the world) can never
		// be completed — end it instead of leaving stale blips on the map.
		if ((g_state == CONTRACT_UNKNOWN || g_state == CONTRACT_FOUND) && !TargetExists())
		{
			DisplaySubtitle("TARGET LOST");
			ClearContract(false);
		}

		// Look at the contract card again (I = photograph recipe, O = cigarette-card recipe for comparison).
		if (ContractActive() && !Cd.obj && !C.cardOpenAtMs)
		{
			if (IsKeyJustUp(Tune::kInspectCardKey))         OpenCard(false);
			else if (IsKeyJustUp(Tune::kInspectCardAltKey)) OpenCard(true);
		}
		UpdateCard();

		switch (g_state)
		{
		case CONTRACT_NONE:
			UpdateGiverPrompt();
			break;
		case CONTRACT_UNKNOWN:
			UpdateGiverPrompt();
			UpdateTrails();
			UpdateTargetAI();
			UpdateCrimeTracking();
			CheckTargetFound();
			break;
		case CONTRACT_FOUND:
			UpdateGiverPrompt();
			UpdateTrails();
			UpdateTargetAI();
			UpdateCrimeTracking();
			CheckTargetDeath();
			break;
		case CONTRACT_DEAD:
			UpdateTrails();
			UpdateGiverPrompt();
			UpdateCrimeTracking();
			break;
		case CONTRACT_PAID:
			UpdatePayment();
			break;
		}
		WAIT(0);
	}
}
