// Exercise actual crime, payout, trail and contract-reset functions with deterministic native responses.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../rdr2 scripting environment/samples/Pools/target_ai_logic.h"

using ULONGLONG = std::uint64_t;
using Ped = int;
using Player = int;
using Object = int;
using Blip = int;
using Prompt = int;
constexpr Ped kTarget = 77, kPlayer = 42;
constexpr Object kCash = 88;
static Player me = 0;
static Ped pedMe = kPlayer;
static Prompt giverPrompt = 90, camPrompt = 91;
enum ContractState { CONTRACT_NONE, CONTRACT_UNKNOWN, CONTRACT_FOUND, CONTRACT_DEAD, CONTRACT_PAID };
static ContractState g_state = CONTRACT_NONE;
static unsigned checks = 0;

static void Check(bool condition, const char* description)
{
    ++checks;
    if (!condition) { std::fprintf(stderr, "FAILED: %s\n", description); std::exit(EXIT_FAILURE); }
}

struct Vector3 { float x = 0, y = 0, z = 0; };
struct ContractDef { void (*onCleanup)() = nullptr; };
static Vector3 playerPos;

struct ActiveContract
{
    Ped target = 0;
    ULONGLONG startMs = 0, crimeMs = 0, photoMs = 0;
    int bountyAtCrime = 0;
    bool gotWanted = false;
    bool damagedByPlayer = false;
    TargetAI::Memory ai;
    Blip searchBlip = 0, targetBlip = 0;
    Object cashObj = 0;
    const ContractDef* def = nullptr;
    bool trailsActive = false;
    Vector3 targetPos;
};
static ActiveContract C;

static struct World
{
    ULONGLONG nowMs = 1000;
    bool targetExists = true;
    bool nativeCombat = false;
    bool incidentActive = false;
    int wantedScore = 0;
    int bounty = 0;
    unsigned combatQueries = 0;
    unsigned lawQueries = 0;
    bool cashExists = false;
    bool ownTrailRegistered = false;
    bool unrelatedTrailRegistered = true;
    unsigned trailRegistrations = 0, trailUnregistrations = 0, tintChanges = 0, globalTrailClears = 0;
    unsigned blipRemovals = 0, handoffStops = 0, cardCleanups = 0, photoReleases = 0;
    unsigned cashDeletes = 0, cleanupHooks = 0, pedCleanupRequests = 0, pedReleases = 0;
    unsigned giverPromptResets = 0, cameraPromptResets = 0;
    unsigned lostMessages = 0;
    bool playerAvailable = true, paused = false, faded = false, passedGate = false;
    unsigned giverPromptHides = 0, cameraPromptHides = 0, waits = 0;
} world;

static ULONGLONG RuntimeNowMs() { return world.nowMs; }
static bool TargetExists() { return C.target == kTarget && world.targetExists; }
static bool PlayerAvailable() { return world.playerAvailable; }
namespace HUD { static bool IS_PAUSE_MENU_ACTIVE() { return world.paused; } }
namespace CAMERA { static bool IS_SCREEN_FADED_OUT() { return world.faded; } }
static void ShowPrompt(Prompt prompt, bool show)
{
    Check(!show && (prompt == giverPrompt || prompt == camPrompt),
        "a suspended frame hides only its two owned prompts");
    if (prompt == giverPrompt) ++world.giverPromptHides;
    else ++world.cameraPromptHides;
}
static void WAIT(int delay)
{
    Check(delay == 0, "a suspended post-interaction frame yields without a blocking delay");
    ++world.waits;
}
static bool Within(const Vector3& a, const Vector3& b, float distance)
{
    const float x = a.x - b.x, y = a.y - b.y, z = a.z - b.z;
    return x * x + y * y + z * z <= distance * distance;
}

namespace ENTITY
{
static bool DOES_ENTITY_EXIST(int entity)
{
    return (entity == kTarget && world.targetExists) || (entity == kCash && world.cashExists);
}
}

