# Routine testing display

The normal `dist/TestScript.asi` now includes an initially enabled testing display.
**F8** toggles its markers and text together; the choice lasts until the next game
restart. Replace the previous ASI while the game is closed, then restart story mode.

## Red map markers

All enabled entries in `routine_locations.h` receive named red dots on the map and
radar, including sites outside the active contract's town. These mark authored candidate
areas, not a claim that every anchor is safe in every world state. The initial spawn
and each arrival still pass the existing ground, exterior and clearance checks.

Two additional named red markers show the current contract's exact prepared spawn and
its current validated destination. The latter follows destination changes, not the
target. These may overlap a catalog marker because runtime projection is deliberately
small. Location markers stay available between contracts. Hiding debugging removes its
owned handles; gameplay search, target and corpse blips keep their existing behavior.

The local discoveries catalog defines `BLIP_STYLE_DEBUG_RED` with `COLOR_RED`
(`blip_styles/README.md:86`) and lists the small dot `BLIP_AMBIENT_PED_SMALL`
(`textures/blips/README.md:85`). Rockstar's `marston6.c:78913` uses the matching debug-red
modifier, and `short_update.c:27719` updates coordinates on an existing blip. Creation
failures retry at most three times, spaced two seconds apart; F8 off/on resets that
budget. Existing dots move only when their coordinates change.

## On-screen information

The upper-left text uses the existing `DrawTextToScreen` helper with a dark text shadow.
It samples observations every 250 ms and draws each visible frame before the final card
update, without changing card render-target selection or portrait code.

| Field | Meaning |
| --- | --- |
| Clock / town / occupation | Current game time and the current contract's selected identity. |
| Target distance | Straight-line player-to-target distance from fresh entity coordinates, in metres. |
| Doing | Combat, search, restraint, vehicle and unloaded-area priorities override the routine controller. Fighting, Searching for player and Preparing to fight append observed combat evidence: task status (0 queued, 1 performing, 7 missing), engine combat Y/N, and SCENARIO / SEATED when detected. For example, `Fighting [task 0, engine Y, SCENARIO, SEATED]` exposes a queued seated engagement despite the policy label. Smoking/drinking requires a confirmed matching scenario and is labelled ambient; other observed scenarios say Ambient scenario. The mod does not start them. Missing routine tasks show pending/recovery rather than claiming movement. |
| Stop and distance | Assigned destination and straight-line target-to-stop distance. A fallback label identifies the all-day public destination used outside its normal phase, or the cached authored area used while a route is unavailable. |
| Next planned | The following schedule phase's authored destination and transition time, including this contract's clock offset. Availability can change the actual next stop. |
| Wander / indoors | Configured wander radius (45 m at every stop) and current interior classification. This does not prevent a target from walking indoors. The gameplay search circle uses the same radius and moves to the next stop when the target enters its wander area. During travel it stays at the previous stop. |
| Loaded / routine / open / valid | Current local collision/navigation residency, observed routine task, the assigned stop's visiting window, and route validation status. `Open` is the mod's exterior visiting window, not a shop's business hours. During cached fallback roaming `Valid N` means fresh route selection is pending; it does not command the ped to stand still. `Routine N` during combat means the routine is not the task being observed. |
| XYZ | Current target coordinates, useful when reporting a wrong level or blocked route. |

Preparing to fight also identifies an engagement waiting for a chair/scenario exit;
the combat task and first weapon draw wait for that exit to finish.

When fresh route selection is unavailable, a healthy native task shows **Wandering while
route recovers** and keeps its authored stop marker. A missing task shows **Wander task
pending / recovery**. Observed smoking, drinking or another scenario still takes precedence
over these labels. Periodic selection retries do not hide ongoing fallback activity.

A dead target shows no travel plan. Its body distance and coordinates remain available
while it exists. A missing target or removed corpse shows no stale live measurements.
The panel deliberately exposes hunt information for testing; F8 hides it when testing
normal investigation. It does not issue ped tasks, run route selection, request world
streaming, clear nearby entities or change discovery.

## Quick in-game check

1. Load story mode and inspect the red dots on the map. Their names identify towns and
   locations. Press F8 twice to confirm all debug markers and text hide, then return.
2. Press U, inspect/put away the card, and walk toward the target. Confirm player distance
   changes independently of the target's distance to its assigned stop.
3. Watch a scheduled transition. The debug destination marker should identify the newly
   selected stop while the yellow search circle stays at the previous stop. When the target
   enters the new stop's 45 m area, travel should switch to wandering and the search circle
   should move there. The target need not reach the marker first. The panel should distinguish
   walking, wandering and any smoking/drinking.
4. Provoke or restrain the target. The panel should show that priority without assigning
   another routine task. After death, it should show body distance without a travel plan.
5. Replace/end a contract. Exact markers should update/remove while catalog dots remain.
   Check the card portrait and put-away behavior, and hide debugging with F8 if desired.

Automated native-shim tests cover marker ownership/reuse/retry/removal, snapshot priority
and schedule labeling, and bounded formatting. They cannot verify the marker color,
text placement/readability or native AI behavior on the player's display; those remain
the purpose of this build's in-game test.

At the original debug delivery (`26c1cca`), all nineteen suites passed, including 41 display-format checks, 437 marker checks and
1,192 debug-bridge checks. Release/x64 built successfully using the README's explicit
workspace output overrides. All eighteen protected portrait/card/handoff/payment
functions and `contract_data.h` match `9642060`; the routine, spawn and combat policy
files are unchanged. The downloadable ASI is 305,152 bytes, SHA-256
`40E37DA5D390756FAB0B85B922F3E8349EDF5C86A8C53BEF60B2A762F6AC7284`.
That artifact is superseded by the [startup safeguard and trace build](startup-crash-investigation.md),
which retains these debug features.
