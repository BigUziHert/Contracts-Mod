# Routine startup placement fix — 6 September 2026

The first in-game routine run repeatedly returned **NO SAFE TARGET AVAILABLE** after
pressing **U**. Twelve `start-v5` failures between 02:07:06 and 02:07:59 UTC on September 7
all recorded failure 10 after successful portrait generation and upload. Initial location
selection had therefore succeeded; rejection happened during destination revalidation or
final ped placement. The old log did not distinguish these sub-stages.

The matching cleanup log confirmed all twelve provisional targets were deleted, with
created=12, deleted=12 and released=0. It does not show an accumulating target leak.
The reported **Unknown error FFFFFFFF** dialog is not enough to identify a crash cause;
the available ScriptHook and Windows application logs supplied no matching crash details.
This change fixes identified placement errors, without claiming the crash is resolved.

## Corrected behavior

- Candidate selection uses `GET_SAFE_COORD_FOR_PED` once. Subsequent checks inspect the
  saved point, including its ground and clearance, without asking for a second coordinate
  and rejecting the first when the answer moves by more than 0.5 m. Periodic destination
  revalidation uses the same direct check.
- The actual ped position uses separate horizontal and vertical bounds. A body origin
  one metre above the street no longer fails the old 0.75 m three-dimensional distance
  test. Horizontal drift is still limited to 0.75 m, and the ground beneath the actual X/Y
  must remain within 0.35 m of the saved ground. Source-anchor height, slope, outdoor,
  interior, water, residency, occupancy and body-clearance checks remain active.
- Initial deployment moves the hidden ped once without clearing ambient entities. It
  restores collision and physics before attempting ground placement. Attempts are spaced
  by 100 ms for at most 1.5 seconds, ending earlier on success or actor/interaction loss.
  Actual validated geometry controls success; a false placement-native return alone does
  not discard a correctly positioned target. A failed placement is never revealed. If
  deletion is delayed, the still-owned hidden target is frozen with collision disabled
  until cleanup completes. Ownership loss stops further placement and physics changes.
- Already loaded destinations skip unnecessary scene/collision/nav-region requests.
  Missing residency retains the existing bounded, cooperative loading path.
- Failure 10 now appends a `routine-start-v2` line with stage, location, failing check,
  placement count/return and expected, actual, candidate, projected and ground coordinates.
  Existing portrait diagnostics and cleanup ownership are retained.

## Local Rockstar source evidence

References use the local `RDR3-Decompiled-Scripts-master/1491.50` checkout:

- `property_use_core.c:56908` selects a candidate with `GET_SAFE_COORD_FOR_PED` using
  `false, 0`; it does not establish that querying the selected point returns itself.
- `camp_horseshoeoverlook.c:4372` uses separate 0.5/0.5/2.0 tolerances when comparing a
  ped with a scenario coordinate. `act_caunc_rustling.c:12390` likewise uses 1/1/3.
- `act_camp_fff_light.c:2863-2873` uses placement without gating on its return, while
  `finale1.c:62383-62388` retries placement across updates. The mod additionally validates
  the resulting geometry rather than treating either pattern as sufficient by itself.

## Validation and remaining game check

All sixteen regression suites pass, including 581 spawn/catalog checks, 410 runtime
checks and 536 portrait-startup checks. The new cases reproduce rejection of a valid
standing ped by the old distance test and rejection risk from a non-idempotent nav
selector. They cover unchanged saved-point validation, false native returns with valid
geometry, delayed settling, bounded timeout, actor loss, unsafe height/position, changed
ground, occupied space and preservation of a foreign scene loader. Tests simulate native
responses; they cannot verify engine ground placement or establish the crash cause.
All eighteen protected portrait, card, handoff, corpse-photo and payment functions,
the complete `contract_data.h` and final card draw ordering match the previous delivery.

Release/x64 builds successfully to `dist/TestScript.asi` using explicit workspace
`OutDir` and `IntDir` overrides. Nothing is installed into the game directory.

```powershell
$bountyMsbuild = 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe'
$bountyRoot = (Get-Location).Path
& $bountyMsbuild (Join-Path $bountyRoot 'rdr2 scripting environment\samples\Pools\Pools.vcxproj') /t:Build /p:Configuration=Release /p:Platform=x64 "/p:OutDir=$bountyRoot/dist/" "/p:IntDir=$bountyRoot/tmp/Release-x64/" /m /nologo /verbosity:minimal
```

Close the game fully, replace the previous ASI with this build, reload story mode and
press **U** once while idle outdoors. Confirm that a card is issued and its target is
standing at street level. If startup still fails, the new log line identifies the exact
stage to investigate. If the crash returns, preserve the new startup and cleanup logs;
the dialog alone does not establish whether the mod caused it.
