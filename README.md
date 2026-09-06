# Bounty Contracts

A Red Dead Redemption 2 single-player mod written in C++ with Alexander Blade's ScriptHookRDR2. Station clerks offer bounty contracts with a physical photo card showing the target.

1. Visit a station clerk and choose **Get Contract**, or press **U** to receive a contract remotely.
2. Read the card, find the target, and complete the hunt.
3. Photograph the target's corpse when prompted.
4. Return to the clerk and choose **Collect Payment**, then use **Take Payment** beside the cash on the counter.

Press **I** to inspect the current contract card again. Use the game's own **Zoom**, **Flip**, and **Put Away** prompts while inspecting it.

The clerk hands over the textured card, the player receives it, and inspection opens after both handoff animations finish. Preparation happens before the animations start. An interrupted handoff leaves the contract available through **I**; an interrupted payment handover can be retried at the clerk. **U** while a hunt is still open ends that hunt and issues a new contract in its place, as **End Contract** at a clerk would; once the corpse has been photographed, **U** reopens that card instead so the pending reward stays collectable. New interactions wait until the player is on foot and out of combat, menus, and other item interactions.

The portrait is rebound when the handed-over card enters inspection, the native item state changes, or its texture becomes available again. Each transition schedules at most five binding attempts: immediately, then after 100, 300, 750, and 1500 ms. Maintenance and drawing share that schedule; slow frames skip missed attempts instead of replaying them. This removes the thousands of duplicate assignments seen during one inspection while retaining retries after material initialization. The timing is a fix candidate that still needs in-game validation; repeated assignments have not been proven to cause the failed downloads.

A contract is issued only after its target and portrait are ready. A failed capture is retried once on the same provisional target; if it still fails, that target is cleaned up and no photo card is handed out. Each portrait retains an explicit local texture-download handle. That handle is released before the next capture, and pending downloads are polled without allocating additional handles. The mod's card stays hidden if its texture is unavailable. Inspection waits briefly for recovery before asking you to retry with **I**.

Each capture writes one of the 32 local persona-photo slots (`Card::kPhotoSlot` through `kPhotoSlot + kPhotoSlotCount - 1`) and requests its download from that same slot, instead of always rewriting slot 0. A slot whose texture has been bound to a card is recorded for the rest of the session and skipped by later captures; among the remaining slots a cursor rotates, so the retry after a failed capture never repeats its slot either. New-contract preparation now checks for an unbound slot **before ending an existing hunt**. When all 32 have been bound, it reports **PORTRAIT SLOTS FULL** and asks for a game restart; the current contract and its card remain available. This does not reclaim slots or change `NextPhotoSlot`: the capture helper still falls back to reuse if called directly after exhaustion, as the cache regression test demonstrates. The download texture is named after its slot (dev-8 saw `..._MPG_0`), and the diagnostics point to an inspected card keeping that name referenced after its prop and item task retired: every download request for the rewritten slot returned -1, while the same rewrite succeeded whenever no card had been inspected. The multi-slot write is the sequence the game's own story-mode portrait code uses for several peds at once. `BountyContracts-startup.log` records the slot and mask of slots bound so far (`start-v5`); preflight exhaustion adds `photo=photo_slots_exhausted`, failure code 9.

A download handle is an opaque nonzero value: the game's own consumer treats only -1 (not yet) and 0 (refused) as "no download". The handles received in story mode are often negative, and an earlier positive-value test turned those accepted downloads into "TARGET PHOTO COULD NOT BE PREPARED" on roughly every second attempt (a `start-v5` line with a large negative `download`, `status=-1` and thirteen requests). Any other value is now polled and released like a positive one.

Capture now follows the same type-2 producer sequence as the cache consumer: wait until the capture system and previous upload are idle, clear previous data before generation, write on a later frame, wait for the upload, then separate completion, cleanup, and the first download request across frames. The extra SP cleanup immediately after generation has been removed. The SP commit probe is logged for diagnosis but does not gate this producer's completion.

