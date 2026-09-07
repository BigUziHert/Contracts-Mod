# Routine radius, location and contract preparation changes

The owner reported short repeated wander segments, requested 35 m wandering and a matching search circle, corrected six catalogue entries, and asked for location rejection to stop requiring another contract request. They also reported that card framing had returned to normal. This change leaves card inspection and portrait capture unchanged.

## Wandering and search

All 60 stops use `RoutineData::kWanderRadius = 35.0f`. The native wander task, generated contract's search radius and yellow circle use that value. The circle starts at the validated stop and moves, using the same handle, when another valid stop is assigned. It does not follow individual footsteps. Travel between stops can extend outside the circle.

Only the native wander task's radius changes. Its other arguments and the routine controller's task/recovery rules remain unchanged. The larger area may help the game find ambient opportunities, but automated tests cannot establish that the former radius caused the reported stopping and turning, or that 35 m guarantees scenario entry. Aim discovery now uses the matching 35 m distance from the target plus line of sight; the combat AI's 45 m reacquisition and 55 m retention limits remain unchanged.

## Catalogue corrections

Removed Van Horn Northwest road, Rhodes Northern camp, Saint Denis Stable entrance and Strawberry Stable entrance. Added Saint Denis Eastern dock approach at `(2822, -1415, 45.5)` and moved Strawberry South-loop street to `(-1827, -415, 161)`. The new dock supports daytime laborer/dock-worker routes. IDs and provenance are recorded in [location sources](routine-location-sources.md).

## One request through temporary startup failures

Preparation first tries the plan's current-phase stop and its all-day fallback, then additional compatible current-phase/Rest locations and other towns. Each pass attempts at most eight distinct locations and starts no new attempt after six seconds. A current streaming wait may finish its existing three-second bound. An accepted alternate updates the route before card clues are generated.

Clerk and remote requests now share one pending request. Location rejection, interrupted preparation and pending cleanup retry after one second; model, NPC-pool, creation and portrait failures retry after three seconds. No repeated failure subtitle is shown. Repeated **U** presses do not create duplicate requests. Pauses, fades, existing item interactions, combat and other interaction blockers defer the pending attempt until the player can proceed. Player loss/change, a completed hunt awaiting payment, or **End Contract** cancels it.

Failed initial preparation preserves the previous hunt. Replacement still clears that hunt once preparation succeeds. A later spawn/capture/placement failure can therefore leave no active hunt temporarily, while the same pending request continues. A provisional target must finish cleanup before another attempt. Publication requires both the target and its portrait; success runs the existing clerk handoff once, or opens the card remotely if the clerk is no longer available.

The `NO SAFE TARGET LOCATION AVAILABLE` message is removed from player feedback. Rejected locations remain logged for diagnosis. Ground, navigation, water, exterior, height and clearance checks are unchanged.

This removes the terminal location-error path, not every possible game-resource limit. A request cannot complete while the engine continually refuses all valid locations or required resources. The existing 32 bound portrait-slot limit still reports that a game restart is required, preserving the current hunt; changing that cache lifetime is outside this change. Capture/card/handoff/payment function bodies are unchanged.

## Validation

`tests/run-tests.ps1` passed all **23 suites** under MSVC C++20 `/W4 /WX`. Changed coverage includes 2,194 spawn/catalogue checks, 940 native routine-runtime checks, 2,900 spawn/input/retry checks, 80 new search-area checks and 82 new contract-start integration checks. The new startup suite extracts the real `StartContract` and request handler and exercises preservation, provisional cleanup, retry, stable definition publication and one card delivery. Native boundary responses are simulated.

Release | x64 MSBuild passed with **zero warnings and zero errors**, using a workspace `OutDir`. Artifact: `dist/TestScript.asi`, **324,096 bytes**, SHA-256 `EB313620A238C2E88C5C73032B8BF1D40F8EEFF52E7C27C3DF656D9CE77D0598`. Nothing was installed in the game directory.

A source comparison against `fcc34ca`, normalizing line endings, confirmed 31 capture/card/handoff/payment/inspection-trace functions unchanged. `contract_data.h`, `handoff_logic.h`, `routine_logic.h` and `routine_spawn.h` are unchanged. The clerk prompt routes new requests through the pending handler; card drawing remains immediately after routine debug and immediately before the final frame wait.

In-game checks: fully restart RDR2 with the rebuilt ASI, request several contracts through **U** and the clerk, inspect/reopen each card, and observe whether the target selects ambient activities with the larger radius. Check both owner-supplied coordinates, verify removed markers are absent, and watch the yellow circle move when the routine changes destination. If preparation takes time, the same request should finish without another input or a duplicate target/card.
