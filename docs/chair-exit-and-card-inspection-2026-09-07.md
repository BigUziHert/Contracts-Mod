# Chair exit and displaced inspection followup — 2026-09-07

**Later test:** the inspection safeguard described below did not resolve framing.
It was rolled back after a downward-camera report, and replaced with bounded,
read-only diagnostics. The chair changes remain. See the [current card investigation](card-framing-investigation-2026-09-07.md);
the build hash and collision-helper behavior below describe the earlier `e473cde` build.

The owner tested `69e722b` at a Van Horn five-finger-fillet chair. The target appeared
to pop out immediately on aggression, then drew a knife and attacked. This confirms
that combat resumed, but does not establish which of the immediate-exit hint,
same-frame weapon draw/task submission, or engine reaction caused the visible snap.
An immediate pop is inconsistent with the delayed recovery being the sole cause.

## Animated departure before combat

`EnterCombat` now requests a combat scenario exit, falling back to a normal exit,
and uses ordinary `CLEAR_PED_TASKS(ped, true, false)`. It records a pending engagement
instead of drawing a weapon or issuing combat while the ped remains seated, in a
scenario, or in a scenario-exit animation. Once those observations clear and restraint
guards permit action, it submits combat once. Seated native adoption follows the same
path; standing native combat retains its task.

A running exit is initially left untouched. Its fixed eight-second allowance is
measured from the request, not refreshed by per-frame observations. An exit that has
not started retains the 2.5-second allowance. The existing one-second absence grace
and three-second retry limit then permit another normal exit/clear attempt. Neither
engagement nor search uses immediate-exit hints or immediate task clears. Retries
preserve the first draw until the original engagement actually starts; recovery of a
previously submitted fight never redraws. Search cancels a pending combat completion.
The debug label is Preparing to fight while an exit is pending.

Local build-1491.50 precedents:

- `beat_public_hanging.c:14333–14366` uses a combat-exit hint and ordinary clear before
  a combat sequence. `theatre_ticket_taker.c:12399` chooses hints by departure reason.
- `fillet_launch_sp.c:565` observes `IS_PED_EXITING_SCENARIO(ped, true)` separately.
- `act_camp_fff_light.c:1161–1165` guards an ordinary clear while an exit is already
  underway. This is a shared companion helper, not a verified bounty encounter.

The eight-second animation allowance is mod tuning, not a known universal chair-exit
duration. Native shims can verify deferred tasking and absence of immediate operations;
they cannot prove that every ambient scenario supplies a smooth exit animation.

## Inspection collision safeguard

The next contract's physical card remained off to the right, including after putting
it away and reopening with I. Flip controls still worked. The back information is a
separate screen-space draw, so its visibility does not prove correct physical-card
alignment. Reopening creates another prop, and normal mod code does not set gameplay
camera position or orientation. No retained handoff offset was established as the cause.

The missing safeguard is collision prevention for remote/I inspection props. Handoff
attachment already disables card collision, but direct inspection previously lacked
the equivalent. A helper now disables collision on the tracked pending/active inspection
card and asks the gameplay camera to ignore that entity each update, including startup
yields. It checks the current living inspector and the exact matching native item task;
only the mod's owned pending prop may be handled before that task starts. Foreign tasks,
missing cards, ended inspections and player replacement are excluded.

Rockstar applies `SET_GAMEPLAY_CAM_IGNORE_ENTITY_COLLISION_THIS_UPDATE` to inspected
documents at `beat_murder_campfire.c:1245` and `beat_frozen_to_death.c:660`, around their
item-interaction task setup. This is a source-backed collision fix candidate; it does
not prove collision caused this particular rightward displacement. No camera offsets,
forced reattachment or inspection-task restarts are added. Protected portrait, card,
handoff and payment functions retain their implementation; the helper runs outside them.

## Validation and build

`./tests/run-tests.ps1` passed all **21 suites** with the Visual Studio 2022 toolchain.
The combat bridge passes **9 scenario groups**, including deferred draw/combat, normal
fallback, timed exit retries, already-running exits, late completion, restraint and
search cancellation. Every simulated tick rejects immediate-exit and immediate-clear
operations. The debug bridge passes **1,704 checks** with the pending-exit label.

The new `card_inspection_camera_tests.cpp` suite passes **128 checks** against the real
inspection helper, ownership/player guards, generic startup wait and extracted frame
tail. It covers pending owned cards, active owned/native fallback cards, fresh reopened
handles, foreign-task exclusion, unavailable players and per-frame integration. Four
existing suites that extract `WaitUntil` received a boundary shim; the full card texture,
portrait cache, capture preparation, photo diagnostic and handoff suites remain green.

Production entry points are `script.cpp:1461` (`EnterCombat`), `:1506`
(`UpdateHumanTarget`) and `:1032` (`MaintainCardInspectionCamera`). The helper runs in
the startup wait at `:411` and gameplay frame at `:2304`, before the unchanged final
debug/card/yield calls. Comparing existing static functions to `69e722b` found only
`EnterCombat` and `UpdateHumanTarget` changed; all protected portrait/card/handoff/payment
functions remain identical after CRLF normalization. `git diff --check` passes.

The added calls match the bundled SDK: ordinary task clear at `natives.h:10172`,
scenario-exit observation at `:10543`, normal-exit hint at `:7089`, card collision at
`:2004` and gameplay-camera entity exclusion at `:1028`.

MSBuild Release/x64 succeeded with **0 warnings and 0 errors**, explicitly writing
`OutDir` to workspace `dist/` and `IntDir` to `tmp/Release-x64/`.
`dist/TestScript.asi` is **319,488 bytes**, SHA-256
`EF9A04E310F19E07792B93A627980F8E06F946D2C78B9385B0A2F4A7F02AAA47`.
No game deployment or build-output commit was performed. The three pre-existing
tracked deletions under `dist` remain excluded.

## In-game verification

Fully restart the game with the updated ASI. At a fillet chair, bench and smoking stop,
provoke once and observe the complete departure before the weapon draw. The panel
should move from Preparing to fight to Fighting. Test restraint during departure,
then break sight and allow search to finish; no delayed combat should restart afterward.

Inspect a fresh remote contract, put it away and reopen with I, then replace it with U.
Check the physical front card is in view before flipping and zooming. Also inspect a
clerk-issued card. If the rightward position persists, capture the whole screen showing
the physical card and native prompts; the collision cause remains unconfirmed and the
next investigation should distinguish attachment, inspection-camera state and camera mode.
