# Town routine delivery — 6 September 2026

This records the original routine delivery. Its placement checks and downloadable
artifact are superseded by [the startup placement fix](routine-spawn-fix-2026-09-06.md).

Code revision: `908c1c7` on `dev`. The normal Release/x64 artifact is
[`dist/TestScript.asi`](../dist/TestScript.asi), 276,992 bytes.
SHA-256: `26986266714143EE5D09EA7D1434A09F807BD29C8CC488E40069B16C77B7F862`.
It was built into the workspace; nothing was installed into the game directory.

## Stage status

| Stage | Delivered | Disabled or remaining |
| --- | --- | --- |
| 1 — Locations/spawning | 27 sourced exterior candidate areas in five towns; occupation filtering; separate candidate, wander, discovery and town-search radii; bounded streaming and two-location fallback; hidden deployment revalidation. | Ground/navigation natives and source anchors still require in-game verification. Construction and other changing sites without availability evidence are omitted. |
| 2 — Daily routines | Four habitual destinations, bounded schedule variation, physical walking, fixed-centre wandering, waiting, arrival/closing checks, midnight windows, travel retry/cooldown, priority suspension and resumption. | Actual navigation routes, engine combat flags and streaming behavior need story-mode tests. |
| 3 — Cards | Occupation, town and four truthful route clues on the existing back panel. Fixed broad search circle does not track the undiscovered target. Portrait and inspection mechanics retained. | Confirm font fit on the player's resolution and normal game rendering. |
| 4 — Activities | Verified smoking and drinking scenarios; enabled/space/entry checks; short timed visits and wandering fallback; interruption and prop-exit handling. | Genuine poker, blackjack, five-finger fillet, theatre attendance, commerce, haircuts, working/tool/furniture animations remain disabled. Props and animation exits need in-game verification. |
| 5 — Tram | Reviewed existing train research and local passenger/ambient-manager scripts. Walking is the complete current transport path. | Ambient tram use is disabled: no verified nonwarp boarding/disembarking sequence with safe encounter, corpse and borrowed-vehicle lifecycle was established. No speculative tram state machine was shipped. |

Exterior visiting windows are authored mod policy, not claims about Rockstar shop hours.
The implementation never starts a game, takes a table participant, writes a show/train
global or promises a service on the card. The detailed evidence and unlock requirements
are in [activities and transit](routine-activities-and-transit.md).

## Bugs found and fixed

| Finding | Fix and production reference |
| --- | --- |
| Fixed spawn data used one radius for unrelated purposes and included an incorrect Strawberry height. | New contracts exclusively use sourced runtime plans; legacy fixed rows are no longer selected. See [runtime preparation](<C:/Users/caleb/Desktop/Bounty Hunting Mod/rdr2 scripting environment/samples/Pools/routine_runtime.h:55>) and [location evidence](routine-location-sources.md). Search, wander and discovery now have separate values. |
| Loading can yield while the player enters a menu, fades or becomes busy. | [StartContract](<C:/Users/caleb/Desktop/Bounty Hunting Mod/rdr2 scripting environment/samples/Pools/script.cpp:1554>) rechecks interaction eligibility before deleting the old hunt. Initial selection failure also preserves it. |
| A location can unload during portrait capture, or native ground placement can fail/drift. | [Deployment validation](<C:/Users/caleb/Desktop/Bounty Hunting Mod/rdr2 scripting environment/samples/Pools/routine_runtime.h:91>) reloads once if needed and rechecks the living subject; [placement](<C:/Users/caleb/Desktop/Bounty Hunting Mod/rdr2 scripting environment/samples/Pools/script.cpp:904>) checks success and actual coordinates while hidden. Failure releases the new portrait/ped without exposing a card. |
| Nav projection or a higher ground surface could select the wrong area/level. | [Spawn validation](<C:/Users/caleb/Desktop/Bounty Hunting Mod/rdr2 scripting environment/samples/Pools/routine_spawn.h:34>) bounds horizontal projection, ground slope, source height and upward displacement; also checks water, interiors and occupied space. |
| Periodic fallback reconsideration could reset a stalled trip's retry/deadline accounting indefinitely. | [Routine updates](<C:/Users/caleb/Desktop/Bounty Hunting Mod/rdr2 scripting environment/samples/Pools/routine_runtime.h:239>) reconsider fallback habits only after travel settles; failed trips retain their two-retry and absolute-deadline limits. |
| A lingering player-combat flag could prevent routines resuming after the existing search policy completed. | [AI integration](<C:/Users/caleb/Desktop/Bounty Hunting Mod/rdr2 scripting environment/samples/Pools/script.cpp:1449>) respects the completed policy while still blocking actual combat tasks, new engagement, other fights and restraints. |
| Main-loop pause/fade guards can skip all routine ticks. | [Pause snapshot](<C:/Users/caleb/Desktop/Bounty Hunting Mod/rdr2 scripting environment/samples/Pools/routine_runtime.h:217>) requests one fresh selection on resumption. |
| A scenario's own bottle or retiring prop could reject the target's destination as occupied. | [Activity clearance handling](<C:/Users/caleb/Desktop/Bounty Hunting Mod/rdr2 scripting environment/samples/Pools/routine_runtime.h:229>) uses residency checks during the activity and a five-second exit grace, then restores full clearance checks. |

