# Contract inspection framing investigation — 2026-09-07

## Update: weapon put-away reproduction

The owner reproduced the out-of-frame card by pressing **U** while holding a
knife or gun: inspection began while the player was still putting the weapon
away. `OpenCard` previously admitted drawn weapons and started the item task
without waiting for that transition.

`PreparePlayerForCard` now checks both weapon hand slots, requests animated
holstering once with `_HIDE_PED_WEAPONS(ped, 2, false)`, and waits for empty/unarmed
hands and no `_IS_WEAPON_HOLSTER_STATE_CHANGING` for 150 ms. Already-running weapon
transitions finish before a holster request is issued. The wait is bounded at
four seconds and aborts if the player becomes unavailable, interaction eligibility
changes, or a carried entity occupies their hands. Carried entities are not dropped.
An initially empty-handed player opens immediately.

The gate runs before card creation, after potentially yielding model streaming,
and before the fallback inspection task. It covers both **U** opens and **I**
reopens through their shared `OpenCard` path. The current contract survives a
failed open and can be inspected with **I** after clearing the interruption.

The hand reads use `GET_CURRENT_PED_WEAPON(ped, &hash, true, slot, false)` for
slots 0 and 1, matching the local build-1491.50 `treasure_hunter.c:2025–2060`.
`beat_booby_trap.c:4228` also waits for the holster-state change to finish before
acting on an unarmed hand. No camera or pose correction is added.

Restart RDR2 with the updated `dist/TestScript.asi`, then try **U** and **I** with
a knife, handgun, long gun, dual guns, and empty hands from both camera views.
The weapon should finish being put away before inspection begins. Check framing,
flip, and put-away, then repeat during a manual holster. This fix addresses the
reported overlap; correct rendered framing still requires that in-game check.

The Release/x64 build completed with zero warnings/errors, with `OutDir` set to
workspace `dist/` and intermediates under `tmp/Release-x64/`. This ASI is 342,016
bytes, SHA-256 `ABCFCDC4B9749EB4FFCC7B17F28E91E07D58D19971CCE67660E3CE698892AD97`.
The 25 existing regression suites passed. The new card-start suite also passed
29,125 checks with `/W4 /WX`, exercising the extracted production readiness
helpers and `OpenCard`: early unarmed hashes, delayed animations, both hand slots,
timeouts, interruptions, model-streaming changes, and both inspection paths.
These simulated natives establish task ordering, not rendered framing. No game
deployment was performed.

## Earlier investigation

The owner reported that the physical contract card could remain off to the right
through repeated opens, occasionally correcting itself. After the `e473cde` build,
the supplied screenshot showed active Zoom / Flip / Put Away prompts with the
camera pointing down at the player's feet and the card out of view. The owner
confirmed this occurs when entering from either first- or third-person, while
ordinary satchel letters/photos look normal. This is an unresolved contract
inspection defect; the prior collision safeguard did not establish a fix.

## Source findings

The normal Bounty code has no camera position, pitch, heading or FOV setter.
The previous `MaintainCardInspectionCamera` helper added physical collision
disabling and a per-frame gameplay-camera exclusion. Both changes are now removed
to return inspection camera/collision behavior to its earlier baseline while
retaining the deferred chair-exit fix. This rollback is not proof either call
caused the new downward angle or that it fixes the earlier displaced card.

Local Rockstar build-1491.50 `generic_document_inspection.c:2536–2544` uses the
same paper intro state as the mod. Its first-person branch at lines 70–82 manages
journal lighting, and line 91 disables cinematic mode; it supplies no camera-angle
correction to copy. `beat_murder_campfire.c:1245` and `beat_frozen_to_death.c:659`
exclude inspected documents from camera collision. That precedent supports the
native's use, but does not identify this defect's cause.

Two setup differences remain hypotheses: the custom GXT title passed to the
owned-prop task where examined Rockstar examples pass document IDs, and the
paper animation family used with a photo prop catalogued under a card animation
family in femga's local `tasks/TASK_ITEM_INTERACTION/readme.md:56`. Neither is
changed without runtime evidence; these values predate the recent AI work and
have produced correctly framed inspections in earlier tests.

Rspect's Cameras is installed, including a global LML `cameras.ymt` replacement.
Its author's [feature description](https://www.nexusmods.com/reddeadredemption2/mods/6010?tab=description)
lists shake, zoom and other camera adjustments. Inspection of the installed ASI
found shake-suppression native references, but no tested position/pitch/heading
setter hashes. Neither that bounded scan nor the normal satchel-item comparison
proves full compatibility; no specific external override was established and no
game files or other mods were changed.

## Bounded inspection measurements

`TraceCardInspection` replaces the safeguard at the same two integration points:
the generic startup wait and the main frame immediately before
`UpdateRoutineDebug(); UpdateCard(); WAIT(0);`. It observes only a tracked owned
prop whose task is pending or the exact primary prop of the current player's
running item task. Unrelated tasks, invalid players, missing props and ended
inspections produce no new native snapshots. It makes no camera, collision,
attachment, pose, visibility or task changes.

The existing `BountyContracts-startup-trace.log` beside the ASI receives
`card_inspection_sample` rows tagged `card-v1`. The trace captures at most once
every 250 ms during the first 15 seconds of an open, with an additional hard
64-sample cap. Missed frames do not replay samples. Closing or replacing the
inspection records an end row and resets the budget for the next card.

Rows include item/state hashes, inspection-task status, attachment parent,
visibility, the current accepted photo-texture flag, card/player transforms,
gameplay and final-rendered camera transforms/FOV, and projection of the card's
origin into screen coordinates. `textureValid` is the stored validity flag,
not an additional texture lookup. The row's `point` is the card position. The
final-rendered camera may describe the preceding rendered frame; compare settled
samples, not a single transition. Projection refers to the model origin, not all
four corners of the card or proof of visible pixels. These measurements distinguish
an invisible/unattached prop from a displaced prop or camera and retain evidence
from both failed and successful opens.

## Next in-game check

Install the new workspace `dist/TestScript.asi` and fully restart RDR2. Open a
contract, leave it open for about three seconds, flip once, then put it away and
reopen with I. If the angle fails, record whether it was the first open or the
reopen and the approximate time. The trace is automatic; no diagnostic key or
special test contract is required. A normal-looking open is useful for comparison.

This build is an inspection rollback and diagnostic step, not a confirmed framing
fix. New-contract startup behavior and the previously reported location-error /
missing-card issue are unchanged.

## Validation

`./tests/run-tests.ps1` passed all 21 suites with MSVC. The revised inspection
suite passes 13,513 checks against the extracted production helper, including
ownership refusals with no snapshot reads, delayed startup, sampling limits,
put-away/replacement isolation, output fields and extreme-coordinate truncation.
It provides no camera/collision/attachment/task setter shims, so adding those
mutations would fail compilation. Existing texture, portrait, handoff, AI, routine
and lifecycle suites remain green. Native shims do not validate rendered framing.

The Release/x64 build completed with zero warnings/errors, writing only to workspace
`dist/` and `tmp/Release-x64/`. The ASI is 323,072 bytes, SHA-256
`1D22202BF21BAAE3F7CCCA59441B3A84DCFA0EF35EFD8B99DE74533035C52D74`.
Existing static function bodies compared against `e473cde` are unchanged except
the removed collision helper. Protected portrait/card/handoff/payment functions,
card constants and chair AI are unchanged. The new trace calls match the bundled
SDK's entity, item-task, camera and projection signatures. The diagnostic text
uses truncating formatting so an extreme coordinate cannot abort the game by
overflowing its output buffer. No game deployment or ASI commit was performed.
