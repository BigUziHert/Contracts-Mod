# Ambient fallback for unavailable routine destinations

The owner observed a Valentine target standing still with `Stop: None` and `Waiting for a usable destination`. The controller explicitly issued `TASK_STAND_STILL(ped, -1)` after scheduled and public-fallback destination checks failed. Selection had already cleared the assigned stop. The screenshot does not identify which native validation check failed.

The runtime now retains a fixed fallback area from the accepted initial spawn and updates it on arrival at a routine destination. Failed selection keeps a real authored stop and uses native 35 m wandering there. It never invents an area around the ped's changing position, teleports the target, or uses a failed travel endpoint as its new fallback.

Healthy wandering and ambient scenarios belonging to that area continue while route selection retries. Missing fallback tasks use the existing 1.5-second absence grace and four-second task retry interval, without permanent exhaustion. Selection retries use a five-second interval. Once a usable route returns, the target follows it; accepting the same area preserves its ongoing wandering instead of walking back to the centre. The cached outdoor visit may continue after its authored visiting window while alternatives remain unavailable.

An established wandering visit no longer repeats spawn-point geometry/clearance checks at the old centre, including through task-recovery gaps. Local collision/navigation residency and combat, search, restraint, vehicle and player-availability priority remain enforced. Spawn/deployment and incoming travel checks remain strict. A rejected saved endpoint can use another bounded point in its authored area; changing that point also replaces the movement task so it cannot keep navigating to the old endpoint.

The debug panel reports `Wandering while route recovers` only when the native task is observed, otherwise `Wander task pending / recovery`. Actual ambient scenarios keep their own labels. The stop marker and 35 m search circle follow the cached authored area during recovery; `Valid N` reflects pending route validation rather than a stand-still command.

## Validation

All **23 suites passed** through `tests/run-tests.ps1` under MSVC C++20 `/W4 /WX`. Changed suites passed 8,630 routine-policy checks, 1,542 native runtime checks, 2,203 debug bridge checks and 91 search-area checks. Coverage exercises the reported 35 m wandering case, transient centre-query failures, unavailable phase destinations, ambient scenario preservation, travel failure/cooldown, unrelated mid-travel scenarios, lost-task recovery, same-area promotion, changed endpoints and higher-priority suppression. Native responses are simulated; actual game navigation still requires testing.

Release | x64 MSBuild succeeded with **zero warnings/errors**, writing only to workspace `dist/TestScript.asi`: **325,632 bytes**, SHA-256 `494027358A24843E54FAE40F1D8FCAC8EE3A6554A943466EE9833A7FC52C3E4A`. The game installation was not modified. Source comparison against `f197a2c` confirmed 31 capture/card/handoff/payment/inspection-trace functions unchanged; the spawn validator and contract/card constants are also unchanged.

In game, revisit the Valentine auction yard and observe a phase transition or unavailable destination. The target should retain a named stop and continue ambient wandering or its current scenario. A route becoming usable should produce walking to that stop. Provoke, lasso and release the target to confirm routine fallback never overrides the encounter. Natural ambient pauses can still occur; this change removes the mod's forced indefinite standing.
