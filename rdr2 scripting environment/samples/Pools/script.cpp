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
#include "target_ai_logic.h"
#include "handoff_logic.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cwchar>

// ===== [ STATE ] =====
enum ContractState { CONTRACT_NONE, CONTRACT_UNKNOWN, CONTRACT_FOUND, CONTRACT_DEAD, CONTRACT_PAID };
enum class ContractStartFailure { None, Interrupted, InvalidModel, ModelLoadTimeout, PedCreationFailed, PortraitFailed, PedPoolFull, CleanupPending, PhotoDiagnosticComplete };
static ContractStartFailure lastStartFailure = ContractStartFailure::None;
static const char* lastPhotoStage = "none";

// The single ped created by this mod remains tracked even if DELETE_PED clears its argument.
// Keeping this outside C prevents contract reset from losing an unconfirmed deletion.
struct OwnedPedRuntime
{
	Ped ped = 0;
	Hash model = 0;
	bool cleanupPending = false;
	bool blockedLogged = false;
	ULONGLONG requestedMs = 0;
	ULONGLONG nextAttemptMs = 0;
};
static OwnedPedRuntime ownedPed;
static unsigned long long ownedPedsCreated = 0, ownedPedsDeleted = 0, ownedPedsReleased = 0;
static constexpr DWORD kOwnedPedDeleteRetryMs = 250;
static constexpr DWORD kOwnedPedCleanupWaitMs = 1500;

struct ActiveContract
{
	const ContractDef* def = nullptr;
	Ped        target = 0;
	Vector3    targetPos;
	Blip       searchBlip = 0;          // radius blip while the target is unknown
	Blip       targetBlip = 0;          // entity blip once found, corpse blip once dead
	bool       corpseBlipPlaced = false;
	bool       trailsActive = false;
	TargetAI::Memory ai;
	Vector3    lastKnownPlayerPos;
	Hash       weapon = 0;             // one loadout per target, retained across encounters
	bool       damagedByPlayer = false; // current tick's consumed damage event

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
	int         photoDownload = -1;     // owned local-cache download; positive handles require release
	int         photoDownloadStatus = -1;
	char        photoLookupName[64] = ""; // last returned name, including an invalid result for diagnostics
	bool        photoLookupValid = false; // backup-loader validity probe; diagnostic, not handle readiness
	ULONGLONG   photoNextRequestMs = 0;
	bool        photoWritten = false;
	bool        photoUploadPending = false, photoCommitReady = false, photoTextureValid = false;
	bool        photoWriteComplete = false;
	bool        photoBusyBefore = false, photoBusyAfterCleanup = false, photoBusyAtRequest = false;
	bool        photoCommitBefore = false;
	unsigned    photoRequestAttempts = 0;
	bool        cardOpenPending = false;

	// hand-in
	int        payoutCents = 0;
	Ped        payingGiver = 0;
	ULONGLONG  handInStartMs = 0;
	bool       cashSpawned = false;
	Object     cashObj = 0;            // decorative cash; only SettlePayment credits the reward
	bool       paymentCredited = false;
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
	const char* ownedRenderTarget = nullptr;
	Ped         inspectingPed = 0;
	bool        customApplied = false;
	ULONGLONG   textureRefreshUntilMs = 0; // retry while the inspection material is being initialized
	Hash        textureItemState = 0;
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
static ULONGLONG giverCooldownUntilMs = 0;

struct HandoffRuntime
{
	bool active = false;
	bool payout = false;
	Ped giver = 0;
	Ped player = 0;
	Handoff::Memory progress;
	Handoff::Config config;
};
static HandoffRuntime handoff;
static void StopHandoff();

// Gameplay deadlines freeze while menus/loading suspend the loop. Streaming waits below
// deliberately use wall time so they still have a bounded failure path.
static ULONGLONG pausedDurationMs = 0;
static ULONGLONG pauseStartedMs = 0;
static void SetRuntimePaused(bool paused)
{
	ULONGLONG now = GetTickCount64();
	if (paused && !pauseStartedMs) pauseStartedMs = now;
	else if (!paused && pauseStartedMs)
	{
		pausedDurationMs += now - pauseStartedMs;
		pauseStartedMs = 0;
	}
}
static ULONGLONG RuntimeNowMs()
{
	return (pauseStartedMs ? pauseStartedMs : GetTickCount64()) - pausedDurationMs;
}

// ===== [ SMALL HELPERS ] =====
static float DistSq(const Vector3& a, const Vector3& b)
{
	float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
	return dx * dx + dy * dy + dz * dz;
}
static bool Within(const Vector3& a, const Vector3& b, float dist) { return DistSq(a, b) <= dist * dist; }

static bool TargetExists() { return C.target && ENTITY::DOES_ENTITY_EXIST(C.target); }
static bool ContractActive() { return g_state == CONTRACT_UNKNOWN || g_state == CONTRACT_FOUND || g_state == CONTRACT_DEAD; }
static bool LivingPed(Ped ped) { return ped && ENTITY::DOES_ENTITY_EXIST(ped) && !PED::IS_PED_DEAD_OR_DYING(ped, true); }
static bool PlayerAvailable() { return LivingPed(pedMe) && PLAYER::PLAYER_PED_ID() == pedMe; }
static bool CanStartInteraction()
{
	return PlayerAvailable() && !HUD::IS_PAUSE_MENU_ACTIVE() && !CAMERA::IS_SCREEN_FADED_OUT() &&
		PLAYER::IS_PLAYER_CONTROL_ON(me) && PED::IS_PED_ON_FOOT(pedMe) &&
		!PED::IS_PED_IN_COMBAT(pedMe, 0) &&
		!PED::IS_PED_RAGDOLL(pedMe) && !TASK::IS_PED_GETTING_UP(pedMe) &&
		!PED::IS_PED_HOGTIED(pedMe) && !PED::IS_PED_LASSOED(pedMe) &&
		!TASK::IS_PED_RUNNING_TASK_ITEM_INTERACTION(pedMe);
}

// Failure-only local diagnostics. Keep gameplay free of the old debug overlay, while preserving
// the actual failing stage instead of calling every startup error "no contracts available".
static void LogContractStartFailure(Hash model, int attempt)
{
	HMODULE module = nullptr;
	wchar_t path[MAX_PATH] = {};
	if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		reinterpret_cast<LPCWSTR>(&LogContractStartFailure), &module)) return;
	DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
	if (!length || length >= MAX_PATH) return;
	wchar_t* slash = std::wcsrchr(path, L'\\');
	if (!slash) return;
	if (wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"BountyContracts-startup.log") != 0) return;
	FILE* file = nullptr;
	if (_wfopen_s(&file, path, L"a") != 0 || !file) return;
	SYSTEMTIME time;
	GetSystemTime(&time);
	fprintf(file, "%04u-%02u-%02uT%02u:%02u:%02uZ start-v4 failure=%d photo=%s attempt=%d model=%08X player=%d current=%d alive=%d freePeds=%d download=%d status=%d name=\"%s\" nameValid=%d ready=%d generated=%d written=%d writeComplete=%d commitProbe=%d commitBefore=%d busyBefore=%d busyAfter=%d busyRequest=%d requests=%u\n",
		time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond,
		static_cast<int>(lastStartFailure), lastPhotoStage, attempt, model, pedMe, PLAYER::PLAYER_PED_ID(),
		LivingPed(pedMe) ? 1 : 0, PED::_GET_NUM_FREE_SLOTS_IN_PED_POOL(), C.photoDownload,
		C.photoDownloadStatus, C.photoLookupName, C.photoLookupValid ? 1 : 0, C.photoTextureValid ? 1 : 0,
		C.photoGenOk ? 1 : 0, C.photoWritten ? 1 : 0, C.photoWriteComplete ? 1 : 0,
		C.photoCommitReady ? 1 : 0, C.photoCommitBefore ? 1 : 0, C.photoBusyBefore ? 1 : 0,
		C.photoBusyAfterCleanup ? 1 : 0, C.photoBusyAtRequest ? 1 : 0, C.photoRequestAttempts);
	fclose(file);
}

static void LogOwnedPedCleanup(const char* event)
{
	HMODULE module = nullptr;
	wchar_t path[MAX_PATH] = {};
	if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		reinterpret_cast<LPCWSTR>(&LogOwnedPedCleanup), &module)) return;
	DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
	if (!length || length >= MAX_PATH) return;
	wchar_t* slash = std::wcsrchr(path, L'\\');
	if (!slash || wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"BountyContracts-cleanup.log") != 0) return;
	FILE* file = nullptr;
	if (_wfopen_s(&file, path, L"a") != 0 || !file) return;
	SYSTEMTIME time;
	GetSystemTime(&time);
	bool exists = ownedPed.ped && ENTITY::DOES_ENTITY_EXIST(ownedPed.ped);
	fprintf(file, "%04u-%02u-%02uT%02u:%02u:%02uZ cleanup-v1 event=%s ped=%d model=%08X exists=%d actualModel=%08X owner=%d persistence=%d freePeds=%d created=%llu deleted=%llu released=%llu\n",
		time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond,
		event, ownedPed.ped, ownedPed.model, exists ? 1 : 0,
		exists ? ENTITY::GET_ENTITY_MODEL(ownedPed.ped) : 0,
		exists ? ENTITY::DOES_ENTITY_BELONG_TO_THIS_SCRIPT(ownedPed.ped, false) : 0,
		exists ? ENTITY::_IS_ENTITY_OWNED_BY_PERSISTENCE_SYSTEM(ownedPed.ped) : 0,
		PED::_GET_NUM_FREE_SLOTS_IN_PED_POOL(), ownedPedsCreated, ownedPedsDeleted, ownedPedsReleased);
	fclose(file);
}

static void TrackOwnedPed(Ped ped, Hash model)
{
	if (!ped || ownedPed.ped) return;
	ownedPed.ped = ped;
	ownedPed.model = model;
	++ownedPedsCreated;
}

static bool OwnedPedIdentityMatches()
{
	return ENTITY::GET_ENTITY_MODEL(ownedPed.ped) == ownedPed.model &&
		ENTITY::DOES_ENTITY_BELONG_TO_THIS_SCRIPT(ownedPed.ped, false) &&
		!ENTITY::_IS_ENTITY_OWNED_BY_PERSISTENCE_SYSTEM(ownedPed.ped);
}

