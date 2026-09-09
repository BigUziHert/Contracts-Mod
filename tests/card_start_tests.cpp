// Run the real readiness helpers and OpenCard against a frame-driven native world.
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>

using Ped = int;
using Player = int;
using Entity = int;
using Object = int;
using Hash = unsigned;
using DWORD = std::uint32_t;
using ULONGLONG = std::uint64_t;
constexpr Ped kPlayer = 42;
constexpr Object kCard = 100;
static Ped pedMe = kPlayer;
static Player me = 1;
static unsigned checks = 0;
static void Check(bool value, const char* message)
{
    ++checks;
    if (!value) { std::fprintf(stderr, "FAILED: %s\n", message); std::exit(EXIT_FAILURE); }
}
constexpr Hash Joaat(const char* text)
{
    Hash hash = 0;
    for (; *text; ++text)
    {
        unsigned c = static_cast<unsigned char>(*text);
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        hash += c; hash += hash << 10; hash ^= hash >> 6;
    }
    hash += hash << 3; hash ^= hash >> 11; hash += hash << 15;
    return hash;
}
static Hash joaat(const char* text) { return Joaat(text); }
constexpr Hash kUnarmed = Joaat("WEAPON_UNARMED");
constexpr Hash kKnife = Joaat("WEAPON_MELEE_KNIFE");
constexpr Hash kRevolver = Joaat("WEAPON_REVOLVER_CATTLEMAN");
constexpr Hash kRifle = Joaat("WEAPON_RIFLE_BOLTACTION");
static struct World
{
    ULONGLONG now = 1000, hideAt = 0, ownedAt = 0, fallbackAt = 0, createdAt = 0;
    Ped currentPlayer = kPlayer;
    bool playerExists = true, playerDead = false, controlled = true, onFoot = true;
    bool paused = false, faded = false, combat = false, ragdoll = false, gettingUp = false;
    bool hogtied = false, lassoed = false, carrying = false, changing = false;
    bool photoReady = true, createSucceeds = true, cardExists = false, attached = false;
    bool ownedStarts = true, fallbackStarts = true, itemRunning = false;
    Hash itemId = 0;
    Entity primaryItem = 0;
    std::array<Hash, 2> weapons = {kUnarmed, 0};
    std::array<unsigned, 2> weaponReads = {};
    unsigned waits = 0, hides = 0, ownedTasks = 0, fallbackTasks = 0;
    unsigned creates = 0, destroys = 0, detaches = 0, maintenance = 0, subtitles = 0;
    unsigned blackboardWrites = 0;
    std::function<void()> onFrame, onHide, onCreate, onDestroy, onPhoto;
} w;
static bool EmptyHands()
{
    for (Hash weapon : w.weapons) if (weapon && weapon != kUnarmed) return false;
    return !w.changing && !w.carrying;
}
static void CheckSafeTask(Ped ped)
{
    Check(ped == kPlayer && w.currentPlayer == ped && w.playerExists && !w.playerDead,
        "inspection starts only for the living current player");
    Check(EmptyHands(), "inspection cannot overlap held weapons, carried items, or holster animations");
    Check(w.controlled && w.onFoot && !w.combat && !w.ragdoll && !w.gettingUp &&
        !w.hogtied && !w.lassoed && !w.paused && !w.faded && !w.itemRunning,
        "inspection preserves the normal interaction eligibility gate");
}
namespace ENTITY
{
static bool DOES_ENTITY_EXIST(Entity entity)
{
    return (entity == kPlayer && w.playerExists) || (entity == kCard && w.cardExists);
}
static bool IS_ENTITY_ATTACHED(Entity entity)
{
    Check(entity == kCard, "attachment query uses our card"); return w.attached;
}
static void DETACH_ENTITY(Entity entity, bool dynamic, bool collision)
{
    Check(entity == kCard && dynamic && !collision, "detach uses the existing card behavior");
    Check(EmptyHands(), "handoff card stays attached until the player is ready");
    ++w.detaches; w.attached = false;
}
static Hash GET_ENTITY_MODEL(Entity entity)
{
    Check(entity == kCard && w.cardExists, "fallback verifies the actual held prop");
    return Joaat("p_cs_photonudie05x_4x6");
}
}
namespace PLAYER
{
static Ped PLAYER_PED_ID() { return w.currentPlayer; }
static bool IS_PLAYER_CONTROL_ON(Player player) { Check(player == me, "checks our player control"); return w.controlled; }
}
namespace PED
{
static bool IS_PED_DEAD_OR_DYING(Ped ped, bool dying)
{
    Check(ped == kPlayer && dying, "readiness checks our player's life state"); return w.playerDead;
}
static bool IS_PED_ON_FOOT(Ped) { return w.onFoot; }
static bool IS_PED_IN_COMBAT(Ped, Ped) { return w.combat; }
static bool IS_PED_RAGDOLL(Ped) { return w.ragdoll; }
static bool IS_PED_HOGTIED(Ped) { return w.hogtied; }
static bool IS_PED_LASSOED(Ped) { return w.lassoed; }
static bool IS_PED_CARRYING_SOMETHING(Ped ped)
{
    Check(ped == kPlayer && w.playerExists && !w.playerDead && w.currentPlayer == ped,
        "carrying query follows current-player validation");
    return w.carrying;
}
static void _SET_PED_BLACKBOARD_BOOL(Ped ped, const char*, bool, int)
{
    Check(ped == kPlayer, "flip prompt belongs to the inspector"); ++w.blackboardWrites;
}
}
namespace HUD
{
static bool IS_PAUSE_MENU_ACTIVE() { return w.paused; }
static bool DOES_TEXT_LABEL_EXIST(const char*) { return false; }
}
namespace CAMERA { static bool IS_SCREEN_FADED_OUT() { return w.faded; } }
namespace WEAPON
{
static bool GET_CURRENT_PED_WEAPON(Ped ped, Hash* weapon, bool inHand, int hand, bool inventory)
{
    Check(ped == kPlayer && inHand && !inventory && hand >= 0 && hand < 2,
        "weapon readiness reads both actual hand slots with production native flags");
    ++w.weaponReads[hand]; *weapon = w.weapons[hand]; return *weapon != 0;
}
static bool _IS_WEAPON_HOLSTER_STATE_CHANGING(Ped ped)
{
    Check(ped == kPlayer, "holster state belongs to the inspector"); return w.changing;
}
static void _HIDE_PED_WEAPONS(Ped ped, int mode, bool immediately)
{
    Check(ped == kPlayer && w.currentPlayer == ped && mode == 2 && !immediately,
        "put-away requests the normal both-hand weapon transition");
    Check(!w.changing, "put-away does not restart a holster or draw already underway");
    ++w.hides; w.hideAt = w.now;
    if (w.onHide) w.onHide();
}
}
namespace TASK
{
static bool IS_PED_GETTING_UP(Ped) { return w.gettingUp; }
static bool IS_PED_RUNNING_TASK_ITEM_INTERACTION(Ped) { return w.itemRunning; }
static Hash GET_ITEM_INTERACTION_ITEM_ID(Ped) { return w.itemId; }
static Entity _GET_ITEM_INTERACTION_ENTITY_FROM_PED(Ped, Hash slot)
{
    Check(slot == Joaat("primaryItem"), "inspection ownership reads the primary item slot");
    return w.primaryItem;
}
static void _TASK_ITEM_INTERACTION_2(Ped ped, Hash, Entity object, Hash slot, Hash, int, int, float)
{
    CheckSafeTask(ped);
    Check(object == kCard && w.cardExists && slot == Joaat("primaryItem"), "owned task uses the created card");
    ++w.ownedTasks; w.ownedAt = w.now;
    if (w.ownedStarts) { w.itemRunning = true; w.itemId = Joaat("generic_photograph"); w.primaryItem = kCard; }
}
static void START_TASK_ITEM_INTERACTION(Ped ped, Hash item, Hash, int, int, float)
{
    CheckSafeTask(ped);
    ++w.fallbackTasks; w.fallbackAt = w.now;
    if (w.fallbackStarts) { w.itemRunning = true; w.itemId = item; w.primaryItem = kCard; w.cardExists = true; }
}
static void _SET_ITEM_INTERACTION_STATE(Ped, Hash, float) { w.itemRunning = false; w.primaryItem = 0; }
}
static ULONGLONG GetTickCount64() { return w.now; }
static ULONGLONG RuntimeNowMs() { return w.now; }
static void MaintainOwnedPedCleanup() { ++w.maintenance; }
static void SetRuntimePaused(bool) {}
static void TraceCardInspection() {}
static void MaintainPortraitAndCard() {}
static void WAIT(int delay)
{
    Check(delay == 0, "readiness yields ordinary game frames");
    ++w.waits; w.now += 10;
    if (w.onFrame) w.onFrame();
}
static bool EnsureTargetPhotoReady() { if (w.onPhoto) w.onPhoto(); return w.photoReady; }
static bool TargetPhotoReady() { return w.photoReady; }
static void DisplaySubtitle(const char*) { ++w.subtitles; }
static void RefreshCardTextureAfterTransition() {}
static void SetCardTitle(Object) {}
static void LinkCardRenderTarget(Hash) {}
static bool CreateCardObject();
static void DestroyCardObject(bool cancelInspection = false);

