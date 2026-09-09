# Bounty Contracts

A Red Dead Redemption 2 single-player bounty mod with physical target-photo cards, station-clerk handoffs, corpse photography and payment.

The current `native-baseline-card-v1` build restores the native ambient behavior from commit `61f3ad873a30bed37fbeb24c9d4a1cb8698c5a32`. It retains the current contract card, portrait fixes, and combat/line-of-sight/search system. The owner tested this combined build and confirmed that the target immediately started an ambient activity, resolving the reported regression.

## Target behavior

Contracts use the original 18 location definitions and model pools in Rhodes, Blackwater, Valentine, Strawberry and Saint Denis. Each target receives `TASK_WANDER_IN_AREA` at its contract centre and radius. The game chooses local ambient behavior. Calm updates do not assign scenario points, reject activities by time/profession, or alternate scripted walks and pauses.

Targets react to provocation and remember the player. After eight seconds without visual contact, they search the last known position for ten seconds. A completed search hands control back to native wandering once; renewed sight can trigger another engagement. Restraints, ragdoll and getting up retain priority, and seated combat engagement waits for a normal scenario exit.

The search circle remains fixed at the original contract area. The daily schedule, generated occupation profiles, forced activity/recovery system and routine debug overlay are absent from this build. Older routine files remain as historical material outside the active project and default regression suite.

## Playing

1. Select **Get Contract** at a station clerk, or press **U** remotely.
2. Inspect the photo and location clue. Press **I** to inspect again; use the game's **Zoom**, **Flip**, and **Put Away** controls.
3. Find and kill the target, then photograph the corpse with the handheld camera when prompted.
4. Return to a clerk for **Collect Payment**, then take the money from the counter.

Pressing **U** during an unfinished hunt replaces it. After the corpse photograph, the pending reward survives and **U** reopens the card. Payment scales with contract duration and wanted status.

## Retained fixes

- Portraits use available local photo slots and owned download handles. Slots bound to cards are not reused during the session. When all 32 are used, the current hunt survives and a restart is requested.
- Card texture binding retries are bounded. Card opening waits for the player's weapon to be put away. A brief **U** input during this card's put-away animation is retained.
- Ped creation, portrait preparation and remote/clerk requests retain their retries. A dead or missing photo subject cannot produce an issued contract.
- Target deletion is tracked until confirmed; cleanup affects only the mod's owned subject. Gameplay deadlines freeze through pauses/loading.
- Corpse proof and pending payment survive corpse disappearance. Crime, payout and Eagle Eye cleanup remain scoped to the contract.

## Installation

Use `dist/native-baseline-card/` or `dist/BountyContracts-native-baseline-card.zip`.

1. Fully exit RDR2.
2. Install ScriptHookRDR2 and an ASI loader if needed. Copy the package's `TestScript.asi` beside `RDR2.exe`, replacing this mod's previous ASI. Keep one copy loaded.
3. For the native **Contract Information** title, merge the included `lml` folder with the game's folder using Lenny's Mod Loader. This supplies title text only; an existing identical installation can be kept.
4. Restart RDR2. An in-game ASI reload does not reset game-side portrait/ownership resources and is unsuitable for a clean test.

ScriptHookRDR2 and Lenny's Mod Loader are not included. The no-LML title fallback has not been verified in-game. `BountyContracts-startup-trace.log` beside the ASI identifies this build with `build=native-baseline-card-v1`.

## Building and testing

Use Visual Studio 2022, v143 C++ tools and Windows 10 SDK. Work on `dev`; publish `main` only when requested. Always override `OutDir`, because the project default points at a game installation.

```powershell
$bountyMsbuild = 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe'
$bountyRoot = (Get-Location).Path
& $bountyMsbuild (Join-Path $bountyRoot 'rdr2 scripting environment\samples\Pools\Pools.vcxproj') `
    /t:Build /p:Configuration=Release /p:Platform=x64 `
    "/p:OutDir=$bountyRoot/dist/native-baseline-card/" `
    "/p:IntDir=$bountyRoot/tmp/native-baseline-card-build/" /nologo
& .\tests\run-tests.ps1
```

The default suites cover native AI/LOS/search, passive ambient ownership, card rendering/inspection/startup, portrait cache/lifecycle, input, spawn/retry/cleanup, static search areas, corpse/payment lifecycle and startup logging. Simulated native responses cannot prove game navigation, animation or card pixels.

For live validation, watch ambient behavior before provoking the target, escape behind a wall, let the search finish, then return into sight. Also check repeated **U** contracts with distinct portraits, **I** while armed, clerk handoffs, lasso/ragdoll priority, corpse photography and a single payout.

The optional portrait diagnostic remains available through `/p:BountyPhotoDiagnostics=true`, using separate output/intermediate paths. It is not enabled in the normal package.

See [restoration scope](docs/native-baseline-restoration-2026-09-09.md), [portrait history](docs/startup-crash-investigation.md), [combat/LOS evidence](docs/target-ai-audit.md), and [card-inspection history](docs/card-framing-investigation-2026-09-07.md).
