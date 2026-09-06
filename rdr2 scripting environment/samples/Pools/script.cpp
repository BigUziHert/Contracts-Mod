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
#include <cstdio>
#include <cstring>

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
	bool        photoTaken = false;     // this mod currently owns capture resources that still need cleanup
	bool        photoPedWasReady = false;
	int         photoCacheType = -1;    // cache type used for both writing and requesting the portrait
	bool        photoGenOk = false;
	char        photoTexture[64] = "";  // texture name to draw ("" = none)
	bool        photoWritten = false;
	bool        photoUploadPending = false, photoCommitReady = false, photoTextureValid = false;
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
	ULONGLONG   openedMs = 0;
	int         renderId = 0;
	bool        customApplied = false; // SET_CUSTOM_TEXTURES_ON_OBJECT tried on this object
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

static void MaintainPortraitAndCard();

// Evaluate once per iteration: a successful write predicate must not be invoked a second time.
template<typename Pred> static bool WaitUntil(DWORD timeoutMs, Pred pred)
{
	ULONGLONG deadline = GetTickCount64() + timeoutMs;
	for (;;)
	{
		MaintainPortraitAndCard();
		if (pred()) return true;
		if (GetTickCount64() >= deadline) return false;
		WAIT(0);
	}
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
// SP reference: Halen84/RDR3-Decompiled-Scripts, 1491.50/short_update.c, func_490.
// Type(1), A1(0), ready -> previous cleanup / generate / previous cleanup -> successful write ->
// !uploadPending && CC4 -> capture cleanup -> valid backup texture. A1's meaning is undocumented;
// its fixed argument is deliberately independent of the network cache type. The hidden subject,
// portrait texture, card face, and flipped information panel were verified together in-game.

// short_update refreshes retained slots each frame, even without a card on screen. Also maintain the
// flip flag during yielding model/animation/capture waits, which do not run UpdateCard().
static void MaintainPortraitAndCard()
{
	if (C.photoTexture[0] && C.photoTextureValid)
		NETWORK::_REQUEST_PEDSHOT_TEXTURE_LOCAL_BACKUP_DOWNLOAD(Card::kPhotoSlot, C.photoCacheType);
	if (Cd.examining && pedMe)
		PED::_SET_PED_BLACKBOARD_BOOL(pedMe, Card::kFlipBlackboard, true, -1);
}

// Copy the borrowed name before invoking another native; a nonempty name alone is not ready.
static bool LookupPhotoTexture(int cacheType, char (&out)[64])
{
	const char* n = NETWORK::_REQUEST_PEDSHOT_TEXTURE_LOCAL_BACKUP_DOWNLOAD(Card::kPhotoSlot, cacheType);
	if (!n || !*n || strnlen_s(n, sizeof out) >= sizeof out) return false;
	strcpy_s(out, n);
	C.photoTextureValid = NETWORK::_TEXTURE_DOWNLOAD_TEXTURE_NAME_IS_VALID(out) != 0;
	return C.photoTextureValid;
}

static void FinishPhotoCapture()
{
	if (!C.photoTaken) return;
	GRAPHICS::_PEDSHOT_INIT_CLEANUP_DATA();
	GRAPHICS::_PEDSHOT_FINISH_CLEANUP_DATA();
	C.photoTaken = false;
}

static bool FinishPhotoAttempt(bool success)
{
	FinishPhotoCapture();
	return success;
}

// Photographs `subject` (parked, frozen, in front of the player) into the local persona-photo cache.
static bool PhotographPed(Ped subject)
{
	const int ct = Card::kPhotoCacheType;
	C.photoCacheType = ct;
	C.photoPedWasReady = C.photoGenOk = false;
	C.photoWritten = false;
	C.photoUploadPending = C.photoCommitReady = C.photoTextureValid = false;
	C.photoTexture[0] = '\0';
	Cd.customApplied = false;
	if (!ENTITY::DOES_ENTITY_EXIST(subject)) return FinishPhotoAttempt(false);

	ENTITY::SET_ENTITY_VISIBLE(subject, !Tune::kPedshotHidden);
	if (!WaitUntil(Card::kPhotoUploadMs, []
	{
		C.photoUploadPending = NETWORK::_NETWORK_IS_PREVIOUS_UPLOAD_PENDING() != 0;
		return !C.photoUploadPending;
	})) return FinishPhotoAttempt(false);

	// Do not touch shared capture data until a previous upload has finished.
	C.photoTaken = true;
	GRAPHICS::_PEDSHOT_SET_PERSONA_PHOTO_TYPE(Card::kPhotoType);
	GRAPHICS::_0xA1A86055792FB249(0);
	const char* photoName = PED::IS_PED_MALE(subject) ? Card::kPhotoName : Card::kPhotoFemaleName;
	C.photoPedWasReady = WaitUntil(Tune::kPedshotReadyMs, [&]
	{
		PED::FORCE_PED_MOTION_STATE(subject, joaat("MotionState_DoNothing"), false, 0, false);
		return PED::IS_PED_READY_TO_RENDER(subject) != 0;
	});
	if (!C.photoPedWasReady) return FinishPhotoAttempt(false);

	GRAPHICS::_PEDSHOT_PREVIOUS_PERSONA_PHOTO_DATA_CLEANUP();
	C.photoGenOk = GRAPHICS::_PEDSHOT_GENERATE_PERSONA_PHOTO(photoName, subject, 0) != 0;
	GRAPHICS::_PEDSHOT_PREVIOUS_PERSONA_PHOTO_DATA_CLEANUP();
	PED::FORCE_PED_MOTION_STATE(subject, joaat("MotionState_DoNothing"), false, 0, false);
	if (!C.photoGenOk) return FinishPhotoAttempt(false);
	WAIT(0); // the SP script enters the write state on a later frame

	C.photoWritten = WaitUntil(Card::kPhotoWriteMs, [&]
	{
		PED::FORCE_PED_MOTION_STATE(subject, joaat("MotionState_DoNothing"), false, 0, false);
		C.photoUploadPending = NETWORK::_NETWORK_IS_PREVIOUS_UPLOAD_PENDING() != 0;
		return NETWORK::_NETWORK_PERSONA_PHOTO_WRITE_LOCAL(photoName, Card::kPhotoSlot, 1, ct) != 0;
	});
	if (!C.photoWritten) return FinishPhotoAttempt(false);

	WAIT(0);
	if (!WaitUntil(Card::kPhotoUploadMs, [&]
	{
		PED::FORCE_PED_MOTION_STATE(subject, joaat("MotionState_DoNothing"), false, 0, false);
		C.photoUploadPending = NETWORK::_NETWORK_IS_PREVIOUS_UPLOAD_PENDING() != 0;
		C.photoCommitReady = NETWORK::_0xCC4E72C339461ED1() != 0;
		return !C.photoUploadPending && C.photoCommitReady;
	})) return FinishPhotoAttempt(false);
	FinishPhotoCapture();

	char name[64] = "";
	if (!WaitUntil(Card::kPhotoNameMs, [&] { return LookupPhotoTexture(ct, name); }))
		return FinishPhotoAttempt(false);
	strcpy_s(C.photoTexture, name);
	Cd.customApplied = false;   // re-apply the (new) texture to any card that is out
	return FinishPhotoAttempt(true);
}

static void ReleaseTargetPhoto()
{
	// short_update releases its retained slots by stopping requests. Do not run global capture cleanup
	// for a completed photo, or apply the separate MP downloaded-mugshot release API to this cache.
	C.photoTexture[0] = '\0';
	C.photoTextureValid = false;
	FinishPhotoCapture();
}
static bool TargetPhotoReady() { return C.photoTexture[0] && C.photoTextureValid; }

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
	TASK::CLEAR_PED_TASKS_IMMEDIATELY(ped, false, true);

	PhotographPed(ped);

	ENTITY::SET_ENTITY_VISIBLE(ped, true);
	ENTITY::SET_ENTITY_COLLISION(ped, true, false);
	ENTITY::FREEZE_ENTITY_POSITION(ped, false);
	ENTITY::SET_ENTITY_COORDS(ped, def.spawn.x, def.spawn.y, def.spawn.z, false, false, false, true);
	ENTITY::PLACE_ENTITY_ON_GROUND_PROPERLY(ped, 1);
	return ped;
}