namespace PLAYER
{
static void _REGISTER_EAGLE_EYE_FOR_ENTITY(Player player, int entity, bool)
{
    Check(player == me && entity == kTarget && world.targetExists,
        "trail registration addresses only the existing contract target");
    world.ownTrailRegistered = true;
    ++world.trailRegistrations;
}
static void _UNREGISTER_EAGLE_EYE_FOR_ENTITY(Player player, int entity)
{
    Check(player == me && entity == kTarget && world.targetExists,
        "trail removal addresses only the existing contract target");
    world.ownTrailRegistered = false;
    ++world.trailUnregistrations;
}
static void EAGLE_EYE_SET_CUSTOM_ENTITY_TINT(int entity, int red, int green, int blue)
{
    Check(entity == kTarget && world.targetExists && red == 255 && green == 255 && blue == 0,
        "only the contract target receives its yellow eagle-eye tint");
    ++world.tintChanges;
}
// Keep the previous global native available so an accidental reintroduction fails behavior checks.
void _CLEAR_PED_EAGLE_EYE_TRAILS_FOR_PLAYER(Player)
{
    world.ownTrailRegistered = false;
    world.unrelatedTrailRegistered = false;
    ++world.globalTrailClears;
}
}

namespace OBJECT
{
static void DELETE_OBJECT(Object* object)
{
    Check(*object == kCash && world.cashExists, "contract reset deletes its own existing cash prop");
    world.cashExists = false;
    *object = 0;
    ++world.cashDeletes;
}
}

static void StopHandoff() { ++world.handoffStops; }
static void RemoveBlip(Blip& blip) { if (blip) ++world.blipRemovals; blip = 0; }
static void DestroyCardObject(bool cancel)
{
    Check(cancel, "contract reset requests cancellation of its own inspection");
    ++world.cardCleanups;
}
static void ReleaseTargetPhoto() { ++world.photoReleases; }
static void RequestOwnedPedCleanup(Ped ped)
{
    Check(ped == kTarget || ped == 0, "contract cancellation only passes its target to owned cleanup");
    if (ped) ++world.pedCleanupRequests;
}
static void ReleaseOwnedPed(Ped ped)
{
    Check(ped == kTarget || ped == 0, "contract completion only releases its target");
    if (ped) ++world.pedReleases;
}
static void ResetPrompt(Prompt prompt)
{
    Check(prompt == giverPrompt || prompt == camPrompt, "contract reset addresses only its own prompts");
    if (prompt == giverPrompt) ++world.giverPromptResets;
    else ++world.cameraPromptResets;
}
static void CleanupHook() { ++world.cleanupHooks; }
static void DisplaySubtitle(const char* message)
{
    Check(std::strcmp(message, "TARGET LOST") == 0, "lost-target detection explains why the unfinished hunt ended");
    ++world.lostMessages;
}

namespace PED
{
static bool IS_PED_IN_COMBAT(Ped target, Ped opponent)
{
    Check(target == kTarget && opponent == kPlayer && world.targetExists,
        "crime detection never queries a missing target or an unrelated ped");
    ++world.combatQueries;
    return world.nativeCombat;
}
}

namespace LAW
{
static int GET_BOUNTY(Player player)
{
    Check(player == me, "bounty reads the current player");
    ++world.lawQueries;
    return world.bounty;
}
static bool IS_LAW_INCIDENT_ACTIVE(Player player)
{
    Check(player == me, "incident reads the current player");
    ++world.lawQueries;
    return world.incidentActive;
}
static int GET_WANTED_SCORE(Player player)
{
    Check(player == me, "wanted score reads the current player");
    ++world.lawQueries;
    return world.wantedScore;
}
}

#include "contract_lifecycle_under_test.h"

static void Reset()
{
    C = ActiveContract();
    C.target = kTarget;
    C.startMs = 1000;
    world = World();
    playerPos = Vector3();
    g_state = CONTRACT_NONE;
}