static void MaintainOwnedPedCleanup()
{
	if (!ownedPed.ped || !ownedPed.cleanupPending) return;
	if (!ENTITY::DOES_ENTITY_EXIST(ownedPed.ped))
	{
		++ownedPedsDeleted;
		LogOwnedPedCleanup("confirmed");
		ownedPed = OwnedPedRuntime();
		return;
	}
	ULONGLONG now = GetTickCount64();
	if (now < ownedPed.nextAttemptMs) return;
	ownedPed.nextAttemptMs = now + kOwnedPedDeleteRetryMs;
	if (!OwnedPedIdentityMatches())
	{
		if (!ownedPed.blockedLogged) { LogOwnedPedCleanup("ownership_blocked"); ownedPed.blockedLogged = true; }
		return;
	}
	if (!ownedPed.blockedLogged && now - ownedPed.requestedMs >= kOwnedPedCleanupWaitMs)
	{
		LogOwnedPedCleanup("still_exists");
		ownedPed.blockedLogged = true;
	}
	if (!PED::IS_PED_DEAD_OR_DYING(ownedPed.ped, true)) ENTITY::SET_ENTITY_LOAD_COLLISION_FLAG(ownedPed.ped, false);
	Ped deleteArgument = ownedPed.ped;
	PED::DELETE_PED(&deleteArgument);
	// Confirmation uses the original handle on a later maintenance poll, never the native's output.
}

static void RequestOwnedPedCleanup(Ped ped)
{
	if (!ped || ped != ownedPed.ped || ownedPed.cleanupPending) return;
	ownedPed.cleanupPending = true;
	ownedPed.requestedMs = GetTickCount64();
	LogOwnedPedCleanup("requested");
	MaintainOwnedPedCleanup();
}

static void ReleaseOwnedPed(Ped ped)
{
	if (!ped || ped != ownedPed.ped) return;
	if (ownedPed.cleanupPending) return;
	if (ENTITY::DOES_ENTITY_EXIST(ped))
	{
		if (!OwnedPedIdentityMatches()) { RequestOwnedPedCleanup(ped); return; }
		Ped releaseArgument = ped;
		ENTITY::SET_ENTITY_AS_NO_LONGER_NEEDED(&releaseArgument);
	}
	++ownedPedsReleased;
	LogOwnedPedCleanup("released");
	ownedPed = OwnedPedRuntime();
}

static const char* Literal(const char* text) { return MISC::VAR_STRING(10, "LITERAL_STRING", text); }

static void DisplaySubtitle(const char* message) { DisplayObjective(message); }

static void ReportContractStartFailure()
{
	switch (lastStartFailure)
	{
	case ContractStartFailure::Interrupted: DisplaySubtitle("CONTRACT REQUEST INTERRUPTED. TRY AGAIN."); break;
	case ContractStartFailure::InvalidModel: DisplaySubtitle("CONTRACT MODEL UNAVAILABLE. TRY AGAIN."); break;
	case ContractStartFailure::ModelLoadTimeout: DisplaySubtitle("TARGET MODEL COULD NOT LOAD. TRY AGAIN."); break;
	case ContractStartFailure::PedCreationFailed: DisplaySubtitle("TARGET COULD NOT SPAWN. TRY AGAIN."); break;
	case ContractStartFailure::PortraitFailed: DisplaySubtitle("TARGET PHOTO COULD NOT BE PREPARED. TRY AGAIN."); break;
	case ContractStartFailure::PedPoolFull: DisplaySubtitle("NO ROOM FOR ANOTHER TARGET. TRY AGAIN AFTER LEAVING THE AREA."); break;
	case ContractStartFailure::CleanupPending: DisplaySubtitle("PREVIOUS TARGET IS STILL BEING REMOVED. TRY AGAIN SHORTLY."); break;
	case ContractStartFailure::PhotoDiagnosticComplete: DisplaySubtitle("PHOTO TEST FINISHED. SEE BountyContracts-photo-test.log."); break;
	case ContractStartFailure::None: DisplaySubtitle("CONTRACT REQUEST FAILED. TRY AGAIN."); break;
	}
}

static void FormatMoney(char* out, size_t size, int cents) { sprintf_s(out, size, "$%d.%02d", cents / 100, cents % 100); }

static void RemoveBlip(Blip& blip)
{
	if (blip && MAP::DOES_BLIP_EXIST(blip)) MAP::REMOVE_BLIP(&blip);
	blip = 0;
}

