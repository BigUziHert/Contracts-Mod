# Bounty Contracts

A Red Dead Redemption 2 single-player mod written in C++ with Alexander Blade's ScriptHookRDR2. Station clerks offer bounty contracts with a physical photo card showing the target.

1. Visit a station clerk and choose **Get Contract**, or press **U** to receive a contract remotely.
2. Read the card, find the target, and complete the hunt.
3. Photograph the target's corpse when prompted.
4. Return to the clerk and choose **Collect Payment**, then use **Take Payment** beside the cash on the counter.

Press **I** to inspect the current contract card again. Use the game's own **Zoom**, **Flip**, and **Put Away** prompts while inspecting it.

The clerk hands over the textured card, the player receives it, and inspection opens after both handoff animations finish. Preparation happens before the animations start. An interrupted handoff leaves the contract available through **I**; an interrupted payment handover can be retried at the clerk. **U** opens an existing contract rather than replacing it, and new interactions wait until the player is on foot and out of combat, menus, and other item interactions.

The portrait is rebound when the handed-over card enters inspection and refreshed briefly while its material settles, including task-start waits and intro/flip transitions. This addresses the stale texture flag that could leave the prop's original artwork showing through on first inspection, while reopening with **I** looked correct. The visual fix still needs in-game confirmation.

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

## Development

Use `dev` for ongoing work. Update `main` when explicitly requested.

Run `./tests/run-tests.ps1` from PowerShell to compile and execute the deterministic AI, handoff, keyboard, and card-texture tests. The keyboard and texture tests exercise the actual source with game-native shims; they cannot verify the game's rendering. Build output stays under `tmp/tests`.

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