**U** starts creation on the next game frame after refreshing the player state. Temporary ped-creation failures retry across frames while the model stays loaded. A returned ped handle is retained while it becomes available instead of being overwritten by another spawn attempt. If the game's NPC pool is full, creation waits briefly for capacity, then reports that there is no room for another target. It does not reroll more targets or remove unrelated NPCs. Details are written only on failures to `BountyContracts-startup.log` beside the ASI, including download handle/status/name and free NPC slots; include that file when reporting a recurring failure.

Failed or cancelled targets also remain tracked until a later frame confirms that the original entity has disappeared. A zeroed `DELETE_PED` argument alone is not treated as confirmation. Further creation waits briefly for cleanup, then reports a pending-cleanup message if needed. Deletion retries only touch the matching model owned by this script. `BountyContracts-cleanup.log` records requested, confirmed, blocked, and released targets with counts, so a failed deletion can be distinguished from general NPC pool pressure. The pool count alone cannot identify which script owns the occupied slots.

The cache change follows the type-2 handle lifecycle in the game's [persona photo script](https://github.com/creativewild/rdr2-scripts-decompiled/blob/953155c10ab0809fdcfb287f98650cd7e7eed1c4/1491.50/script_mp_rel/persona_photos.ysc.c) and [download consumer](https://github.com/JayKoZa/RDR2-Decompiled-Scripts/blob/a111493215b9db0b11c8f477816280f412c8b2de/script_mp_rel/net_ugc_end_flow.c#L7879-L7981); the rotating slot index follows the story-mode portrait code in [short_update](https://github.com/Halen84/RDR3-Decompiled-Scripts/blob/f7cb7ef3fbd30b09da85c12bfe74cf6c64de1ca2/1491.50/short_update.c), which writes one slot per ped with a single cache type. **Repeated portraits failed in the reported story-mode session before the slot rotation.** The `start-v4` log showed a completed write followed by 13 download requests returning -1; checked cleanup subsequently returned the failed target's NPC slot. Every diagnostic that reproduced it rewrote slot 0 after a card had been bound to slot 0's texture, and the same rewrite succeeded when no card had been inspected, which is why the fix avoids rewriting a bound slot rather than changing the capture itself. With the rotation and the opaque-handle fix, the owner confirmed in story mode that repeated **U** presses issue a new contract every time and each card shows its own target on both faces. Standalone tests verify slot rotation, ownership and retry logic using simulated native responses and do not establish the engine-level cause.

Targets keep one weapon loadout: setup removes inherited weapons once before choosing fists, knife or revolver. They pursue with the game's combat AI, search the last place they saw you after losing sight for eight seconds, then return to their roaming area after ten seconds of searching. They remember you and can re-engage on sight. Task recovery is delayed and rate-limited, and does not interrupt ragdoll, getting up, lasso, hogtying or fully hogtied states. Flee bit 32768 now follows the paired armed-enemy/flee transition in Flaco's mission; the full evidence and other retained flags are in [the AI audit](docs/target-ai-audit.md).

The September 2026 audit also retains one **U** release for up to three seconds during this card's put-away outro, including when its prop retires before the native task. Other blocked interactions do not queue a replacement; pause, fade, player change, handoff, payment or expiry cancels the request. Pauses inside streaming waits now freeze gameplay time while streaming retains its wall-clock timeout. Startup rejects a target that disappears or dies during capture, and gameplay rechecks suspension and target loss after yielding interactions. Wanted tracking continues after a photographed corpse disappears, and trail cleanup unregisters only the contract target. The working capture/card/handoff/payment functions are unchanged. General capture failure after replacement has begun can still leave no active hunt, and a transient unavailable texture status can still hide the card; those limitations are documented in [the full audit and test checklist](docs/audit-2026-09-06.md).

To photograph a corpse, enter the handheld camera view within 25 metres with a clear view of the target, then use **Take Photo**. Payment uses a decorative banknote and one credit path; if the note disappears or remains uncollected for two minutes, the reward is credited automatically.

## Installation