static void ShowPrompt(Prompt prompt, bool show)
{
	if (!prompt || !HUD::_UI_PROMPT_IS_VALID(prompt)) return;
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
static void ApplyCardCustomTexture();
static void RefreshCardTextureAfterTransition();

// Evaluate once per iteration: a successful write predicate must not be invoked a second time.
template<typename Pred> static bool WaitUntil(DWORD timeoutMs, Pred pred)
{
	ULONGLONG deadline = GetTickCount64() + timeoutMs;
	for (;;)
	{
		MaintainOwnedPedCleanup();
		if (!PlayerAvailable()) return false;
		MaintainPortraitAndCard();
		if (pred()) return true;
		if (GetTickCount64() >= deadline) return false;
		WAIT(0);
	}
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
	lastStartFailure = ContractStartFailure::None;
	if (!PlayerAvailable()) { lastStartFailure = ContractStartFailure::Interrupted; return 0; }
	if (ownedPed.ped && !WaitUntil(kOwnedPedCleanupWaitMs, [] { return ownedPed.ped == 0; }))
	{
		lastStartFailure = PlayerAvailable() ? ContractStartFailure::CleanupPending : ContractStartFailure::Interrupted;
		return 0;
	}
	if (!STREAMING::IS_MODEL_VALID(model) || !STREAMING::IS_MODEL_IN_CDIMAGE(model) || !STREAMING::IS_MODEL_A_PED(model))
	{
		lastStartFailure = ContractStartFailure::InvalidModel;
		return 0;
	}
	if (!LoadModel(model))
	{
		lastStartFailure = PlayerAvailable() ? ContractStartFailure::ModelLoadTimeout : ContractStartFailure::Interrupted;
		return 0;
	}

	Ped ped = 0;
	ULONGLONG nextAttemptMs = 0;
	// A loaded model does not guarantee that the engine can create its ped this frame.
	// Keep the model requested and let the game advance between failed creation attempts.
	bool created = WaitUntil(Tune::kPedSpawnRetryMs, [&] {
		// A nonzero handle may become observable on a later frame. Never overwrite it
		// with another creation while this attempt still owns the pending handle.
		if (ped) return ENTITY::DOES_ENTITY_EXIST(ped) != 0;
		ULONGLONG now = GetTickCount64();
		if (now < nextAttemptMs) return false;
		nextAttemptMs = now + Tune::kPedSpawnRetryDelayMs;
		if (PED::_GET_NUM_FREE_SLOTS_IN_PED_POOL() <= 0) return false;
		ped = PED::CREATE_PED(model, pos, 0.0f, false, true, true, true);
		TrackOwnedPed(ped, model);
		return ped && ENTITY::DOES_ENTITY_EXIST(ped);
	});
	if (!created || !PlayerAvailable())
	{
		lastStartFailure = !PlayerAvailable() ? ContractStartFailure::Interrupted
			: PED::_GET_NUM_FREE_SLOTS_IN_PED_POOL() <= 0 ? ContractStartFailure::PedPoolFull
			: ContractStartFailure::PedCreationFailed;
		RequestOwnedPedCleanup(ped);
		STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
		return 0;
	}
	ENTITY::SET_ENTITY_AS_MISSION_ENTITY(ped, true, true);
	PED::_SET_RANDOM_OUTFIT_VARIATION(ped, true);
	ENTITY::PLACE_ENTITY_ON_GROUND_PROPERLY(ped, 1);
	STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
	if (!ENTITY::DOES_ENTITY_EXIST(ped)) { RequestOwnedPedCleanup(ped); lastStartFailure = ContractStartFailure::PedCreationFailed; return 0; }
	return ped;
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
// Reference: 1491.50/script_mp_rel/persona_photos.ysc.c, func_3 / func_10 / func_11.
// The type-2 producer waits for !PEDSHOT_IS_AVAILABLE and !previousUpload, clears old data
// BEFORE generation, writes on a later frame, waits for upload completion, and cleans up on
// another frame. The SP A1/CC4 and post-generate cleanup sequence belongs to a different path.

// Cache type 2 uses an owned download handle (R* persona_photos / map_app_event_handler),
// released before rewriting the same slot. Do not mix that ownership with the SP backup loader.
static bool LookupPhotoTexture(int cacheType, char (&out)[64]);
static void ReleaseTargetPhoto();

// Maintain the retained download, card material and flip flag during yielding waits,
// which do not run UpdateCard().
static void MaintainPortraitAndCard()
{
	if (C.photoTexture[0])
	{
		bool wasValid = C.photoTextureValid;
		char name[64] = "";
		C.photoTextureValid = LookupPhotoTexture(C.photoCacheType, name);
		if (C.photoTextureValid && (!wasValid || strcmp(C.photoTexture, name) != 0))
		{
			strcpy_s(C.photoTexture, name);
			Cd.customApplied = false;
			Cd.textureRefreshUntilMs = RuntimeNowMs() + Card::kTextureSettleMs;
		}
	}
	if (Cd.obj && Cd.ownsObj && ENTITY::DOES_ENTITY_EXIST(Cd.obj))
		ENTITY::SET_ENTITY_VISIBLE(Cd.obj, C.photoTexture[0] && C.photoTextureValid);
	ApplyCardCustomTexture(); // also runs while the inspect task has not yet reported its primary item
	if (Cd.examining && Cd.inspectingPed && ENTITY::DOES_ENTITY_EXIST(Cd.inspectingPed))
		PED::_SET_PED_BLACKBOARD_BOOL(Cd.inspectingPed, Card::kFlipBlackboard, true, -1);
}

// Request once and retain the positive handle until cleanup. A failed download can be
// released and requested again, but never allocate a new handle on every pending frame.
// Readiness follows map_app_event_handler: status 0 and a nonempty name. The backup-loader
// validity native is only a diagnostic here; its semantics for explicit handles are undocumented.
// Copy the borrowed name before invoking another native.
static bool LookupPhotoTexture(int cacheType, char (&out)[64])
{
	C.photoTextureValid = false;
	C.photoLookupValid = false;
	if (C.photoDownload <= 0)
	{
		if (GetTickCount64() < C.photoNextRequestMs) return false;
		C.photoNextRequestMs = GetTickCount64() + Card::kPhotoRequestRetryMs;
		C.photoDownloadStatus = -1;
		C.photoLookupName[0] = '\0';
		C.photoBusyAtRequest = GRAPHICS::PEDSHOT_IS_AVAILABLE() != 0;
		++C.photoRequestAttempts;
		C.photoDownload = NETWORK::_LOCAL_PLAYER_PEDSHOT_TEXTURE_DOWNLOAD_REQUEST(Card::kPhotoSlot, cacheType);
		if (C.photoDownload <= 0) return false;
	}
	C.photoDownloadStatus = NETWORK::GET_STATUS_OF_TEXTURE_DOWNLOAD(C.photoDownload);
	if (C.photoDownloadStatus == 2)
	{
		NETWORK::TEXTURE_DOWNLOAD_RELEASE(C.photoDownload);
		C.photoDownload = -1;
		C.photoNextRequestMs = GetTickCount64() + Card::kPhotoRequestRetryMs;
		return false;
	}
	if (C.photoDownloadStatus != 0) return false;
	C.photoLookupName[0] = '\0';
	const char* n = NETWORK::TEXTURE_DOWNLOAD_GET_NAME(C.photoDownload);
	if (!n || !*n || strnlen_s(n, sizeof out) >= sizeof out) return false;
	strcpy_s(out, n);
	strcpy_s(C.photoLookupName, out);
	C.photoLookupValid = NETWORK::_TEXTURE_DOWNLOAD_TEXTURE_NAME_IS_VALID(out) != 0;
	C.photoTextureValid = true;
	return C.photoTextureValid;
}

static void FinishPhotoCapture()
{
	if (!C.photoTaken) return;
	GRAPHICS::_PEDSHOT_INIT_CLEANUP_DATA();
	GRAPHICS::_PEDSHOT_FINISH_CLEANUP_DATA();
	C.photoTaken = false;
	C.photoBusyAfterCleanup = GRAPHICS::PEDSHOT_IS_AVAILABLE() != 0;
}

static bool FinishPhotoAttempt(bool success)
{
	FinishPhotoCapture();
	return success;
}

// Photographs `subject` (parked, frozen, in front of the player) into the local persona-photo cache.
static bool PhotographPed(Ped subject)
{
	// Includes a pending/failed previous attempt, not only a previously accepted portrait.
	// The type-2 cache consumer must be released before its slot is overwritten.
	ReleaseTargetPhoto();
	lastPhotoStage = "subject";
	const int ct = Card::kPhotoCacheType;
	C.photoCacheType = ct;
	C.photoPedWasReady = C.photoGenOk = false;
	C.photoWritten = false;
	C.photoUploadPending = C.photoCommitReady = C.photoTextureValid = false;
	C.photoWriteComplete = false;
	C.photoBusyBefore = GRAPHICS::PEDSHOT_IS_AVAILABLE() != 0;
	C.photoBusyAfterCleanup = C.photoBusyAtRequest = false;
	C.photoCommitBefore = NETWORK::_0xCC4E72C339461ED1() != 0;
	C.photoRequestAttempts = 0;
	C.photoTexture[0] = '\0';
	Cd.customApplied = false;
	if (!ENTITY::DOES_ENTITY_EXIST(subject)) return FinishPhotoAttempt(false);

	ENTITY::SET_ENTITY_VISIBLE(subject, !Tune::kPedshotHidden);
	const char* photoName = PED::IS_PED_MALE(subject) ? Card::kPhotoName : Card::kPhotoFemaleName;
	lastPhotoStage = "ped_assets";
	C.photoPedWasReady = WaitUntil(Tune::kPedshotReadyMs, [&]
	{
		PED::FORCE_PED_MOTION_STATE(subject, joaat("MotionState_DoNothing"), false, 0, false);
		return PED::IS_PED_READY_TO_RENDER(subject) != 0;
	});
	if (!C.photoPedWasReady) return FinishPhotoAttempt(false);

	// PEDSHOT_IS_AVAILABLE is counterintuitive: the MP producer defers while it is true.
	// Check after streaming the subject, before taking ownership or clearing any shared data.
	lastPhotoStage = "capture_busy";
	if (!WaitUntil(Card::kPhotoUploadMs, [&]
	{
		PED::FORCE_PED_MOTION_STATE(subject, joaat("MotionState_DoNothing"), false, 0, false);
		bool busy = GRAPHICS::PEDSHOT_IS_AVAILABLE() != 0;
		C.photoUploadPending = NETWORK::_NETWORK_IS_PREVIOUS_UPLOAD_PENDING() != 0;
		return !busy && !C.photoUploadPending;
	})) return FinishPhotoAttempt(false);
	C.photoTaken = true;
	GRAPHICS::_PEDSHOT_PREVIOUS_PERSONA_PHOTO_DATA_CLEANUP();
	GRAPHICS::_PEDSHOT_SET_PERSONA_PHOTO_TYPE(Card::kPhotoType);
	lastPhotoStage = "generate";
	C.photoGenOk = GRAPHICS::_PEDSHOT_GENERATE_PERSONA_PHOTO(photoName, subject, 0) != 0;
	PED::FORCE_PED_MOTION_STATE(subject, joaat("MotionState_DoNothing"), false, 0, false);
	if (!C.photoGenOk) return FinishPhotoAttempt(false);
	WAIT(0);

	lastPhotoStage = "write";
	C.photoWritten = WaitUntil(Card::kPhotoWriteMs, [&]
	{
		PED::FORCE_PED_MOTION_STATE(subject, joaat("MotionState_DoNothing"), false, 0, false);
		C.photoUploadPending = NETWORK::_NETWORK_IS_PREVIOUS_UPLOAD_PENDING() != 0;
		return NETWORK::_NETWORK_PERSONA_PHOTO_WRITE_LOCAL(photoName, Card::kPhotoSlot, 1, ct) != 0;
	});
	if (!C.photoWritten) return FinishPhotoAttempt(false);

	WAIT(0);
	lastPhotoStage = "upload";
	C.photoWriteComplete = WaitUntil(Card::kPhotoUploadMs, [&]
	{
		PED::FORCE_PED_MOTION_STATE(subject, joaat("MotionState_DoNothing"), false, 0, false);
		C.photoUploadPending = NETWORK::_NETWORK_IS_PREVIOUS_UPLOAD_PENDING() != 0;
		C.photoCommitReady = NETWORK::_0xCC4E72C339461ED1() != 0;
		return !C.photoUploadPending; // CC4 is diagnostic only for the type-2 MP producer
	});
	if (!C.photoWriteComplete) return FinishPhotoAttempt(false);
	WAIT(0); // MP case 2 -> case 3: cleanup follows completion on another frame
	FinishPhotoCapture();
	WAIT(0); // let the cache publish the completed capture before requesting its download

	char name[64] = "";
	lastPhotoStage = "texture_download";
	if (!WaitUntil(Card::kPhotoNameMs, [&] { return LookupPhotoTexture(ct, name); }))
		return FinishPhotoAttempt(false);
	strcpy_s(C.photoTexture, name);
	Cd.customApplied = false;   // re-apply the (new) texture to any card that is out
	lastPhotoStage = "none";
	return FinishPhotoAttempt(true);
}

static void ReleaseTargetPhoto()
{
	if (C.photoDownload > 0) NETWORK::TEXTURE_DOWNLOAD_RELEASE(C.photoDownload);
	C.photoDownload = -1;
	C.photoDownloadStatus = -1;
	C.photoNextRequestMs = 0;
	C.photoLookupName[0] = '\0';
	C.photoLookupValid = false;
	C.photoTexture[0] = '\0';
	C.photoTextureValid = false;
	FinishPhotoCapture();
}
static bool TargetPhotoReady() { return C.photoTexture[0] && C.photoTextureValid; }

static bool EnsureTargetPhotoReady()
{
	if (!C.photoTexture[0]) return false;
	return WaitUntil(Card::kPhotoNameMs, TargetPhotoReady); // maintenance polls the owned download
}

#ifdef BOUNTY_PHOTO_SELF_TEST
// Opt-in diagnostic build: one provisional subject, a real inspected card, no bounty.
// In particular, probe A never generates or writes: it tests whether a released
// consumer can reopen the SAME published photo after card use, before another write.
static unsigned photoTestBindAttempts = 0;
static bool ProbePhotoCard(ULONGLONG started, unsigned captures);
static void LogPhotoCacheTest(const char* phase, bool success, ULONGLONG started,
	unsigned captures, const char* previousName, const char* observedName,
	Object observedCard = 0, bool cardOwned = false)
{
	HMODULE module = nullptr;
	wchar_t path[MAX_PATH] = {};
	if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		reinterpret_cast<LPCWSTR>(&LogPhotoCacheTest), &module)) return;
	DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
	if (!length || length >= MAX_PATH) return;
	wchar_t* slash = std::wcsrchr(path, L'\\');
	if (!slash || wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"BountyContracts-photo-test.log") != 0) return;
	FILE* file = nullptr;
	if (_wfopen_s(&file, path, L"a") != 0 || !file) return;
	SYSTEMTIME time;
	GetSystemTime(&time);
	bool previousValid = previousName[0] && NETWORK::_TEXTURE_DOWNLOAD_TEXTURE_NAME_IS_VALID(previousName);
	bool itemRunning = LivingPed(pedMe) && TASK::IS_PED_RUNNING_TASK_ITEM_INTERACTION(pedMe);
	Hash itemState = itemRunning ? TASK::GET_ITEM_INTERACTION_STATE(pedMe) : 0;
	fprintf(file, "%04u-%02u-%02uT%02u:%02u:%02uZ photo-test-v2 phase=%s ok=%d elapsedMs=%llu captures=%u slot=%d cache=%d stage=%s download=%d status=%d requests=%u name=\"%s\" previous=\"%s\" previousValid=%d nameValid=%d ready=%d generated=%d written=%d writeComplete=%d busyBefore=%d busyAfter=%d busyRequest=%d freePeds=%d card=%d cardOwned=%d cardExists=%d binds=%u itemRunning=%d itemState=%08X\n",
		time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond,
		phase, success ? 1 : 0, GetTickCount64() - started, captures, Card::kPhotoSlot, Card::kPhotoCacheType,
		lastPhotoStage, C.photoDownload, C.photoDownloadStatus, C.photoRequestAttempts, observedName,
		previousName, previousValid ? 1 : 0, C.photoLookupValid ? 1 : 0, TargetPhotoReady() ? 1 : 0, C.photoGenOk ? 1 : 0,
		C.photoWritten ? 1 : 0, C.photoWriteComplete ? 1 : 0, C.photoBusyBefore ? 1 : 0,
		C.photoBusyAfterCleanup ? 1 : 0, C.photoBusyAtRequest ? 1 : 0, PED::_GET_NUM_FREE_SLOTS_IN_PED_POOL(),
		observedCard, cardOwned ? 1 : 0, observedCard && ENTITY::DOES_ENTITY_EXIST(observedCard) ? 1 : 0,
		photoTestBindAttempts, itemRunning ? 1 : 0, itemState);
	fclose(file);
}

