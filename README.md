# Bounty Contracts

A Red Dead Redemption 2 single-player mod written in C++ with Alexander Blade's ScriptHookRDR2. Station clerks offer bounty contracts with a physical photo card showing the target.

1. Visit a station clerk and choose **Get Contract**, or press **U** to receive a contract remotely.
2. Read the card, find the target, and complete the hunt.
3. Photograph the target's corpse when prompted.
4. Return to the clerk and choose **Collect Payment**, then use **Take Payment** beside the cash on the counter.

Press **I** to inspect the current contract card again. Use the game's own **Zoom**, **Flip**, and **Put Away** prompts while inspecting it.

The clerk hands over the textured card, the player receives it, and inspection opens after both handoff animations finish. Preparation happens before the animations start. An interrupted handoff leaves the contract available through **I**; an interrupted payment handover can be retried at the clerk. **U** opens an existing contract rather than replacing it, and new interactions wait until the player is on foot and out of combat, menus, and other item interactions.

The portrait is rebound when the handed-over card enters inspection and refreshed briefly while its material settles, including task-start waits and intro/flip transitions. This addresses the stale texture flag that could leave the prop's original artwork showing through on first inspection, while reopening with **I** looked correct. The visual fix still needs in-game confirmation.

A contract is issued only after its target and portrait are ready. A failed capture is retried once on the same provisional target; if it still fails, that target is cleaned up and no photo card is handed out. Each portrait now retains an explicit local texture-download handle. That handle is released before the next capture overwrites its cache slot, and pending downloads are polled without allocating additional handles. The mod's card stays hidden if its texture is unavailable. Inspection waits briefly for recovery before asking you to retry with **I**.

Capture now follows the same type-2 producer sequence as the cache consumer: wait until the capture system and previous upload are idle, clear previous data before generation, write on a later frame, wait for the upload, then separate completion, cleanup, and the first download request across frames. The extra SP cleanup immediately after generation has been removed. The SP commit probe is logged for diagnosis but does not gate this producer's completion.

**U** starts creation on the next game frame after refreshing the player state. Temporary ped-creation failures retry across frames while the model stays loaded. A returned ped handle is retained while it becomes available instead of being overwritten by another spawn attempt. If the game's NPC pool is full, creation waits briefly for capacity, then reports that there is no room for another target. It does not reroll more targets or remove unrelated NPCs. Details are written only on failures to `BountyContracts-startup.log` beside the ASI, including download handle/status/name and free NPC slots; include that file when reporting a recurring failure.

Failed or cancelled targets also remain tracked until a later frame confirms that the original entity has disappeared. A zeroed `DELETE_PED` argument alone is not treated as confirmation. Further creation waits briefly for cleanup, then reports a pending-cleanup message if needed. Deletion retries only touch the matching model owned by this script. `BountyContracts-cleanup.log` records requested, confirmed, blocked, and released targets with counts, so a failed deletion can be distinguished from general NPC pool pressure. The pool count alone cannot identify which script owns the occupied slots.

The cache change follows the type-2 handle lifecycle in the game's [persona photo script](https://github.com/creativewild/rdr2-scripts-decompiled/blob/953155c10ab0809fdcfb287f98650cd7e7eed1c4/1491.50/script_mp_rel/persona_photos.ysc.c) and [download consumer](https://github.com/JayKoZa/RDR2-Decompiled-Scripts/blob/a111493215b9db0b11c8f477816280f412c8b2de/script_mp_rel/net_ugc_end_flow.c#L7879-L7981). **Repeated portraits still fail in the reported story-mode session.** The latest `start-v4` log shows a completed write followed by 13 download requests returning -1; checked cleanup subsequently returned the failed target's NPC slot. Standalone tests verify ownership and retry logic using simulated native responses and do not establish the engine-level cause.