static void TestCrimeRequiresHostileContact()
{
    Reset();
    world.incidentActive = true;
    world.wantedScore = 10;
    world.bounty = 500;
    UpdateCrimeTracking();
    Check(!C.crimeMs && !C.gotWanted && world.lawQueries == 0,
        "unrelated law activity before first hostile contact does not alter the contract");

    Reset();
    world.targetExists = false;
    C.damagedByPlayer = true;
    C.ai.state = TargetAI::State::Engaged;
    UpdateCrimeTracking();
    Check(!C.crimeMs && !C.gotWanted && !world.combatQueries && !world.lawQueries,
        "a missing target cannot start a new crime from stale damage or AI memory");

    for (int contact = 0; contact < 3; ++contact)
    {
        Reset();
        world.nowMs = 6000;
        world.bounty = 250;
        world.nativeCombat = contact == 0;
        C.damagedByPlayer = contact == 1;
        if (contact == 2) C.ai.state = TargetAI::State::Engaged;
        UpdateCrimeTracking();
        Check(C.crimeMs == 6000 && C.bountyAtCrime == 250 && !C.gotWanted,
            "native combat, damage and engaged AI each establish the initial crime baseline");
        world.nowMs = 12000;
        UpdateCrimeTracking();
        Check(C.crimeMs == 6000 && C.bountyAtCrime == 250,
            "continued hostile contact does not restart the crime time or baseline");
    }
}

static void TestLawTrackingSurvivesCorpseDisappearance()
{
    for (int incident = 0; incident < 3; ++incident)
    {
        Reset();
        world.nativeCombat = true;
        world.bounty = 250;
        UpdateCrimeTracking();
        world.targetExists = false;
        world.nativeCombat = false;
        C.photoMs = 1000; // The proof and reward remain valid after the corpse disappears.
        world.incidentActive = incident == 0;
        world.wantedScore = incident == 1 ? 1 : 0;
        world.bounty = incident == 2 ? 251 : 250;
        const unsigned previousCombatQueries = world.combatQueries;
        UpdateCrimeTracking();
        Check(C.gotWanted && world.combatQueries == previousCombatQueries,
            "a later incident, wanted score or bounty increase counts after corpse disappearance");
        world.incidentActive = false;
        world.wantedScore = 0;
        world.bounty = 0;
        const unsigned previousLawQueries = world.lawQueries;
        UpdateCrimeTracking();
        Check(C.gotWanted && world.lawQueries == previousLawQueries,
            "a wanted penalty stays recorded after the law activity has cleared");
    }

    Reset();
    C.crimeMs = 1000;
    C.bountyAtCrime = 250;
    world.targetExists = false;
    world.bounty = 250;
    UpdateCrimeTracking();
    Check(!C.gotWanted, "an unchanged pre-existing bounty alone does not count as later law activity");
    world.bounty = 100;
    UpdateCrimeTracking();
    Check(!C.gotWanted, "a reduced bounty after corpse disappearance does not create a penalty");
}

static void TestPayoutBoundsAndTiming()
{
    Reset();
    Check(ComputePayoutCents() == 2500, "a just-started contract pays the $25 floor");
    world.nowMs = C.startMs + 3 * 1000;
    Check(ComputePayoutCents() == 2525, "partial payout rounds down to a 25-cent step");
    world.nowMs = C.startMs + 10 * 60000;
    Check(ComputePayoutCents() == 10000, "a ten-minute contract pays $100");
    C.gotWanted = true;
    Check(ComputePayoutCents() == 5000, "wanted halves the earned payout before the floor");
    world.nowMs = C.startMs + 2 * 60000;
    Check(ComputePayoutCents() == 2500, "a short wanted contract still pays the advertised $25 floor");
    world.nowMs = C.startMs + 20 * 60000;
    C.gotWanted = false;
    Check(ComputePayoutCents() == 17500, "twenty minutes reaches the $175 ceiling");
    world.nowMs = C.startMs + ULONGLONG(30) * 24 * 60 * 60000;
    Check(ComputePayoutCents() == 17500, "even a very long contract remains at the ceiling");
    C.gotWanted = true;
    Check(ComputePayoutCents() == 8750, "wanted also halves the capped payout");

    Reset();
    C.photoMs = C.startMs + 10 * 60000;
    world.nowMs = C.startMs + 30 * 60000;
    Check(ComputePayoutCents() == 10000, "the corpse photograph fixes elapsed hunt time until collection");
    C.gotWanted = true;
    Check(ComputePayoutCents() == 5000, "law activity after the photo still applies to its frozen hunt time");
}