Ground checks are conservative, not proof of every possible roof or changed world mesh.
Five sampled static rays supplement a source-backed occupancy query. Initial source
preparation takes at most two three-second location budgets, plus one bounded residency
reload after capture if necessary. The global scene-loader API has no ownership handle;
the guarded start/stop cannot distinguish replacement by another script across a yield.
No navigation-region removal handle exists in this SDK. These engine limitations are
documented rather than hidden behind the native-shim test results.

## Validation

All **16 suites** passed through `tests/run-tests.ps1`. The complete Release/x64 build
succeeded at every implementation stage, including the final activity/exit changes.

| Suite | Final result |
| --- | --- |
| Target AI policy | 5 scenario groups passed |
| Handoff policy | Passed |
| Keyboard | 50 checks |
| Card texture | 55,804 checks; both intentional regression controls rejected as expected |
| Spawn/input/pause | 2,751 checks |
| Portrait startup | 247 checks |
| Portrait cache | 14,811 checks |
| Owned ped cleanup | 189 checks |
| Photo diagnostic driver | 13,844 checks |
| Native AI bridge | 4 scenario groups |
| Contract lifecycle | 190 checks |
| Routine spawn/catalog | 456 checks |
| Routine policy | 8,468 checks |
| Routine plans/clues | 104,529 checks |
| Routine native bridge | 361 checks |
| Routine card layout/clues | 80,813 checks |

Tests compile real production headers or functions extracted with exactly-one-match
requirements. They cover midnight/arrival closing, inaccessible/occupied fallbacks,
bounded retries, combat/restraint priority, resumed routines, truthful clues and cleanup.
They do not simulate real game physics, animation, rendering or private minigame scripts.

An independent comparison against `733eb83` found all 18 protected functions unchanged
after newline normalization: portrait capture/lookup/release/handle checks, maintenance,
slot selection/binding, card create/destroy/open/attach, face drawing/update, handoff
begin/update/stop, payment settlement and corpse photography. Every comparison matched
exactly one declaration in each version. `contract_data.h`, including all Card constants,
was unchanged. `UpdateCard()` remains last in the frame before `WAIT(0)`.

Exact final commands, run from the workspace:

```powershell
.\tests\run-tests.ps1
$bountyMsbuild='C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe'
$bountyRoot=(Get-Location).Path
& $bountyMsbuild (Join-Path $bountyRoot 'rdr2 scripting environment\samples\Pools\Pools.vcxproj') /t:Build /p:Configuration=Release /p:Platform=x64 "/p:OutDir=$bountyRoot/dist/" "/p:IntDir=$bountyRoot/tmp/Release-x64/" /m /nologo /verbosity:minimal
```

Focused implementation commits: `29c92bb` locations/spawning; `d1f01c2` daily walking;
`3a4a3b4` habit cards; `908c1c7` ambient activities and game/tram evidence.
The pre-existing local deletions under `dist` were preserved and excluded from commits.

## Ordered in-game checklist

These are engine checks still outstanding; the automated policy/ownership checks above
are already complete.

1. Issue contracts in all five towns at day/evening/late hours. Verify accepted targets
   stand outdoors at street level, with no rooftop, interior, water or blocked spawns.
   Inspect both card faces and all six clue lines; the broad search circle must stay fixed.
2. Watch a complete destination transition and one midnight/closing transition. Confirm
   physical walking, a fixed wander centre, unchanged identity and no position snap.
3. Watch smoke/drink entry, props, timed exit and wandering. Occupy a destination and
   confirm fallback instead of pushing another actor out. Observe rejected/unloaded paths.
4. Provoke during travel and activity; fight indoors if the target enters. Escape behind
   cover, allow the search to expire, then verify routine resumption. Repeat with lasso,
   hogtying, full hogtie, release, ragdoll and getting up.
5. Pause/fade, skip time, fast-travel away and return. Verify safe resumption and bounded
   streaming failure. Player death/change should clear the current contract as before.
6. Kill a target during an activity, photograph the corpse and collect one reward. Replace
   another hunt with **U**, inspect with **I**, and verify the next portrait is fresh.
   Repeated contracts must retain the existing 32-slot exhaustion protection.

For extension instructions, see [target-routines.md](target-routines.md), the sourced
[location catalog notes](routine-location-sources.md) and [README](../README.md).