static bool ProbeExistingPhoto(char (&name)[64])
{
	// Clear the accepted name so WaitUntil's normal maintenance cannot create a
	// second consumer or change the outcome before this probe gets to poll it.
	ReleaseTargetPhoto();
	C.photoRequestAttempts = 0;
	name[0] = '\0';
	lastPhotoStage = "probe_explicit";
	return WaitUntil(Card::kPhotoNameMs, [&] { return LookupPhotoTexture(Card::kPhotoCacheType, name); });
}

static bool ProbeBackupPhoto(char (&name)[64])
{
	ReleaseTargetPhoto();
	C.photoRequestAttempts = 0;
	name[0] = '\0';
	lastPhotoStage = "probe_backup";
	return WaitUntil(Card::kPhotoNameMs, [&]
	{
		++C.photoRequestAttempts;
		const char* borrowed = NETWORK::_REQUEST_PEDSHOT_TEXTURE_LOCAL_BACKUP_DOWNLOAD(Card::kPhotoSlot, Card::kPhotoCacheType);
		if (!borrowed || !*borrowed || strnlen_s(borrowed, sizeof name) >= sizeof name) return false;
		strcpy_s(name, borrowed); // copy before another native invalidates its return buffer
		strcpy_s(C.photoLookupName, name);
		C.photoLookupValid = NETWORK::_TEXTURE_DOWNLOAD_TEXTURE_NAME_IS_VALID(name) != 0;
		return C.photoLookupValid;
	});
}

static bool RunPhotoCacheSelfTest(Ped subject)
{
	ULONGLONG started = GetTickCount64();
	unsigned captures = 0;
	photoTestBindAttempts = 0;
	char previousName[64] = "";
	char name[64] = "";
	LogPhotoCacheTest("begin", true, started, captures, previousName, name);
	// Caller releases any remaining handle and removes this one provisional ped,
	// including when a wait is interrupted. A failed probe is a completed test.
	if (!PlayerAvailable()) return false;
	bool initial = PhotographPed(subject);
	++captures;
	LogPhotoCacheTest("initial", initial, started, captures, previousName, C.photoLookupName);
	if (!initial || !PlayerAvailable()) return PlayerAvailable();
	strcpy_s(previousName, C.photoTexture);

	bool inspected = ProbePhotoCard(started, captures);
	if (inspected && C.photoTexture[0]) strcpy_s(previousName, C.photoTexture);
	LogPhotoCacheTest("card_inspection", inspected, started, captures, previousName, C.photoLookupName);
	if (!inspected || !PlayerAvailable()) return PlayerAvailable();

	bool reopened = ProbeExistingPhoto(name);
	LogPhotoCacheTest("A_reopen_without_write", reopened, started, captures, previousName, name);
	if (!PlayerAvailable()) return false;
	if (reopened)
	{
		strcpy_s(previousName, name); // invalidate the latest accepted consumer name if B fails
		// PhotographPed releases A's owned handle before writing again. Keep the
		// same subject so later recovery cannot accidentally accept an older target.
		bool rewritten = PhotographPed(subject);
		++captures;
		LogPhotoCacheTest("B_second_capture", rewritten, started, captures, previousName, C.photoLookupName);
		if (rewritten || !PlayerAvailable()) return PlayerAvailable();
		// A producer failure cannot tell us whether a readback workaround works.
		if (!C.photoWritten || !C.photoWriteComplete) return true;
	}

	ReleaseTargetPhoto();
	if (previousName[0])
	{
		// Experimental control based on pause_menu's mugshot invalidation. Only
		// the exact name returned by our own successful download may be released.
		lastPhotoStage = "probe_name_invalidation";
		bool validBefore = NETWORK::_TEXTURE_DOWNLOAD_TEXTURE_NAME_IS_VALID(previousName) != 0;
		LogPhotoCacheTest("C_before_name_release", validBefore, started, captures, previousName, "");
		if (!PlayerAvailable()) return false;
		NETWORK::_TEXTURE_DOWNLOAD_RELEASE_BY_NAME(previousName);
		bool invalidated = WaitUntil(Card::kPhotoNameMs, [&]
		{
			return !NETWORK::_TEXTURE_DOWNLOAD_TEXTURE_NAME_IS_VALID(previousName);
		});
		LogPhotoCacheTest(validBefore ? "C_name_invalidation" : "C_name_already_invalid",
			invalidated, started, captures, previousName, "");
		if (!PlayerAvailable()) return false;
		if (invalidated)
		{
			bool recovered = ProbeExistingPhoto(name); // deliberately no generate/write
			LogPhotoCacheTest("C_reopen_without_write", recovered, started, captures, previousName, name);
			if (recovered || !PlayerAvailable()) return PlayerAvailable();
		}
	}

	// Last, after C: this probes backup recovery after name invalidation, not an
	// independent untouched-cache control. It provides no explicit owned handle.
	// Never put its name in C.photoTexture, which would start normal handle polling.
	bool backup = ProbeBackupPhoto(name);
	LogPhotoCacheTest("D_backup_without_write", backup, started, captures, previousName, name);
	return PlayerAvailable();
}
#endif