Targets keep one weapon loadout, pursue with the game's combat AI, search the last place they saw you after losing sight for eight seconds, then return to their roaming area after ten seconds of searching. They remember you and can re-engage on sight. Task recovery is delayed and rate-limited, and does not interrupt ragdoll or lasso recovery.

To photograph a corpse, enter the handheld camera view within 25 metres with a clear view of the target, then use **Take Photo**. Payment uses a decorative banknote and one credit path; if the note disappears or remains uncollected for two minutes, the reward is credited automatically.

## Installation

1. Install Alexander Blade's ScriptHookRDR2 and an ASI loader for RDR2.
2. Close the game and copy `TestScript.asi` from `dist` into the folder containing `RDR2.exe`. Replace the previous copy of this mod; keep only one copy loaded.
3. To use the native **Contract Information** card title, install Lenny's Mod Loader and merge the included `dist/lml` folder into the game's existing `lml` folder.

ScriptHookRDR2 and Lenny's Mod Loader are not included. The LML resource supplies only the card-title text; it does not provide the portrait or bounty gameplay. The code attempts a fallback without that resource, but the native title may be missing. The no-LML configuration has not yet been tested in-game.

The portrait fix was confirmed working in-game from the user's screenshots of dev-10-sp. The current dev handoff and AI changes compile and pass the native-free regression tests but still require in-game validation. The hand-contact phase is an estimate in `Tune::kHandoffTransferPhase`; exact hand alignment must be checked at the different clerks.

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

Fully exit and restart the game with the diagnostic ASI, load story mode, stand on foot outside menus, and press **U once**. Wait for **Photo test finished**. This build creates one hidden provisional NPC, runs bounded portrait tests, then removes the NPC without issuing a contract. A clerk request runs the same test; completing a bounty is unnecessary. Read `BountyContracts-photo-test.log` and `BountyContracts-cleanup.log` beside the installed ASI. Restore the normal ASI and restart to resume bounty gameplay.

The log identifies each probe:

- `initial`: capture and read the first portrait normally. If this fails, stop so later tests do not assume a published first portrait.
- `A_reopen_without_write`: release the download and reopen the same slot, without another capture or write.
- `B_second_capture`: only if A succeeds, capture the same subject again and read it back.
- `C_before_name_release` / `C_name_invalidation` / `C_reopen_without_write`: after a readback failure, record validity, release the exact most recently accepted texture name, wait for invalidation, then reopen the existing photo without writing again. `C_name_already_invalid` identifies names that were invalid before release; this does not demonstrate an invalidation transition.
- `D_backup_without_write`: if C cannot recover, try the backup reader last, with no explicit download handle or accepted card texture active. This measures backup recovery after C, not an independent backup test against untouched cache state.

The name-invalidation probe is experimental, based on the game's [pause-menu mugshot lifecycle](https://github.com/JayKoZa/RDR2-Decompiled-Scripts/blob/a111493215b9db0b11c8f477816280f412c8b2de/script_mp_rel/pause_menu.c#L333-L360); it is not a verified fix for this cache. A successful handle or valid name alone does not prove fresh pixels. No gameplay fallback, slot rotation, or unverified cache type is enabled by this diagnostic build.

## Development

Use `dev` for ongoing work. Update `main` when explicitly requested.

Run `./tests/run-tests.ps1` from PowerShell to compile and execute the deterministic AI, handoff, keyboard, card-texture, spawn, portrait-startup, portrait-cache, owned-ped cleanup, and diagnostic-driver tests. The native-shim tests exercise actual production functions; they cannot verify the game's rendering or establish the cause of an in-game engine failure. Build output stays under `tmp/tests`.

For in-game validation, check **U** from idle, receiving a contract at two different clerks, **I** and card flip/put-away, interrupting a handoff, escaping behind a wall and returning, a lasso/ragdoll encounter, photographing a corpse, and collecting one reward. Pause during a handoff as well: gameplay deadlines should stay frozen. The physical hand transfer and native AI behavior cannot be verified by the standalone tests.

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
