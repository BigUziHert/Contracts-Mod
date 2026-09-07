# Seated target and routine fixes — 2026-09-07

Implemented on `dev`, based on `2e2212e`. No portrait/card/handoff/payment function,
relationship group or loadout configuration changed. Native-shim validation and the
Release/x64 build pass; actual RDR2 animation and combat still need the checks below.

## Cause and corrections

Native area wandering can put a target into an ambient seat or scenario. Engagement
previously submitted combat over that scenario, or adopted an engine combat flag
without any task. Both the flag and a combat task queued at status 0 counted as
healthy forever. The overlay displayed policy state alone, hiding the stalled task.

Locations refer to this change's final production source under
`rdr2 scripting environment/samples/Pools/`.

| Fix | Implementation and location |
| --- | --- |
| A1 | `script.cpp:1442`: combat exit toward the player, immediate-exit fallback, and a real combat task for seated adoption. Still-seated recovery clears tasks immediately. Every request records `combatRequestMs` (`:62`); only first scripted engagement draws the stored weapon. Actual task submission also records retry timing, including seated adoption. |
| A2 | `script.cpp:1491`, `contract_data.h:60`: after 2,500 ms, a scenario/seat or queued task is unhealthy even with engine combat true. The existing 1-second absence grace and 3-second retry limit apply; restraint/get-up guards remain. |
| A3 | `target_ai_logic.h:43,93,140`: only a fresh engine-combat edge selects adoption. Standing adoption does not invent a task timestamp. Explicit task-history state allows recovery at 1,200 ms after clock-zero adoption and absence from 200 ms without unsigned underflow. |
| A4 | `script.cpp:1503`: search requests a directed normal scenario exit toward the last-seen position, falling back to the immediate hint. Search remains transition-only. |
| A5 | `routine_runtime.h:321`: observed ambient scenarios receive a normal exit before scheduled Travel/Wait. No new scenario reads during travel or blocked updates. |
| A6 | `routine_debug.h:64`, `routine_debug_view.h:17,89`: sample combat status, engine combat, scenario and sitting before live-target priority labels. Fighting/search/preparation rows expose compact evidence, bounded to 64 characters. |
| B1 | `routine_logic.h:160,177`, `routine_runtime.h:214,230`: pause/fade/fallback reselection preserves healthy travel/wandering at the same destination, its exact centre/radius and trip clocks. New destinations still travel. |
| B2 | `routine_logic.h:220,226`: a stall retry resets its best-distance baseline; healthy wandering resets the recovery budget. Continuously failing tasks still have bounded retries. |
| B3 | `routine_runtime.h:295`, `routine_spawn.h:46,85`: arrived stops skip occupancy/body-clearance checks permanently. Residency, ground/exterior, interior, water and visiting-hour checks remain; travelling stops retain full validation. |
| C1 | `script.cpp:1720`: engaged policy state discovers the target, including intimidation without an engine combat flag. |
| C2 | `script.cpp:73,1542,1720`: observed death before contact excludes later corpse damage from starting crime/wanted tracking. This also covers peaceful discovery followed by external death. Existing hostile contact and current lethal player damage retain law tracking. |
| C3 | `script.cpp:935,950`: `interaction_interrupted` maps to Interrupted at both deployment checks even when the player remains available. Existing guards and cleanup are unchanged. |
| C4 | `routine_logic.h:13,196`, `routine_runtime.h:193,330`: one 1 m/s walking estimate with a 1.2 detour factor drives ETA and the per-trip/native timeout. The deadline keeps a 300-second floor; longer routes receive their distance-derived allowance. |

The reason-specific scenario-exit ordering follows Rockstar's
`theatre_ticket_taker.c:12399`, documented in
[routine-activities-and-transit.md](routine-activities-and-transit.md). The 2.5-second
settle window and recovery delays are mod policy. A first recovery normally becomes
eligible about 3.5 seconds after a continuously stuck engagement; frame sampling and
restraint can delay it. The exit hint and immediate recovery still need engine testing.

## Regression coverage and validation

- `target_ai_bridge_tests.cpp`: seated/scenario and sitting-only provocation/adoption,
  refused exit hints, exact exit/clear/task ordering, seated/queued recovery, standing
  and seated adoption timing, stale-flag reacquisition, search exits and restraint.