// Spawns the target in front of the player first — hidden, frozen, no collision, exactly how the persona-
// photo script parks its clone — so his clothes and textures stream in and the portrait can be taken.
// Then moves him to his town.
static Ped SpawnTargetWithPhoto(Hash model, const ContractDef& def)
{
	Ped ped = SpawnPed(model, ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(pedMe, 0.0f, Card::kPhotoPedOffsetY, 0.0f));
	if (!ped) return 0;
	ENTITY::FREEZE_ENTITY_POSITION(ped, true);
	ENTITY::SET_ENTITY_COLLISION(ped, false, false);
	ENTITY::SET_ENTITY_HEADING(ped, ENTITY::GET_ENTITY_HEADING(pedMe) + 180.0f); // facing the player / camera
	PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(ped, false);
	TASK::CLEAR_PED_TASKS_IMMEDIATELY(ped, false, true);

#ifdef BOUNTY_PHOTO_SELF_TEST
	ENTITY::SET_ENTITY_VISIBLE(ped, false);
	C.def = &def; // show the normal flipped information panel during this diagnostic
	bool testComplete = RunPhotoCacheSelfTest(ped);
	C.def = nullptr;
	ReleaseTargetPhoto();
	RequestOwnedPedCleanup(ped);
	lastStartFailure = testComplete ? ContractStartFailure::PhotoDiagnosticComplete : ContractStartFailure::Interrupted;
	return 0;
#endif

	bool photographed = false;
	for (int attempt = 1; attempt <= Card::kPhotoAttempts; ++attempt)
	{
		if (PlayerAvailable() && ENTITY::DOES_ENTITY_EXIST(ped) && PhotographPed(ped))
		{
			photographed = true;
			break;
		}
		lastStartFailure = PlayerAvailable() ? ContractStartFailure::PortraitFailed : ContractStartFailure::Interrupted;
		LogContractStartFailure(model, attempt);
		if (lastStartFailure == ContractStartFailure::Interrupted || !ENTITY::DOES_ENTITY_EXIST(ped)) break;
		if (attempt < Card::kPhotoAttempts) WAIT(0);
	}
	if (!photographed)
	{
		RequestOwnedPedCleanup(ped);
		ReleaseTargetPhoto();
		return 0;
	}

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
		// Never take over or release another script's registration.
		if (HUD::IS_NAMED_RENDERTARGET_REGISTERED(name)) continue;
		if (!HUD::REGISTER_NAMED_RENDERTARGET(name, false)) continue;
		HUD::LINK_NAMED_RENDERTARGET(cardModel);
		if (HUD::IS_NAMED_RENDERTARGET_LINKED(cardModel))
		{
			Cd.renderId = HUD::GET_NAMED_RENDERTARGET_RENDER_ID(name);
			Cd.ownedRenderTarget = name;
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
	if (!TargetPhotoReady()) return false;
	if (Cd.obj) return ENTITY::DOES_ENTITY_EXIST(Cd.obj) != 0;
	if (!LoadModel(Card::kPropModel)) return false;
	if (!TargetPhotoReady()) { STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(Card::kPropModel); return false; }
	Cd.obj = OBJECT::CREATE_OBJECT(Card::kPropModel, ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(pedMe, 0.0f, 0.5f, 0.0f), true, true, true, false, false);
	STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(Card::kPropModel);
	if (!ENTITY::DOES_ENTITY_EXIST(Cd.obj)) { Cd.obj = 0; return false; }
	Cd.ownsObj = true;
	SetCardTitle(Cd.obj);
	LinkCardRenderTarget(Card::kPropModel);
	RefreshCardTextureAfterTransition();
	return true;
}

static bool OwnCardTaskRunning()
{
	return Cd.inspectingPed && ENTITY::DOES_ENTITY_EXIST(Cd.inspectingPed) && Cd.obj &&
		TASK::IS_PED_RUNNING_TASK_ITEM_INTERACTION(Cd.inspectingPed) &&
		TASK::_GET_ITEM_INTERACTION_ENTITY_FROM_PED(Cd.inspectingPed, Card::kPrimaryItem) == Cd.obj;
}

static void DestroyCardObject(bool cancelInspection = false)
{
	if (cancelInspection && OwnCardTaskRunning())
	{
		TASK::_SET_ITEM_INTERACTION_STATE(Cd.inspectingPed, Card::kStateOutro, 0.25f);
		if (!WaitUntil(1200, [] { return !OwnCardTaskRunning(); }) && OwnCardTaskRunning())
			TASK::CLEAR_PED_TASKS(Cd.inspectingPed, true, false);
	}
	if (Cd.inspectingPed && ENTITY::DOES_ENTITY_EXIST(Cd.inspectingPed))
		PED::_SET_PED_BLACKBOARD_BOOL(Cd.inspectingPed, Card::kFlipBlackboard, false, -1);
	if (Cd.obj && Cd.ownsObj && ENTITY::DOES_ENTITY_EXIST(Cd.obj))
	{
		if (ENTITY::IS_ENTITY_ATTACHED(Cd.obj)) ENTITY::DETACH_ENTITY(Cd.obj, true, false);
		OBJECT::DELETE_OBJECT(&Cd.obj);
	}
	if (Cd.ownedRenderTarget) HUD::RELEASE_NAMED_RENDERTARGET(Cd.ownedRenderTarget);
	Cd = CardRuntime();
}

// Player takes the card out and examines it (Zoom / Flip / Put Away are the game's own prompts).
// Uses generic_photograph, prop p_cs_photonudie05x_4x6, slot primaryItem and paper-inspect states;
// Flip comes from the GENERIC_DOCUMENT_FLIP_AVAILABLE blackboard flag.
//  Path 1 holds our own card through _TASK_ITEM_INTERACTION_2; if that task refuses to start, path 2 lets the
//  game spawn the item's own prop (how R* document scripts do it) and takes that prop over.
static bool OpenCard(bool reuseHandoffCard = false)
{
	if (!CanStartInteraction() || Cd.examining || (Cd.obj && !reuseHandoffCard)) return false;
	if (!EnsureTargetPhotoReady())
	{
		DisplaySubtitle("CONTRACT PHOTO UNAVAILABLE. PRESS I TO RETRY.");
		return false;
	}
	if (!CanStartInteraction()) return false;
	if (Cd.obj && ENTITY::IS_ENTITY_ATTACHED(Cd.obj)) ENTITY::DETACH_ENTITY(Cd.obj, true, false);
	Cd.inHand = false;
	Cd.inspectingPed = pedMe;
	Hash item  = Card::kItem;
	Hash state = Card::kStartState;
	PED::_SET_PED_BLACKBOARD_BOOL(pedMe, Card::kFlipBlackboard, true, -1);

	if (CreateCardObject())
	{
		if (!CanStartInteraction()) { DestroyCardObject(); return false; }
		// The task's first parameter is the title label (natives.h: propNameGxt) — proven in dev-7. Use our
		// LML label when installed, else the item hash (no title). No retry: re-issuing the task while the
		// first call was still starting restarted it without the label.
		bool haveLabel = HUD::DOES_TEXT_LABEL_EXIST(Card::kTitleLabel) != 0;
		Hash firstParam = haveLabel ? joaat(Card::kTitleLabel) : item;
		TASK::_TASK_ITEM_INTERACTION_2(pedMe, firstParam, Cd.obj, Card::kPrimaryItem, state, 1, 0, -1.0f);
		RefreshCardTextureAfterTransition(); // the reused handoff prop is entering a new native task
		if (WaitUntil(Card::kTaskStartWaitMs * 2, OwnCardTaskRunning))
		{
			RefreshCardTextureAfterTransition(); // bind again after the task confirms this exact prop
			Cd.examining = true;
			Cd.openedMs = RuntimeNowMs();
			return true;
		}
		DestroyCardObject(true);
	}

	if (!CanStartInteraction() || !TargetPhotoReady()) return false;
	Cd.inspectingPed = pedMe;
	auto abandonFallback = [&] {
		if (PlayerAvailable())
		{
			bool running = TASK::IS_PED_RUNNING_TASK_ITEM_INTERACTION(pedMe) != 0;
			bool ours = running && TASK::GET_ITEM_INTERACTION_ITEM_ID(pedMe) == item;
			if (ours) TASK::_SET_ITEM_INTERACTION_STATE(pedMe, Card::kStateOutro, 0.25f);
			if (!running || ours) PED::_SET_PED_BLACKBOARD_BOOL(pedMe, Card::kFlipBlackboard, false, -1);
		}
		Cd.inspectingPed = 0;
		DestroyCardObject();
	};
	TASK::START_TASK_ITEM_INTERACTION(pedMe, item, state, 1, 0, -1.0f);
	if (!WaitUntil(Card::kTaskStartWaitMs, [&] {
		return TASK::IS_PED_RUNNING_TASK_ITEM_INTERACTION(pedMe) && TASK::GET_ITEM_INTERACTION_ITEM_ID(pedMe) == item;
	}))
	{
		abandonFallback();
		return false;
	}
	Entity held = 0;
	WaitUntil(500, [&] { held = TASK::_GET_ITEM_INTERACTION_ENTITY_FROM_PED(pedMe, Card::kPrimaryItem); return ENTITY::DOES_ENTITY_EXIST(held) != 0; });
	if (PlayerAvailable() && TASK::GET_ITEM_INTERACTION_ITEM_ID(pedMe) == item &&
		ENTITY::DOES_ENTITY_EXIST(held) && ENTITY::GET_ENTITY_MODEL(held) == Card::kPropModel)
	{
		Cd.obj = (Object)held;   // the game's prop — it deletes it when the task ends
		Cd.ownsObj = false;
		SetCardTitle(Cd.obj);
		LinkCardRenderTarget(ENTITY::GET_ENTITY_MODEL(Cd.obj));
		RefreshCardTextureAfterTransition();
	}
	else { abandonFallback(); return false; }
	Cd.examining = true;
	Cd.openedMs = RuntimeNowMs();
	return true;
}

// Card in the player's hand for the hand-in animation (no examine UI).
static bool AttachCardToHand(Ped holder)
{
	if (!LivingPed(holder) || !CreateCardObject() || !PlayerAvailable() || !LivingPed(holder)) return false;
	int bone = ENTITY::GET_ENTITY_BONE_INDEX_BY_NAME(holder, Card::kHandBone);
	if (bone < 0) return false;
	if (ENTITY::IS_ENTITY_ATTACHED(Cd.obj)) ENTITY::DETACH_ENTITY(Cd.obj, true, false);
	ENTITY::SET_ENTITY_COLLISION(Cd.obj, false, false);
	ENTITY::ATTACH_ENTITY_TO_ENTITY(Cd.obj, holder, bone, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, false, false, false, true, 0, true, false, false);
	RefreshCardTextureAfterTransition();
	Cd.inHand = true;
	return true;
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

// Object-level custom texture used by R* for documents. A new item task may initialize the material
// again on the same object. Retry for a bounded settling interval;
// this native returns void, so customApplied records an attempt, not an engine acknowledgment.
static void ApplyCardCustomTexture()
{
	if (!Card::kCardCustomTexture || !Cd.obj || !ENTITY::DOES_ENTITY_EXIST(Cd.obj) || !TargetPhotoReady()) return;
	if (Cd.customApplied && RuntimeNowMs() >= Cd.textureRefreshUntilMs) return;
	OBJECT::SET_CUSTOM_TEXTURES_ON_OBJECT(Cd.obj, joaat(C.photoTexture), 0, 0);
#ifdef BOUNTY_PHOTO_SELF_TEST
	++photoTestBindAttempts;
#endif
	Cd.customApplied = true;
}

static void RefreshCardTextureAfterTransition()
{
	Cd.customApplied = false;
	Cd.textureRefreshUntilMs = RuntimeNowMs() + Card::kTextureSettleMs;
	ApplyCardCustomTexture();
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
	ULONGLONG now = RuntimeNowMs();
	if (C.cardOpenPending && !handoff.active && CanStartInteraction())
	{
		C.cardOpenPending = false;
		if (!OpenCard(Cd.inHand)) DestroyCardObject();
	}
	if (Cd.obj && !ENTITY::DOES_ENTITY_EXIST(Cd.obj)) { DestroyCardObject(); return; }
	if (Cd.examining)
	{
		if (!OwnCardTaskRunning() && now > Cd.openedMs + 1500) { DestroyCardObject(); return; }
		PED::_SET_PED_BLACKBOARD_BOOL(pedMe, Card::kFlipBlackboard, true, -1); // the inspect task reads this while it runs
		if (!TargetPhotoReady() && !Cd.ownsObj)
		{
			// We cannot hide a game-owned fallback prop indefinitely. End only our matching task.
			DestroyCardObject(true);
			DisplaySubtitle("CONTRACT PHOTO UNAVAILABLE. PRESS I TO RETRY.");
			return;
		}
		if (Cd.obj) SetCardTitle(Cd.obj);                                     // the task resets the prop's name on start
		Hash itemState = TASK::GET_ITEM_INTERACTION_STATE(pedMe);
		if (itemState != Cd.textureItemState)
		{
			Cd.textureItemState = itemState;
			RefreshCardTextureAfterTransition(); // intro/base/flip may each initialize the material
		}
		ApplyCardCustomTexture();
		if (CardIsFlipped(itemState)) DrawCardBackPanel();
		DrawCardFace(false);   // last: it leaves the render target selected
	}
	else if (Cd.inHand)
	{
		ApplyCardCustomTexture();
		DrawCardFace(g_state == CONTRACT_DEAD || g_state == CONTRACT_PAID);
	}
}

#ifdef BOUNTY_PHOTO_SELF_TEST
static bool ProbePhotoCard(ULONGLONG started, unsigned captures)
{
	lastPhotoStage = "probe_card";
	bool opened = OpenCard();
	Object originalCard = Cd.obj;
	Ped inspector = Cd.inspectingPed;
	bool owned = Cd.ownsObj;
	LogPhotoCacheTest("card_opened", opened, started, captures, "", C.photoLookupName, originalCard, owned);
	if (!opened)
	{
		DestroyCardObject(true);
		return false;
	}
	DisplaySubtitle("PHOTO TEST: FLIP THE CARD, THEN PUT IT AWAY.");
	bool putAway = WaitUntil(60000, []
	{
		if (HUD::IS_PAUSE_MENU_ACTIVE()) return false;
		UpdateCard(); // same task-transition binding, flip panel and cleanup as normal gameplay
		return !Cd.obj;
	});
	LogPhotoCacheTest("card_put_away", putAway, started, captures, "", C.photoLookupName, originalCard, owned);
	DestroyCardObject(true); // also closes our inspection on timeout or interruption
	// Preserve the original handle across Cd reset. A cleared handle alone cannot
	// establish that the native material consumer and the inspection task retired.
	bool retired = WaitUntil(1500, [&]
	{
		return (!originalCard || !ENTITY::DOES_ENTITY_EXIST(originalCard)) &&
			(!inspector || !ENTITY::DOES_ENTITY_EXIST(inspector) ||
				!TASK::IS_PED_RUNNING_TASK_ITEM_INTERACTION(inspector));
	});
	LogPhotoCacheTest("card_removed", retired, started, captures, "", C.photoLookupName, originalCard, owned);
	return putAway && retired && PlayerAvailable();
}
#endif

// ===== [ HUMAN TARGET: SETUP ] =====
static void StartWander(Ped ped, const ContractDef& def)
{
	PED::SET_PED_CONFIG_FLAG(ped, 233, false);
	PED::SET_PED_COMBAT_ATTRIBUTES(ped, 5, false);
	TASK::SET_PED_PATH_PREFER_TO_AVOID_WATER(ped, true, def.searchRadius);
	TASK::SET_PED_PATH_MAY_ENTER_WATER(ped, false);
	TASK::TASK_WANDER_IN_AREA(ped, def.spawn, def.searchRadius, 0.0f, 0.0f, 1);
	PED::SET_PED_KEEP_TASK(ped, true);
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
		{ 437, false }, // DisableWeatherConditionPerceptionChecks
		{ 351, true  }, // DisableIntimidationBackingAway
		{ 356, true  }, // BlockRobberyInteractionEscape
	};
	for (const auto& f : kConfig) PED::SET_PED_CONFIG_FLAG(ped, f.flag, f.on);

	PED::SET_PED_COMBAT_RANGE(ped, CR_MEDIUM);
	PED::SET_PED_COMBAT_MOVEMENT(ped, 1);

	static const struct { int attr; bool on; } kCombat[] = {
		{ 114, true  }, // CA_CAN_EXECUTE_TARGET
		{ 41,  true  }, // CA_CAN_COMMANDEER_VEHICLES
		{ 125, false }, // continue pursuit after an interaction fight
		{ 21,  true  }, // CA_CAN_CHASE_TARGET_ON_FOOT
		{ 12,  true  }, // CA_BLIND_FIRE_IN_COVER
		{ 17,  false }, // CA_ALWAYS_FLEE
		{ 14,  true  }, // CA_CAN_INVESTIGATE
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

	C.weapon = (rand() % 100) >= Tune::kArmedChancePct ? joaat("WEAPON_UNARMED")
		: (rand() % 100) < Tune::kGunVsKnifePct ? WEAPON_REVOLVER_CATTLEMAN : WEAPON_MELEE_KNIFE;
	bool melee = C.weapon != WEAPON_REVOLVER_CATTLEMAN;
	PED::SET_PED_COMBAT_ATTRIBUTES(ped, 93, melee);
	PED::SET_PED_COMBAT_ATTRIBUTES(ped, 54, !melee);
	if (C.weapon != joaat("WEAPON_UNARMED"))
		WEAPON::GIVE_WEAPON_TO_PED(ped, C.weapon, melee ? 0 : 60, false, true, 0, true, 0.0f, 0.0f, ADD_REASON_DEFAULT, false, 0.0f, false);
	C.lastKnownPlayerPos = def.spawn;

	StartWander(ped, def);
}

// ===== [ HUMAN TARGET: COMBAT ] =====
// Preserve native combat when it has already started. Recovery only reissues the task;
// it never forces another weapon draw or clears an animation while the ped is getting up.
static void EnterCombat(Ped ped, bool adoptNativeCombat, bool taskRecovery)
{
	PED::SET_PED_CONFIG_FLAG(ped, 233, true);     // PedIsEnemyToPlayer — so his own AI presses the attack
	PED::SET_PED_COMBAT_ATTRIBUTES(ped, 5, true); // CA_ALWAYS_FIGHT
	if (!adoptNativeCombat)
	{
		if (!taskRecovery) WEAPON::SET_CURRENT_PED_WEAPON(ped, C.weapon, false, WEAPON_ATTACH_POINT_HAND_PRIMARY, false, false);
		TASK::TASK_COMBAT_PED(ped, pedMe, 0, 0);
	}
	PED::SET_PED_KEEP_TASK(ped, true); // the combat task sticks so he can't drop into flee / cower
}

// First contact: he turns hostile when the player aims at or intimidates him while he is looking
// at the player, or hurts him.
static bool PlayerProvoked(Ped ped)
{
	bool looking = PED::IS_PED_HEADTRACKING_PED(ped, pedMe) != 0;
	return (looking && (PLAYER::IS_PLAYER_FREE_AIMING_AT_ENTITY(me, ped) || PED::_IS_PED_INTIMIDATED(ped)))
		&& Within(playerPos, C.targetPos, Tune::kReAggroSightDist) &&
		ENTITY::HAS_ENTITY_CLEAR_LOS_TO_ENTITY(ped, pedMe, 17);
}

// ===== [ HUMAN TARGET: PER-FRAME AI ] =====
// Ped tasks are issued on state transitions only — never re-issued per frame (that thrashes the ped AI).
static void UpdateHumanTarget(Ped ped, const ContractDef& def)
{
	TargetAI::Config config;
	config.acquireSightDist = Tune::kReAggroSightDist;
	config.retainSightDist = Tune::kRetainSightDist;
	config.lossGraceMs = Tune::kDeAggroGraceMs;
	config.searchMs = Tune::kTargetSearchMs;
	TargetAI::Observation observation;
	observation.nowMs = RuntimeNowMs();
	observation.distance = std::sqrt(DistSq(playerPos, C.targetPos));
	observation.clearLineOfSight = observation.distance <= config.retainSightDist && ENTITY::HAS_ENTITY_CLEAR_LOS_TO_ENTITY(ped, pedMe, 17);
	observation.provoked = C.damagedByPlayer || PlayerProvoked(ped);
	observation.nativeInCombat = PED::IS_PED_IN_COMBAT(ped, pedMe) != 0;
	int taskStatus = TASK::GET_SCRIPT_TASK_STATUS(ped, joaat("SCRIPT_TASK_COMBAT"), true);
	observation.combatTaskActive = observation.nativeInCombat || taskStatus == 0 || taskStatus == 1;
	observation.canAct = !PED::IS_PED_RAGDOLL(ped) && !TASK::IS_PED_GETTING_UP(ped) && !PED::IS_PED_HOGTIED(ped) && !PED::IS_PED_LASSOED(ped);
	TargetAI::Decision decision = TargetAI::Step(C.ai, config, observation);
	if (decision.updateLastKnownPosition) C.lastKnownPlayerPos = playerPos;
	switch (decision.action)
	{
	case TargetAI::Action::Engage: EnterCombat(ped, false, decision.taskRecovery); break;
	case TargetAI::Action::AdoptCombat: EnterCombat(ped, true, false); break;
	case TargetAI::Action::Search:
		PED::SET_PED_CONFIG_FLAG(ped, 233, false);
		PED::SET_PED_COMBAT_ATTRIBUTES(ped, 5, false);
		TASK::TASK_GO_TO_COORD_ANY_MEANS(ped, C.lastKnownPlayerPos, 1.5f, 0, false, 0, 0.0f);
		break;
	case TargetAI::Action::Wander: StartWander(ped, def); break;
	case TargetAI::Action::None: break;
	}
}

const TargetBehavior kHumanTarget = { SetupHumanTarget, UpdateHumanTarget };

// ===== [ PAYOUT ] =====
// Longer contracts pay more (linear up to kFullPayMinutes); getting wanted after the crime cuts the pay.
static int ComputePayoutCents()
{
	ULONGLONG end = C.photoMs ? C.photoMs : RuntimeNowMs();
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
		if (PED::IS_PED_IN_COMBAT(C.target, pedMe) || C.damagedByPlayer || C.ai.state == TargetAI::State::Engaged)
		{
			C.crimeMs = RuntimeNowMs();
			C.bountyAtCrime = LAW::GET_BOUNTY(me);
		}
	}
	if (C.crimeMs && !C.gotWanted &&
		(LAW::IS_LAW_INCIDENT_ACTIVE(me) || LAW::GET_WANTED_SCORE(me) > 0 || LAW::GET_BOUNTY(me) > C.bountyAtCrime))
	{
		C.gotWanted = true;
	}
}

// ===== [ CONTRACT LIFECYCLE ] =====
static void ClearContract(bool deleteTarget)
{
	StopHandoff();
	RemoveBlip(C.searchBlip);
	RemoveBlip(C.targetBlip);
	PLAYER::_CLEAR_PED_EAGLE_EYE_TRAILS_FOR_PLAYER(me);
	if (TargetExists()) PLAYER::_UNREGISTER_EAGLE_EYE_FOR_ENTITY(me, C.target);
	DestroyCardObject(true);
	ReleaseTargetPhoto();
	if (C.cashObj && ENTITY::DOES_ENTITY_EXIST(C.cashObj)) OBJECT::DELETE_OBJECT(&C.cashObj);
	if (C.def && C.def->onCleanup) C.def->onCleanup();
	if (deleteTarget) RequestOwnedPedCleanup(C.target);
	else ReleaseOwnedPed(C.target);
	C = ActiveContract();
	ResetPrompt(giverPrompt);
	ResetPrompt(camPrompt);
	g_state = CONTRACT_NONE;
}

// Rolls a random contract, photographs and spawns its target. False if nothing could be spawned.
static bool StartContract()
{
	lastStartFailure = ContractStartFailure::None;
	lastPhotoStage = "none";
	ClearContract(true);
	for (int attempt = 0; attempt < Tune::kSpawnAttempts; ++attempt)
	{
		if (!PlayerAvailable())
		{
			lastStartFailure = ContractStartFailure::Interrupted;
			LogContractStartFailure(0, attempt + 1);
			break;
		}
		const ContractDef& def = kContracts[rand() % kContractCount];
		Hash model = def.models.list[rand() % def.models.count];
		Ped ped = SpawnTargetWithPhoto(model, def);
		if (!ped)
		{
			// Capture attempts log their own texture state before releasing it.
			if (lastStartFailure != ContractStartFailure::PortraitFailed &&
				lastStartFailure != ContractStartFailure::PhotoDiagnosticComplete) LogContractStartFailure(model, attempt + 1);
			// Capture already retried this subject. Do not multiply capture timeouts by rerolling models.
			if (lastStartFailure == ContractStartFailure::Interrupted || lastStartFailure == ContractStartFailure::PortraitFailed ||
				lastStartFailure == ContractStartFailure::PedPoolFull || lastStartFailure == ContractStartFailure::CleanupPending ||
				lastStartFailure == ContractStartFailure::PhotoDiagnosticComplete) break;
			if (attempt + 1 < Tune::kSpawnAttempts) WAIT(0);
			continue;
		}
		if (!PlayerAvailable())
		{
			RequestOwnedPedCleanup(ped);
			ReleaseTargetPhoto();
			lastStartFailure = ContractStartFailure::Interrupted;
			LogContractStartFailure(model, attempt + 1);
			break;
		}

		C.def = &def;
		C.target = ped;
		C.targetPos = def.spawn;
		C.startMs = RuntimeNowMs();
		def.behavior->setup(ped, def);
		if (def.onSpawned) def.onSpawned(def);
		AddSearchBlip();
		g_state = CONTRACT_UNKNOWN;
		lastStartFailure = ContractStartFailure::None;
		return true;
	}
	return false;
}

static void UpdateTargetAI()
{
	if (!C.def || !TargetExists() || PED::IS_PED_DEAD_OR_DYING(C.target, true)) return;
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
	bool hurt  = C.damagedByPlayer;
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
	Hash weapon = 0;
	if (!WEAPON::GET_CURRENT_PED_WEAPON(pedMe, &weapon, true, WEAPON_ATTACH_POINT_HAND_PRIMARY, false)) return false;
	Hash context = PAD::_GET_CURRENT_CONTROL_CONTEXT(4);
	return (weapon == joaat("WEAPON_KIT_CAMERA") || weapon == joaat("WEAPON_KIT_CAMERA_ADVANCED")) &&
		(context == joaat("PHOTOCAMERAINUSE") || context == joaat("ADVANCEDPHOTOCAMERA"));
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
	HUD::_UI_PROMPT_REGISTER_END(camPrompt);
	ShowPrompt(camPrompt, false);
}

static void CheckTargetDeath()
{
	CreateCameraPrompt(); // lazily, on the first FOUND frame — as it always has been
	ShowPrompt(camPrompt, false);
	if (!PED::IS_PED_DEAD_OR_DYING(C.target, true)) return;

	if (!C.corpseBlipPlaced)
	{
		RemoveBlip(C.targetBlip);
		DisplaySubtitle("TAKE PICTURE OF CORPSE");
		AddCorpseBlip();
		C.corpseBlipPlaced = true;
	}
	if (MAP::DOES_BLIP_EXIST(C.targetBlip)) MAP::SET_BLIP_COORDS(C.targetBlip, C.targetPos);

	// Making our prompt the ACTIVE prompt in the native CAMERA_ITEM_GROUP this frame renders it inside the
	// handheld camera AND intercepts the take-photo input: the corpse shot triggers our prompt instead of
	// the native capture, so the game never saves it. Once we reach CONTRACT_DEAD this stops running and
	// the native shutter works (and saves) normally again.
	if (!IsInHandheldCamera() || !Within(playerPos, C.targetPos, Tune::kCorpsePhotoDistance) ||
		!ENTITY::IS_ENTITY_ON_SCREEN(C.target) || !ENTITY::HAS_ENTITY_CLEAR_LOS_TO_ENTITY(pedMe, C.target, 17)) return;

	ShowPrompt(camPrompt, true);
	HUD::_UI_PROMPT_SET_ACTIVE_GROUP_THIS_FRAME(camGroup, "CAM_CONG_HC", 1, 0, 0, 0);
	if (HUD::_UI_PROMPT_IS_JUST_PRESSED(camPrompt))
	{
		DisplaySubtitle("RETURN TO CLERK");
		AUDIO::PLAY_SOUND_FRONTEND("take_photo", "Photo_Mode_Sounds", true, 0);
		ShowPrompt(camPrompt, false);
		RemoveBlip(C.targetBlip);
		C.photoMs = RuntimeNowMs();
		g_state = CONTRACT_DEAD;
	}
}

// ===== [ HAND-IN: PHOTO FOR CASH ] =====
static void SettlePayment()
{
	if (g_state != CONTRACT_PAID || C.paymentCredited) return;
	C.paymentCredited = true;
	MONEY::_MONEY_INCREMENT_CASH_BALANCE(C.payoutCents, 0);
	char money[16], message[64];
	FormatMoney(money, sizeof money, C.payoutCents);
	sprintf_s(message, "REWARD RECEIVED: %s", money);
	DisplaySubtitle(message);
	ClearContract(false);
	giverCooldownUntilMs = RuntimeNowMs() + Tune::kGiverCooldownMs;
}

static void UpdatePayment()
{
	ULONGLONG now = RuntimeNowMs();
	ShowPrompt(giverPrompt, false);
	if (!C.cashSpawned)
	{
		C.cashSpawned = true;
		Vector3 giver = ENTITY::DOES_ENTITY_EXIST(C.payingGiver) ? ENTITY::GET_ENTITY_COORDS(C.payingGiver, true, false) : playerPos;
		Vector3 counter((giver.x + playerPos.x) * 0.5f, (giver.y + playerPos.y) * 0.5f, giver.z + Tune::kCounterHeight);
		// An inert prop cannot independently award cash. Every collection/fallback uses one credit path.
		if (LoadModel(Card::kCashModel))
		{
			C.cashObj = OBJECT::CREATE_OBJECT(Card::kCashModel, counter, true, true, false, false, false);
			STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(Card::kCashModel);
		}

		char money[16], msg[64];
		FormatMoney(money, sizeof money, C.payoutCents);
		if (!C.cashObj || !ENTITY::DOES_ENTITY_EXIST(C.cashObj))
		{
			// No pickup could be placed — pay directly rather than short the player.
			SettlePayment();
			return;
		}
		sprintf_s(msg, "TAKE YOUR PAYMENT: %s", money);
		DisplaySubtitle(msg);
		ResetPrompt(giverPrompt);
		giverCooldownUntilMs = now + Tune::kGiverCooldownMs;
		return;
	}

	if (!ENTITY::DOES_ENTITY_EXIST(C.cashObj) || now - C.handInStartMs >= Tune::kCashTimeoutMs)
	{
		SettlePayment();
		return;
	}
	if (now < giverCooldownUntilMs || !CanStartInteraction() ||
		!Within(playerPos, ENTITY::GET_ENTITY_COORDS(C.cashObj, true, false), 2.5f)) return;
	Hash group = joaat("BOUNTY_CONTRACT_PAYMENT");
	HUD::_UI_PROMPT_SET_GROUP(giverPrompt, group, 0);
	HUD::_UI_PROMPT_SET_TEXT(giverPrompt, Literal("Take Payment"));
	ShowPrompt(giverPrompt, true);
	HUD::_UI_PROMPT_SET_ACTIVE_GROUP_THIS_FRAME(group, Literal("Contract Payment"), 1, 0, 0, 0);
	if (HUD::_UI_PROMPT_HAS_HOLD_MODE_COMPLETED(giverPrompt)) SettlePayment();
}

// ===== [ GIVER (CLERK) PROMPT ] =====
static void CreateGiverPrompt()
{
	giverPrompt = HUD::_UI_PROMPT_REGISTER_BEGIN();
	HUD::_UI_PROMPT_SET_CONTROL_ACTION(giverPrompt, 0x620A6C5E);
	HUD::_UI_PROMPT_SET_STANDARDIZED_HOLD_MODE(giverPrompt, SHORT_TIMED_EVENT_MP);
	HUD::_UI_PROMPT_REGISTER_END(giverPrompt);
	ShowPrompt(giverPrompt, false);
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

// In the original Pay Alden scene the *_player clip is the donor and trainworker is the receiver.
// Keep the verified role reversal when the clerk gives the player a contract.
namespace HandoffAnim
{
	constexpr const char* donorDict = "script_rc@chrb@ig1_visit_clerk";
	constexpr const char* donorClip = "arthur_gives_money_player";
	constexpr const char* receiverDict = "script_rc@chrb@ig_1_waitinginline";
	constexpr const char* receiverClip = "gives_money_trainworker";
}

static void StopHandoff()
{
	if (!handoff.active) return;
	Ped donor = handoff.payout ? handoff.player : handoff.giver;
	Ped receiver = handoff.payout ? handoff.giver : handoff.player;
	// Stop only our clips. The clerk's base scenario stays alive under the upper-body overlay.
	if (ENTITY::DOES_ENTITY_EXIST(donor)) TASK::STOP_ANIM_TASK(donor, HandoffAnim::donorDict, HandoffAnim::donorClip, -4.0f);
	if (ENTITY::DOES_ENTITY_EXIST(receiver)) TASK::STOP_ANIM_TASK(receiver, HandoffAnim::receiverDict, HandoffAnim::receiverClip, -4.0f);
	STREAMING::REMOVE_ANIM_DICT(HandoffAnim::donorDict);
	STREAMING::REMOVE_ANIM_DICT(HandoffAnim::receiverDict);
	handoff = HandoffRuntime();
	giverCooldownUntilMs = RuntimeNowMs() + Tune::kGiverCooldownMs;
}

static bool BeginHandoff(Ped giver, bool payout)
{
	if (!CanStartInteraction() || !LivingPed(giver)) return false;
	if (!EnsureTargetPhotoReady()) return false;
	STREAMING::REQUEST_ANIM_DICT(HandoffAnim::donorDict);
	STREAMING::REQUEST_ANIM_DICT(HandoffAnim::receiverDict);
	bool loaded = WaitUntil(Tune::kStreamTimeoutMs, [&] {
		return !LivingPed(giver) || (STREAMING::HAS_ANIM_DICT_LOADED(HandoffAnim::donorDict) &&
			STREAMING::HAS_ANIM_DICT_LOADED(HandoffAnim::receiverDict));
	});
	if (!loaded || !CanStartInteraction() || !LivingPed(giver) ||
		!Within(ENTITY::GET_ENTITY_COORDS(pedMe, true, false), ENTITY::GET_ENTITY_COORDS(giver, true, false), 3.0f) ||
		!AttachCardToHand(payout ? pedMe : giver))
	{
		STREAMING::REMOVE_ANIM_DICT(HandoffAnim::donorDict);
		STREAMING::REMOVE_ANIM_DICT(HandoffAnim::receiverDict);
		DestroyCardObject();
		return false;
	}
	// Creating the card can itself yield for model streaming; validate again before issuing tasks.
	if (!CanStartInteraction() || !LivingPed(giver) || PED::IS_PED_IN_COMBAT(giver, 0) ||
		!Within(ENTITY::GET_ENTITY_COORDS(pedMe, true, false), ENTITY::GET_ENTITY_COORDS(giver, true, false), 3.0f))
	{
		STREAMING::REMOVE_ANIM_DICT(HandoffAnim::donorDict);
		STREAMING::REMOVE_ANIM_DICT(HandoffAnim::receiverDict);
		DestroyCardObject();
		return false;
	}
	ApplyCardCustomTexture();
	handoff.active = true;
	handoff.payout = payout;
	handoff.giver = giver;
	handoff.player = pedMe;
	float donorDuration = ENTITY::GET_ANIM_DURATION(HandoffAnim::donorDict, HandoffAnim::donorClip);
	float receiverDuration = ENTITY::GET_ANIM_DURATION(HandoffAnim::receiverDict, HandoffAnim::receiverClip);
	float duration = donorDuration > receiverDuration ? donorDuration : receiverDuration;
	if (!(duration > 0.0f && duration < 30.0f)) duration = 6.0f;
	handoff.config.timeoutMs = static_cast<ULONGLONG>(duration * 1000.0f) + 2000;
	handoff.config.transferPhase = Tune::kHandoffTransferPhase;
	const int giverFlags = AF_FORCE_START | AF_UPPERBODY | AF_SECONDARY;
	const int playerFlags = AF_FORCE_START;
	const int ikFlags = AIK_DISABLE_ARM_IK | AIK_DISABLE_TORSO_REACT_IK | AIK_DISABLE_TORSO_IK | AIK_DISABLE_HEAD_IK | AIK_DISABLE_LEG_IK;
	// Both dictionaries and the textured prop are ready before either task starts.
	TASK::TASK_PLAY_ANIM(payout ? pedMe : giver, HandoffAnim::donorDict, HandoffAnim::donorClip,
		4.0f, -4.0f, -1, payout ? playerFlags : giverFlags, 0.0f, false, ikFlags, false, nullptr, false);
	TASK::TASK_PLAY_ANIM(payout ? giver : pedMe, HandoffAnim::receiverDict, HandoffAnim::receiverClip,
		4.0f, -4.0f, -1, payout ? giverFlags : playerFlags, 0.0f, false, ikFlags, false, nullptr, false);
	handoff.progress.startedMs = RuntimeNowMs();
	return true;
}

static void UpdateHandoff()
{
	if (!handoff.active) return;
	Handoff::Observation observation;
	observation.nowMs = RuntimeNowMs();
	observation.payout = handoff.payout;
	observation.actorsValid = PlayerAvailable() && handoff.player == pedMe && LivingPed(handoff.giver) &&
		Cd.obj && ENTITY::DOES_ENTITY_EXIST(Cd.obj) && !PED::IS_PED_RAGDOLL(pedMe) &&
		!PED::IS_PED_IN_COMBAT(handoff.giver, 0) && !PED::IS_PED_IN_COMBAT(pedMe, 0) &&
		Within(playerPos, ENTITY::GET_ENTITY_COORDS(handoff.giver, true, false), 3.5f);
	if (observation.actorsValid)
	{
		const char* giverDict = handoff.payout ? HandoffAnim::receiverDict : HandoffAnim::donorDict;
		const char* giverClip = handoff.payout ? HandoffAnim::receiverClip : HandoffAnim::donorClip;
		const char* playerDict = handoff.payout ? HandoffAnim::donorDict : HandoffAnim::receiverDict;
		const char* playerClip = handoff.payout ? HandoffAnim::donorClip : HandoffAnim::receiverClip;
		observation.giverPlaying = ENTITY::IS_ENTITY_PLAYING_ANIM(handoff.giver, giverDict, giverClip, 3) != 0;
		observation.playerPlaying = ENTITY::IS_ENTITY_PLAYING_ANIM(pedMe, playerDict, playerClip, 3) != 0;
		observation.giverPhase = ENTITY::_GET_ENTITY_ANIM_CURRENT_TIME(handoff.giver, giverDict, giverClip);
		observation.playerPhase = ENTITY::_GET_ENTITY_ANIM_CURRENT_TIME(pedMe, playerDict, playerClip);
		observation.giverFinished = ENTITY::HAS_ENTITY_ANIM_FINISHED(handoff.giver, giverDict, giverClip, 3) != 0;
		observation.playerFinished = ENTITY::HAS_ENTITY_ANIM_FINISHED(pedMe, playerDict, playerClip, 3) != 0;
	}
	Handoff::Decision decision = Handoff::Step(handoff.progress, handoff.config, observation);
	if (decision.transfer && !AttachCardToHand(handoff.payout ? handoff.giver : pedMe)) decision.cancel = true;
	if (decision.cancel)
	{
		bool payout = handoff.payout;
		StopHandoff();
		DestroyCardObject();
		if (payout) DisplaySubtitle("HANDOVER INTERRUPTED. COLLECT PAYMENT AT THE CLERK.");
		else { C.cardOpenPending = false; DisplaySubtitle("CONTRACT READY. PRESS I TO INSPECT."); }
		return;
	}
	if (decision.complete)
	{
		bool payout = handoff.payout;
		Ped giver = handoff.giver;
		StopHandoff();
		if (payout)
		{
			DestroyCardObject();
			C.payingGiver = giver;
			C.handInStartMs = RuntimeNowMs();
			g_state = CONTRACT_PAID;
		}
		else C.cardOpenPending = true;
		return;
	}
	// Frame-scoped controls need no persistent player-control flag to restore after cancellation.
	for (Hash input : { INPUT_MOVE_LR, INPUT_MOVE_UD, INPUT_SPRINT, INPUT_JUMP, INPUT_ATTACK, INPUT_AIM })
		PAD::DISABLE_CONTROL_ACTION(0, input, true);
}

static void UpdateGiverPrompt()
{
	if (handoff.active || Cd.obj || !CanStartInteraction() || RuntimeNowMs() < giverCooldownUntilMs)
	{
		ShowPrompt(giverPrompt, false);
		return;
	}
	Entity aimed = 0;
	Ped giver = 0;
	const GiverSpot* spot = nullptr;
	if (PLAYER::GET_PLAYER_TARGET_ENTITY(me, &aimed) && ENTITY::IS_ENTITY_A_PED(aimed))
	{
		giver = ENTITY::GET_PED_INDEX_FROM_ENTITY_INDEX(aimed);
		if (LivingPed(giver) && !PED::IS_PED_IN_COMBAT(giver, pedMe)) spot = FindGiverSpot(giver);
	}
	if (!spot)
	{
		ResetPrompt(giverPrompt);
		return;
	}

	HUD::_UI_PROMPT_SET_GROUP(giverPrompt, HUD::_UI_PROMPT_GET_GROUP_ID_FOR_TARGET_ENTITY(giver), 0);
	const char* text = g_state == CONTRACT_NONE ? "Get Contract"
	                 : g_state == CONTRACT_DEAD ? "Collect Payment"
	                 :                            "End Contract";
	HUD::_UI_PROMPT_SET_TEXT(giverPrompt, Literal(text));
	ShowPrompt(giverPrompt, true);
	if (!HUD::_UI_PROMPT_HAS_HOLD_MODE_COMPLETED(giverPrompt)) return;
	ResetPrompt(giverPrompt);
	giverCooldownUntilMs = RuntimeNowMs() + Tune::kGiverCooldownMs;

	switch (g_state)
	{
	case CONTRACT_NONE:
	{
		DisplaySubtitle("PREPARING CONTRACT");
		if (StartContract())
		{
			if (!BeginHandoff(giver, false)) C.cardOpenPending = true;
			DisplaySubtitle("FIND THE TARGET");
		}
		else
		{
			ReportContractStartFailure();
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
		if (!BeginHandoff(giver, true)) DisplaySubtitle("HANDOVER UNAVAILABLE. TRY THE CLERK AGAIN.");
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
	if (pedMe && ENTITY::DOES_ENTITY_EXIST(pedMe)) playerPos = ENTITY::GET_ENTITY_COORDS(pedMe, true, false);
	if (TargetExists()) C.targetPos = ENTITY::GET_ENTITY_COORDS(C.target, true, false);
}

static void StartRemoteContract()
{
	Ped requestedPlayer = pedMe;
	DisplaySubtitle("PREPARING CONTRACT");
	// Run creation on a fresh game frame after the keyboard event, then refresh the player snapshot.
	WAIT(0);
	UpdatePlayer();
	if (pedMe != requestedPlayer || !CanStartInteraction())
	{
		lastStartFailure = ContractStartFailure::Interrupted;
		LogContractStartFailure(0, 0);
		ReportContractStartFailure();
		return;
	}
	if (StartContract()) { C.cardOpenPending = true; DisplaySubtitle("FIND THE TARGET"); }
	else ReportContractStartFailure();
}

void ScriptMain()
{
	srand((unsigned)GetTickCount64());
	CreateGiverPrompt();
	while (true)
	{
		MaintainOwnedPedCleanup();
		Ped previousPlayer = pedMe;
		UpdatePlayer();
		SetRuntimePaused(!PlayerAvailable() || HUD::IS_PAUSE_MENU_ACTIVE() || CAMERA::IS_SCREEN_FADED_OUT());
		MaintainPortraitAndCard();
		bool bypassPressed = IsKeyJustUp(Tune::kBypassClerkKey);
		bool inspectPressed = IsKeyJustUp(Tune::kInspectCardKey);
		if (!PlayerAvailable() || (previousPlayer && previousPlayer != pedMe))
		{
			StopHandoff();
			DestroyCardObject(true);
			C.cardOpenPending = false;
			ResetPrompt(giverPrompt);
			ResetPrompt(camPrompt);
			WAIT(0);
			continue;
		}
		if (HUD::IS_PAUSE_MENU_ACTIVE() || CAMERA::IS_SCREEN_FADED_OUT())
		{
			ShowPrompt(giverPrompt, false);
			ShowPrompt(camPrompt, false);
			WAIT(0);
			continue;
		}

		// A contract whose target vanished (deleted by another script, fell out of the world) can never
		// be completed — end it instead of leaving stale blips on the map.
		if ((g_state == CONTRACT_UNKNOWN || g_state == CONTRACT_FOUND) && !TargetExists())
		{
			DisplaySubtitle("TARGET LOST");
			ClearContract(false);
		}

		// U bypasses the clerk for a new contract; an existing hunt is never silently discarded.
		if (bypassPressed && !handoff.active && !Cd.obj && CanStartInteraction())
		{
			if (g_state == CONTRACT_NONE)
			{
				StartRemoteContract();
			}
			else if (ContractActive()) C.cardOpenPending = true;
		}
		if (inspectPressed && ContractActive() && !handoff.active && !Cd.obj) C.cardOpenPending = true;

		UpdateHandoff();
		// Select the contract state only AFTER processing a prompt that can clear/change it.
		if (g_state != CONTRACT_PAID) UpdateGiverPrompt();
		if (!PlayerAvailable()) { WAIT(0); continue; }
		if (TargetExists())
		{
			C.targetPos = ENTITY::GET_ENTITY_COORDS(C.target, true, false);
			C.damagedByPlayer = ENTITY::HAS_ENTITY_BEEN_DAMAGED_BY_ENTITY(C.target, pedMe, true, true) != 0;
		}
		ShowPrompt(camPrompt, false);

		switch (g_state)
		{
		case CONTRACT_NONE:
			break;
		case CONTRACT_UNKNOWN:
			UpdateTrails();
			UpdateTargetAI();
			UpdateCrimeTracking();
			CheckTargetFound();
			break;
		case CONTRACT_FOUND:
			UpdateTrails();
			UpdateTargetAI();
			UpdateCrimeTracking();
			CheckTargetDeath();
			break;
		case CONTRACT_DEAD:
			UpdateTrails();
			UpdateCrimeTracking();
			break;
		case CONTRACT_PAID:
			UpdatePayment();
			break;
		}
		// Every consumer sees the same damage event, then history is cleared so it cannot keep
		// re-triggering aggression through walls for the rest of the contract.
		if (C.damagedByPlayer && TargetExists()) ENTITY::CLEAR_ENTITY_LAST_DAMAGE_ENTITY(C.target);
		C.damagedByPlayer = false;
		UpdateCard(); // render-target drawing must remain last
		WAIT(0);
	}
}