// ===== [ CONTRACT CARD ] =====
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
			return;
		}
		if (HUD::IS_NAMED_RENDERTARGET_REGISTERED(name)) HUD::RELEASE_NAMED_RENDERTARGET(name);
	}
	Cd.renderId = 0;
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
	Cd = CardRuntime();
}

static bool CardTaskRunning() { return TASK::IS_PED_RUNNING_TASK_ITEM_INTERACTION(pedMe) != 0; }

// Player takes the card out and examines it (Zoom / Flip / Put Away are the game's own prompts).
// Uses generic_photograph, prop p_cs_photonudie05x_4x6, slot primaryItem and paper-inspect states;
// Flip comes from the GENERIC_DOCUMENT_FLIP_AVAILABLE blackboard flag.
//  Path 1 holds our own card through _TASK_ITEM_INTERACTION_2; if that task refuses to start, path 2 lets the
//  game spawn the item's own prop (how R* document scripts do it) and takes that prop over.
static bool OpenCard()
{
	if (Cd.obj || Cd.examining) return false;
	Hash item  = Card::kItem;
	Hash state = Card::kStartState;
	PED::_SET_PED_BLACKBOARD_BOOL(pedMe, Card::kFlipBlackboard, true, -1);

	if (CreateCardObject())
	{
		// The task's first parameter is the title label (natives.h: propNameGxt) — proven in dev-7. Use our
		// LML label when installed, else the item hash (no title). No retry: re-issuing the task while the
		// first call was still starting restarted it without the label.
		bool haveLabel = HUD::DOES_TEXT_LABEL_EXIST(Card::kTitleLabel) != 0;
		Hash firstParam = haveLabel ? joaat(Card::kTitleLabel) : item;
		TASK::_TASK_ITEM_INTERACTION_2(pedMe, firstParam, Cd.obj, Card::kPrimaryItem, state, 1, 0, -1.0f);
		if (WaitUntil(Card::kTaskStartWaitMs * 2, CardTaskRunning))
		{
			Cd.examining = true;
			Cd.openedMs = GetTickCount64();
			return true;
		}
		DestroyCardObject();
	}

	TASK::START_TASK_ITEM_INTERACTION(pedMe, item, state, 1, 0, -1.0f);
	if (!WaitUntil(Card::kTaskStartWaitMs, CardTaskRunning))
	{
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
	Cd.examining = true;
	Cd.openedMs = GetTickCount64();
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
	return state == Card::kStateFlipToBack || state == Card::kStateFlippedBase;
}

static void UpdateCard()
{
	ULONGLONG now = GetTickCount64();
	if (C.cardOpenAtMs && now >= C.cardOpenAtMs)
	{
		C.cardOpenAtMs = 0;
		OpenCard();
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
		MaintainPortraitAndCard();

		// A contract whose target vanished (deleted by another script, fell out of the world) can never
		// be completed — end it instead of leaving stale blips on the map.
		if ((g_state == CONTRACT_UNKNOWN || g_state == CONTRACT_FOUND) && !TargetExists())
		{
			DisplaySubtitle("TARGET LOST");
			ClearContract(false);
		}

		// I: look at the contract card again.
		if (ContractActive() && !Cd.obj && !C.cardOpenAtMs)
		{
			if (IsKeyJustUp(Tune::kInspectCardKey)) OpenCard();
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