static void TestTrailOwnershipAndTransitions()
{
    Reset();
    g_state = CONTRACT_UNKNOWN;
    C.targetPos.x = 10;
    UpdateTrails();
    Check(C.trailsActive && world.ownTrailRegistered && world.trailRegistrations == 1 && world.tintChanges == 1,
        "a nearby unknown target gets one registered trail and tint");
    UpdateTrails();
    g_state = CONTRACT_FOUND;
    UpdateTrails();
    Check(world.trailRegistrations == 1, "steady proximity and target discovery do not register every frame");
    C.targetPos.x = 13;
    UpdateTrails();
    Check(!C.trailsActive && !world.ownTrailRegistered && world.trailUnregistrations == 1,
        "leaving the target's twelve-metre range removes only its registration");
    Check(world.unrelatedTrailRegistered && !world.globalTrailClears,
        "leaving the target preserves another script's eagle-eye trail");
    UpdateTrails();
    Check(world.trailUnregistrations == 1, "steady distance does not unregister every frame");
    C.targetPos.x = 12;
    UpdateTrails();
    Check(C.trailsActive && world.trailRegistrations == 2, "the twelve-metre boundary allows reacquiring the trail");
    g_state = CONTRACT_DEAD;
    UpdateTrails();
    Check(!C.trailsActive && world.trailUnregistrations == 2 && world.unrelatedTrailRegistered,
        "photographing the corpse retires only the contract trail");

    Reset();
    g_state = CONTRACT_FOUND;
    world.targetExists = false;
    UpdateTrails();
    Check(!C.trailsActive && !world.trailRegistrations && !world.trailUnregistrations,
        "a vanished nearby target cannot be registered or passed to trail natives");
    C.trailsActive = true;
    UpdateTrails();
    Check(!C.trailsActive && !world.trailUnregistrations && world.unrelatedTrailRegistered,
        "an already-active vanished target resets local trail state without an invalid native call");
}

static void TestContractCleanupRouting()
{
    static const ContractDef def = { CleanupHook };
    for (int deleting = 0; deleting < 2; ++deleting)
    {
        Reset();
        C.def = &def;
        C.searchBlip = 1;
        C.targetBlip = 2;
        C.cashObj = kCash;
        C.trailsActive = true;
        C.crimeMs = 1000;
        C.gotWanted = true;
        world.cashExists = true;
        world.ownTrailRegistered = true;
        g_state = CONTRACT_FOUND;
        ClearContract(deleting != 0);
        Check(g_state == CONTRACT_NONE && !C.target && !C.def && !C.trailsActive && !C.startMs && !C.crimeMs && !C.gotWanted,
            "contract reset discards prior target, timing, trail and law metadata");
        Check(world.handoffStops == 1 && world.cardCleanups == 1 && world.photoReleases == 1 && world.blipRemovals == 2,
            "contract reset retires the handoff, card, portrait and both blip handles");
        Check(world.cashDeletes == 1 && !world.cashExists && world.cleanupHooks == 1,
            "contract reset deletes its cash prop and runs its selected cleanup hook once");
        Check(world.pedCleanupRequests == unsigned(deleting) && world.pedReleases == unsigned(!deleting),
            "cancellation requests owned deletion while completion releases the corpse");
        Check(world.trailUnregistrations == 1 && !world.ownTrailRegistered && world.unrelatedTrailRegistered && !world.globalTrailClears,
            "contract cleanup preserves other scripts' eagle-eye trails");
        Check(world.giverPromptResets == 1 && world.cameraPromptResets == 1,
            "contract cleanup resets both owned prompts");
        ClearContract(deleting != 0);
        Check(world.cleanupHooks == 1 && world.cashDeletes == 1 && world.trailUnregistrations == 1 && world.unrelatedTrailRegistered,
            "clearing an empty contract does not rerun entity cleanup or remove unrelated trails");
    }

    Reset();
    world.targetExists = false;
    C.trailsActive = true;
    ClearContract(false);
    Check(!C.trailsActive && world.pedReleases == 1 && !world.trailUnregistrations && !world.globalTrailClears,
        "lost-target cleanup releases ownership bookkeeping without invoking a trail native on the missing entity");
}