- `target_ai_logic_tests.cpp`: clock-zero adoption recovery, fresh edge versus stale
  level, and preserved debouncing/retry behavior.
- `routine_logic_tests.cpp`, `routine_runtime_tests.cpp`: unchanged pause/fallback
  destinations, seated pause without projection/tasking, phase departures and invalid
  stop exit ordering, independent versus continuously failing wander retries, detour
  progress, occupied arrived centres, environmental rejection and a route beyond 300 s.
- `routine_debug_bridge_tests.cpp`, `routine_debug_view_tests.cpp`: priority evidence,
  separate seat/scenario flags, queued/running/missing/invalid statuses and text bounds.
- `contract_lifecycle_tests.cpp`: extracted real discovery function, intimidation
  discovery, external death before/after neutral discovery, later corpse damage and reset.
- `portrait_start_tests.cpp`: both interruption mappings with a living available player,
  plus the existing cleanup, placement and capture-boundary regressions.

`./tests/run-tests.ps1` passed all **20 suites** using Visual Studio 2022, C++20 and
the runners' `/W4 /WX` settings (the existing keyboard shim retains `/wd4100`).

| Suite | Passed |
| --- | --- |
| Target AI policy / bridge | 7 / 8 scenario groups |
| Handoff / keyboard | passed / 50 checks |
| Card texture | 55,804 checks; both negative controls rejected as expected |
| Spawn/input/pause | 2,771 checks |
| Portrait preparation / cache | 586 / 14,811 checks |
| Owned ped cleanup / photo self-test | 189 / 13,844 checks |
| Contract lifecycle | 245 checks |
| Routine spawn/catalog / logic | 2,366 / 8,501 checks |
| Routine plan / runtime / card | 156,021 / 825 / 107,693 checks |
| Debug view / blips / bridge | 70 / 977 / 1,670 checks |
| Startup trace writer | 11 checks |

All extraction runners passed their exactly-one-match checks, including the final
`UpdateRoutineDebug(); UpdateCard(); WAIT(0);` tail assertion. Comparing production
functions with the baseline found only the five intended `script.cpp` functions
changed: `SpawnTargetWithPhoto`, `EnterCombat`, `UpdateHumanTarget`,
`UpdateCrimeTracking` and `CheckTargetFound`. `ScriptMain` and protected functions
remain identical after CRLF normalization. Added native calls match the SDK signatures
at `natives.h:6204,7072,7088,7089,7090,7100,10296`. `git diff --check` is clean.

The README's MSBuild Release/x64 command succeeded with **0 warnings and 0 errors**,
explicitly overriding `OutDir` to workspace `dist/` and `IntDir` to `tmp/Release-x64/`.
`dist/TestScript.asi` is **318,976 bytes**, SHA-256
`CB3F89A38B85587E84368774F67C2E1F1EBDADD439D4A05A569A548B8D0D6E84`.
Build output is uncommitted and was not deployed into the game. The three pre-existing
tracked deletions under `dist` remain excluded from the changes.

## Required in-game checks

1. Fully restart RDR2 with the new ASI. Provoke targets at a five-finger-fillet table,
   on a bench, and while smoking. Check standing up, weapon choice and actual fighting;
   record the Doing evidence if a task remains queued/seated or recovery fires.
2. Open/close the pause menu during an ambient scenario. The target should continue
   the same activity when its destination remains current. Then observe a scheduled
   departure and normal scenario exit.
3. Break sight for eight seconds, stay hidden through the ten-second search, then
   return into sight. Check directed search exit and a real renewed combat task.
4. Lasso, hogtie, release and knock down an engaged target. Recovery must wait until
   it can act, retain its loadout, and avoid repeated forced weapon draws.
5. Observe a bystander at the wander centre and a long same-town route. Check stable
   wandering, physical progress and eventual arrival. Check overlay readability.
6. Discover peacefully, let another actor kill the target, then damage the corpse:
   that later damage must not start the contract's crime clock. Interrupt startup with
   a menu/fade/mount/combat and confirm interruption messaging and clean cancellation.