#include "card_start_under_test.h"

static bool CreateCardObject()
{
    ++w.creates; w.createdAt = w.now;
    if (w.onCreate) w.onCreate();
    if (!w.createSucceeds) return false;
    Cd.obj = kCard; Cd.ownsObj = true; w.cardExists = true;
    return true;
}
static void DestroyCardObject(bool)
{
    ++w.destroys; w.cardExists = false; w.itemRunning = false; w.primaryItem = 0;
    Cd = {};
    if (w.onDestroy) w.onDestroy();
}
static void Reset() { w = {}; Cd = {}; pedMe = kPlayer; }
static void TimedHolster(ULONGLONG firstHandMs, ULONGLONG secondHandMs, ULONGLONG animationMs)
{
    w.onHide = [] { w.changing = true; };
    w.onFrame = [=] {
        if (!w.hides) return;
        const ULONGLONG elapsed = w.now - w.hideAt;
        if (elapsed >= firstHandMs) w.weapons[0] = kUnarmed;
        if (elapsed >= secondHandMs) w.weapons[1] = 0;
        if (elapsed >= animationMs) w.changing = false;
    };
}
static void UnarmedOpensImmediately()
{
    for (Hash empty : {0u, kUnarmed})
    {
        Reset(); w.weapons = {empty, empty};
        Check(OpenCard(), "unarmed card opening still succeeds");
        Check(w.waits == 0 && w.hides == 0 && w.ownedTasks == 1 && w.fallbackTasks == 0,
            "already free hands start inspection without an artificial delay or weapon mutation");
        Check(w.weaponReads[0] && w.weaponReads[1] && Cd.examining, "unarmed fast path checks both hands");
    }
}
static void HeldWeaponsFinishPuttingAway()
{
    for (Hash weapon : {kKnife, kRevolver, kRifle, Joaat("WEAPON_MELEE_LANTERN")})
    {
        for (int hand : {0, 1})
        {
            Reset(); w.weapons[hand] = weapon; TimedHolster(100, 350, 600);
            Check(OpenCard(), "knife, firearm, lantern and offhand inspection succeeds after put-away");
            Check(w.hides == 1 && w.ownedAt >= 1000 + 600 + Card::kHandsSettleMs,
                "a single holster request completes its animation and stable-hands interval before inspection");
            Check(w.createdAt == w.ownedAt && w.fallbackTasks == 0,
                "card creation waits until the original weapon has fully retired");
        }
    }
    Reset(); w.weapons = {kRevolver, kRevolver}; TimedHolster(100, 800, 250);
    Check(OpenCard(), "dual-wield inspection succeeds after both hands clear");
    Check(w.ownedAt >= 1000 + 800 + Card::kHandsSettleMs && w.hides == 1,
        "offhand weapon blocks inspection after primary selection and holster state clear");

    Reset(); w.weapons[0] = kKnife;
    w.onHide = [] { w.weapons[0] = kUnarmed; };
    w.onFrame = [] { w.changing = w.now >= 1080 && w.now < 1260; };
    Check(OpenCard(), "delayed holster animation is admitted after it finishes");
    Check(w.ownedAt >= 1260 + Card::kHandsSettleMs,
        "a delayed animation resets the settling interval after the selected hash becomes unarmed");
}
static void ExistingTransitionsAreNotRestarted()
{
    Reset(); w.weapons[0] = kKnife; w.changing = true;
    w.onFrame = [] {
        if (w.now >= 1100) w.weapons[0] = kUnarmed;
        if (w.now >= 1400) w.changing = false;
    };
    Check(OpenCard(), "an already running put-away completes before inspection");
    Check(w.hides == 0 && w.ownedAt >= 1400 + Card::kHandsSettleMs,
        "already holstering does not receive a redundant put-away request");

    Reset(); w.changing = true;
    w.onHide = [] { w.changing = true; };
    w.onFrame = [] {
        if (!w.hides) {
            if (w.now >= 1100) w.weapons[0] = kRevolver;
            if (w.now >= 1250) w.changing = false;
        } else {
            if (w.now >= w.hideAt + 100) w.weapons[0] = kUnarmed;
            if (w.now >= w.hideAt + 400) w.changing = false;
        }
    };
    Check(OpenCard(), "a draw already underway finishes and is then put away");
    Check(w.hides == 1 && w.hideAt == 1250 && w.ownedAt >= 1650 + Card::kHandsSettleMs,
        "active drawing is allowed to end before one normal holster request");
}
static void SetInterruption(int reason)
{
    switch (reason)
    {
    case 0: w.playerDead = true; break;
    case 1: w.playerExists = false; break;
    case 2: w.currentPlayer = kPlayer + 1; break;
    case 3: w.paused = true; break;
    case 4: w.faded = true; break;
    case 5: w.controlled = false; break;
    case 6: w.onFoot = false; break;
    case 7: w.combat = true; break;
    case 8: w.ragdoll = true; break;
    case 9: w.gettingUp = true; break;
    case 10: w.hogtied = true; break;
    case 11: w.lassoed = true; break;
    case 12: w.itemRunning = true; w.itemId = Joaat("foreign_item"); break;
    case 13: w.carrying = true; break;
    }
}
static void TimeoutAndInterruptionsDoNotStartTasks()
{
    Reset(); w.weapons[0] = kKnife;
    Check(!OpenCard(), "a weapon that never clears times out");
    Check(w.now == 1000 + Card::kHandsReadyWaitMs && w.hides == 1 &&
        w.ownedTasks == 0 && w.fallbackTasks == 0 && w.creates == 0,
        "timeout is bounded and starts neither inspection path nor creates a card");
    Check(w.maintenance == w.waits + 1, "waiting continues existing per-frame maintenance");

    Reset(); w.changing = true;
    Check(!OpenCard() && w.hides == 0 && w.ownedTasks == 0 && w.fallbackTasks == 0,
        "a stuck holster animation cannot start either inspection path");
    for (int reason = 0; reason < 14; ++reason)
    {
        Reset(); SetInterruption(reason);
        Check(!OpenCard() && w.waits == 0 && w.hides == 0 && w.creates == 0,
            "initial ineligibility and carried objects are refused without a weapon change");
        Reset(); w.weapons[0] = kRevolver;
        w.onFrame = [reason] { if (w.now >= 1100) SetInterruption(reason); };
        Check(!OpenCard(), "interruption during put-away cancels startup");
        Check(w.now == 1100 && w.ownedTasks == 0 && w.fallbackTasks == 0 && w.creates == 0,
            "interruption exits promptly without waiting out the timeout or starting fallback");
    }

    Reset(); w.weapons[0] = kKnife; Cd.obj = kCard; Cd.ownsObj = true; Cd.inHand = true;
    w.cardExists = w.attached = true;
    Check(!OpenCard(true) && w.detaches == 0 && Cd.inHand && Cd.obj == kCard,
        "failed preparation preserves a reused handoff card's existing attachment");
}
static void StreamingRechecksHandsAndEligibility()
{
    Reset(); TimedHolster(100, 100, 300);
    w.onPhoto = [] { w.weapons[0] = kKnife; WAIT(0); };
    Check(OpenCard() && w.hides == 1 && w.createdAt >= w.hideAt + 300 + Card::kHandsSettleMs,
        "photo preparation yielding to a weapon draw is gated before creating the card");

    Reset(); TimedHolster(100, 100, 300);
    w.onCreate = [] { w.weapons[1] = kRevolver; WAIT(0); };
    Check(OpenCard() && w.hides == 1 && w.createdAt < w.hideAt &&
        w.ownedAt >= w.hideAt + 300 + Card::kHandsSettleMs,
        "a weapon drawn while the card model streams is put away before the owned inspection starts");

    Reset(); w.onCreate = [] { w.weapons[0] = kKnife; WAIT(0); };
    Check(!OpenCard() && w.destroys == 1 && !Cd.obj && w.ownedTasks == 0 && w.fallbackTasks == 0,
        "failed post-streaming weapon preparation cleans up the provisional card without fallback");
    for (int reason = 0; reason < 14; ++reason)
    {
        Reset(); w.onCreate = [reason] { SetInterruption(reason); WAIT(0); };
        Check(!OpenCard() && w.destroys == 1 && !Cd.obj && w.ownedTasks == 0 && w.fallbackTasks == 0,
            "streaming interruptions, player replacement and carried items refuse both task starts");
    }
}
static void FallbackHasItsOwnReadinessGate()
{
    Reset(); w.createSucceeds = false; TimedHolster(100, 250, 400);
    w.onCreate = [] { w.weapons = {kRevolver, kRevolver}; WAIT(0); };
    Check(OpenCard() && w.ownedTasks == 0 && w.fallbackTasks == 1 && w.hides == 1 &&
        w.fallbackAt >= w.hideAt + 400 + Card::kHandsSettleMs,
        "failed object creation cannot bypass the fallback's weapon preparation");
    Check(Cd.examining && !Cd.ownsObj && Cd.obj == kCard, "fallback still adopts the matching native card");

    Reset(); w.createSucceeds = false;
    w.onCreate = [] { w.weapons[1] = kKnife; };
    Check(!OpenCard() && w.ownedTasks == 0 && w.fallbackTasks == 0 && w.hides == 1,
        "fallback timeout never calls either item interaction native");
    for (int reason = 0; reason < 14; ++reason)
    {
        Reset(); w.createSucceeds = false;
        w.onCreate = [reason] { SetInterruption(reason); };
        Check(!OpenCard() && w.ownedTasks == 0 && w.fallbackTasks == 0,
            "fallback refuses interruptions introduced by failed model preparation");
    }

    Reset(); w.ownedStarts = false; TimedHolster(100, 100, 400);
    w.onDestroy = [] { w.weapons[0] = kKnife; };
    Check(OpenCard() && w.ownedTasks == 1 && w.fallbackTasks == 1 && w.hides == 1,
        "a refused owned task can still recover through the native fallback");
    Check(w.fallbackAt >= w.hideAt + 400 + Card::kHandsSettleMs && w.hideAt >= w.ownedAt + Card::kTaskStartWaitMs * 2,
        "fallback rechecks weapon state after the first inspection startup wait and cleanup");
}
int main()
{
    UnarmedOpensImmediately(); HeldWeaponsFinishPuttingAway(); ExistingTransitionsAreNotRestarted();
    TimeoutAndInterruptionsDoNotStartTasks(); StreamingRechecksHandsAndEligibility();
    FallbackHasItsOwnReadinessGate();
    std::printf("Card start regression: %u checks passed.\n", checks);
}