1. Install Alexander Blade's ScriptHookRDR2 and an ASI loader for RDR2.
2. Close the game and copy `TestScript.asi` from `dist` into the folder containing `RDR2.exe`. Replace the previous copy of this mod; keep only one copy loaded.
3. To use the native **Contract Information** card title, install Lenny's Mod Loader and merge the included `dist/lml` folder into the game's existing `lml` folder.

ScriptHookRDR2 and Lenny's Mod Loader are not included. The LML resource supplies only the card-title text; it does not provide the portrait or bounty gameplay. The code attempts a fallback without that resource, but the native title may be missing. The no-LML configuration has not yet been tested in-game.

The portrait fix was confirmed working in-game from the user's screenshots of dev-10-sp, and repeated contracts through **U** were confirmed in-game with the slot rotation and opaque-handle fix. The handoff and AI changes compile and pass the native-free regression tests but still require in-game validation. The hand-contact phase is an estimate in `Tune::kHandoffTransferPhase`; exact hand alignment must be checked at the different clerks.

## Building

Use Visual Studio 2022 with the v143 C++ toolset and Windows 10 SDK. The project is `rdr2 scripting environment/samples/Pools/Pools.vcxproj`, with configuration **Release | x64** and output name `TestScript.asi`.

Run this PowerShell command from the repository root. It overrides the project's output directory so the build stays inside the workspace; it does not install anything into the game folder.

```powershell
$bountyMsbuild = 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe'
$bountyRoot = (Get-Location).Path
$bountyBuildArgs = @(
    (Join-Path $bountyRoot 'rdr2 scripting environment\samples\Pools\Pools.vcxproj'),
    '/t:Build',
    '/p:Configuration=Release',
    '/p:Platform=x64',
    "/p:OutDir=$bountyRoot/dist/",
    "/p:IntDir=$bountyRoot/tmp/Release-x64/",
    '/m',
    '/nologo'
)
& $bountyMsbuild @bountyBuildArgs
```

Adjust the MSBuild path if Visual Studio is installed elsewhere. The ScriptHookRDR2 headers and x64 import library are included in the scripting environment.

## Optional portrait diagnostic build

Build with `/p:BountyPhotoDiagnostics=true`, a separate `OutDir` under `dist/diagnostic/`, and a separate `IntDir` under `tmp`. This defines `BOUNTY_PHOTO_SELF_TEST`; ordinary builds do not run these probes. The downloadable diagnostic ASI replaces `TestScript.asi` temporarily. Keep only one copy of the mod in the game folder.

The first diagnostic (`photo-test-v1`) passed repeated captures, released-download reopening, and a different NPC after 56 seconds. It also passed after opening and closing the handheld camera without taking a photo. `photo-test-v2` reproduced the failure after actual card inspection: the accepted object and item task retired, reopening the old portrait succeeded, then the second capture's download failed. Name invalidation and backup loading did not recover it. The inspection logged 2,072 custom-texture assignments. Later capture attempts still failed after NPC-pool capacity recovered.

`photo-test-v3` still failed after reducing texture assignments to 29. A fresh front-only run also failed with 16 assignments, zero face/panel sprite draws, and no selected named render target. Both the accepted card and item task had retired, and the old photo reopened successfully before the next capture failed. Its generation/upload took under 100 ms; the remaining three seconds were failed download requests. These results rule out flipping and excessive assignment count as sufficient explanations, but do not distinguish object material binding from ordinary inspection.

The current **`photo-test-v4`** separates those consumers. Fully restart the game with the diagnostic ASI, load story mode, stand on foot outside menus, and press **U once**. Keep the camera still during the automatic checks. The first inspection intentionally shows the stock card image; use **Put Away without flipping**. If another inspection opens later, check its portrait and put it away without flipping. Wait for **Photo test finished**, then send the new log lines from `BountyContracts-photo-test.log` and `BountyContracts-cleanup.log` beside the ASI. Stop after that run; a failed cache can make later attempts uninformative until a restart.

One hidden provisional NPC is used throughout and removed afterward, without issuing a bounty. A clerk request runs the same test. No completed bounty is needed. Restore the normal ASI and restart to resume bounty gameplay.

The log's `control` field identifies the steps, in order:

- `baseline`: initial capture, release/reopen without a write, then another capture with no card.
- `unbound_object`: create an unmodified card in front of the camera, retain it for two seconds, confirm deletion, then reopen/capture again.
- `plain_inspection`: use the normal card inspection with dynamic material assignment, portrait sprite drawing, and named-target registration disabled. Cache polling remains active. Confirm card and task retirement, then reopen/capture.
- `bound_object`: repeat the standalone object check with portrait binding enabled, then reopen/capture.
- `portrait_inspection`: normal inspection with all portrait consumers restored, followed by reopen/capture.

Every failed consumer, reopen, or capture stops the sequence. A completed sequence logs `phase=complete`. Per-control `binds`, `faceDraws`, and `panelDraws` counters distinguish actual consumer use; `plainCard=1` should have zero of each. `object_on_screen` reports the engine's visibility result, not proof of correct pixels. Object checks reject pause/fade, a new item task, early disappearance, or unconfirmed deletion. Failed deletion retains the owned handle and blocks later controls instead of silently forgetting the object. If a check is interrupted, restart before another run.

`photo-timing-v1` records capture-stage durations separately from native-call maxima. A successful handle or valid name alone does not prove fresh pixels. The failed v3 name-release/backup-recovery experiments have been removed from v4; this build changes no gameplay capture semantics or cache types. Since the slot rotation, each of its captures writes the next slot like normal gameplay does, and the log's `slot` field shows which one; a same-slot rewrite can no longer be reproduced with it.

## Development

Use `dev` for ongoing work. Update `main` when explicitly requested.

Run `./tests/run-tests.ps1` from PowerShell to compile and execute eleven suites: deterministic AI, handoff, keyboard, card-texture, spawn/input/pause, portrait-startup, portrait-cache, owned-ped cleanup, diagnostic-driver, native AI bridge, and contract lifecycle. The native-shim tests exercise actual production functions; they cannot verify the game's rendering or establish the cause of an in-game engine failure. Build output stays under `tmp/tests`.

[Train and Saint Denis tram mission research](docs/train-tram-missions.md) documents the local Rockstar examples, verified SDK surface, ownership/cleanup risks and future `ContractDef` integration. It adds no train or tram gameplay.

For in-game validation, check **U** from idle, **U** again after inspecting and putting away the first card (the second card must show the second target on both faces), receiving a contract at two different clerks, **I** and card flip/put-away, interrupting a handoff, escaping behind a wall and returning, a lasso/ragdoll encounter, photographing a corpse, and collecting one reward. Pause during a handoff as well: gameplay deadlines should stay frozen. The physical hand transfer and native AI behavior cannot be verified by the standalone tests.

The handoff preserves the clerk's base scenario and blends an upper-body animation over it. The selected donor/receiver clips are listed in [femga's animation catalog](https://github.com/femga/rdr3_discoveries/blob/master/animations/ingameanims/ingameanims_list.lua); the original [Pay Alden scene](https://github.com/Halen84/RDR3-Decompiled-Scripts/blob/f7cb7ef3fbd30b09da85c12bfe74cf6c64de1ca2/1491.50/rcm_coach_robbery1.c#L13525-L13611) establishes the donor role. No verified transfer event exists for this clip pair, so the transfer uses a tunable animation phase rather than an assumed event hash.

## SDK and research resources

- [Alexander Blade's ScriptHookRDR2](http://www.dev-c.com/rdr2/scripthookrdr2/)
- [alloc8or native database](https://alloc8or.re/rdr3/nativedb/)
- [alloc8or RDR3 documentation](https://alloc8or.re/rdr3/doc/)
- [femga's RDR3 discoveries](https://github.com/femga/rdr3_discoveries)
- [RDR3 script global research](https://github.com/Halen84/RDR3-Script-Global-Research)
- [RDR3 native flags and enums](https://github.com/Halen84/RDR3-Native-Flags-And-Enums)
- [RDR3 decompiled scripts](https://github.com/Halen84/RDR3-Decompiled-Scripts)
- [RDR3 parser dumps, build 1491](https://alexguirre.github.io/rage-parser-dumps/dump.html?game=rdr3&build=1491)