static void TestLostTargetStateRouting()
{
    for (int state = CONTRACT_NONE; state <= CONTRACT_PAID; ++state)
    {
        Reset();
        g_state = static_cast<ContractState>(state);
        C.photoMs = 5000;
        C.searchBlip = 1;
        C.targetBlip = 2;
        CheckTargetLost();
        Check(g_state == state && C.target == kTarget && !world.lostMessages && !world.pedReleases,
            "a still-existing target is retained in every contract state");
        world.targetExists = false; // Simulate deletion while the earlier interaction yielded.
        CheckTargetLost();
        if (state == CONTRACT_UNKNOWN || state == CONTRACT_FOUND)
        {
            Check(g_state == CONTRACT_NONE && !C.target && !C.searchBlip && !C.targetBlip && world.lostMessages == 1,
                "a target lost during preparation retires an unfinished hunt before its next state update");
            Check(world.pedReleases == 1 && !world.pedCleanupRequests && world.blipRemovals == 2,
                "lost-target routing releases only bookkeeping and removes both contract blips");
            CheckTargetLost();
            Check(world.lostMessages == 1 && world.pedReleases == 1,
                "two lost-target checks cannot issue duplicate cleanup or notifications");
        }
        else
        {
            Check(g_state == state && C.target == kTarget && C.photoMs == 5000 && !world.lostMessages && !world.pedReleases,
                "corpse proof, pending payment and idle state survive a missing target");
        }
    }
}

static void TestPostInteractionPauseAndPlayerGate()
{
    for (int interruption = 0; interruption < 3; ++interruption)
    {
        Reset();
        g_state = CONTRACT_FOUND;
        world.targetExists = false;
        world.playerAvailable = interruption != 0;
        world.paused = interruption == 1;
        world.faded = interruption == 2;
        RunPostInteractionGate();
        Check(!world.passedGate && world.waits == 1 && world.giverPromptHides == 1 && world.cameraPromptHides == 1,
            "a player loss, pause or fade that begins during a yielding interaction stops the current state update");
        Check(g_state == CONTRACT_FOUND && C.target == kTarget && !world.lostMessages,
            "a suspended frame does not enter lost-target cleanup before yielding");
        world.playerAvailable = true;
        world.paused = world.faded = false;
        RunPostInteractionGate();
        Check(world.passedGate && g_state == CONTRACT_NONE && world.lostMessages == 1 && world.waits == 1,
            "resuming the post-interaction guard revalidates a target that disappeared during the wait");
    }

    Reset();
    g_state = CONTRACT_FOUND;
    RunPostInteractionGate();
    Check(world.passedGate && !world.waits && !world.giverPromptHides && !world.cameraPromptHides && g_state == CONTRACT_FOUND,
        "an ordinary live gameplay frame proceeds without hiding prompts or altering its hunt");
}

int main()
{
    TestCrimeRequiresHostileContact();
    TestLawTrackingSurvivesCorpseDisappearance();
    TestPayoutBoundsAndTiming();
    TestTrailOwnershipAndTransitions();
    TestContractCleanupRouting();
    TestLostTargetStateRouting();
    TestPostInteractionPauseAndPlayerGate();
    std::printf("Contract lifecycle tests passed (%u checks, actual production crime, payout, trail and cleanup functions).\n", checks);
}
