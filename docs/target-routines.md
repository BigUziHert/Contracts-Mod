# Town target routines

## Stage 1: sourced locations and initial deployment

New contracts choose an occupation and a four-destination plan in one of seven towns.
`routine_locations.h` contains public exterior candidate areas, including stable,
livestock, market, dock, shop-front, station, theatre-front and camp areas. Coordinate provenance
and omissions are in [routine-location-sources.md](routine-location-sources.md).
Construction, lumber yards and other world-state-dependent sites without a verified
availability check are omitted. A source coordinate is a candidate, not proof of safety.

`routine_spawn.h` tests loaded collision and navmesh, a small safe-coordinate projection,
ground/slope, anchor height, outside/interior state, water and clearance. Ground may be
at most 0.75 m above its source anchor and within the location's overall height tolerance.
Five upright mask-87 probes sample static clearance; a source-backed half-metre occupancy
test rejects overlapping actors/obstacles. These are conservative engine checks, not a
geometric proof: every enabled area needs visual verification in story mode.

Source precedents (local build 1491.50): `property_use_core.c:56908` for safe-coordinate
flags; `short_update.c:87117,87126-87178` for ground/outside and synchronous mask-87
probes; `spd_sheriffoftumbleweed.c:1911-1924` for occupied arrival with traveller ignored;
`act_fishing01.c:47009-47025` and `beat_dark_alley_ambush.c:12329,5553-5581` for bounded
sphere loading; `rcm_beau_and_penelope21.c:38953,38978` for nav-region requests.
Every called native exists in the bundled `inc/natives.h`.

Initial preparation tries five small offsets per location, with a three-second collision/
navigation budget. It tries the current phase's selected site, then its authored all-day
fallback. It never starts a scene request over an already active one and stops a request
only if its own start succeeded. The API has no ownership handle: replacement by another
script during a yield cannot be distinguished, and remains an engine integration risk.
Navigation-region requests have no removal handle in this SDK.

Preparation happens before replacing the current hunt. Failure there preserves that hunt.
After successful portrait capture, the prepared point is checked again before the hidden
provisional target is deployed and revealed. Failure cleans up its ped and portrait and
issues no card. That later failure can leave no active hunt, as with existing portrait
failure. Revalidation checks the saved point directly instead of selecting another nav
coordinate and requiring the two answers to match. Already loaded destinations do not
start another scene request. The hidden ped regains collision and physics before a
bounded 1.5-second placement wait, with at most one ground-placement attempt per 100 ms.
Actual geometry decides success: horizontal displacement is limited to 0.75 m, while
the ped's body origin may be up to 2 m above the saved ground. The ground under its
actual X/Y must remain within 0.35 m of that surface and pass the same outdoor, height,
slope, water and clearance checks. A native placement return of false alone does not
reject valid geometry. Only initial deployment sets coordinates; routine travel does not.
Location-unavailable startup diagnostics use failure code 10 and a `routine-start-v2`
line identifying the stage, location, failed check and expected/actual coordinates.
See [the placement fix report](routine-spawn-fix-2026-09-06.md) for the regression evidence.

The generated definition lives in stable runtime storage; the existing cleanup hook resets
its plan and controller. Portrait generation, download ownership, slot rotation and all
clerk/payment paths retain their existing implementation.

## Separate areas

- Town search: a fixed 180–310 m investigation circle covering the curated sites and their
  wandering areas. It does not follow the target or disclose its selected destination.
- Candidate radius: 4–12 m around a sourced coordinate, used only for validation.
- Wander radius: 12–26 m around a fixed validated destination point.
- Discovery: 45 m plus existing aim/LOS rules; direct interaction, damage or combat still
  discovers the target. Broadening the search circle does not broaden aim discovery.

`ContractDef::searchRadius` remains a legacy field; generated definitions use it for
the 45 m discovery threshold. Routine search and wander do not consume it.

## Availability policy

Enabled places are exterior visits. Work areas use 06:00–18:00, shop fronts/markets
08:00–20:00, evening gathering areas 17:00–02:00, and public fallback areas all day.
These are authored visiting windows, **not researched business opening hours**. No service,
indoor game, haircut, purchase, performance or sleeping accommodation is implied. Future
venue activities require separately verified opening, active event and seat availability.

## Extension points

Add a town's search centre/radius and sourced `Location` rows, each with a unique id,
occupation mask, phase, bounded candidate/height/wander dimensions and a source reference.
Every supported occupation needs one compatible site per phase and an all-day Rest
fallback. Add matching archetypes in `RoutineModels`; reject unsupported town/role pairs.
Update `GeneratedOccupation` too: every enabled location must be reachable by an actual
generated occupation, not merely by a profile that only tests construct. Saint Denis
generates laborers as well as dock workers so its market/stable day areas are reachable.
Never enable construction or interiors merely because a marker exists. Record the world
state/venue checks first, then add tests and validate the accepted points in game.

`routine_plan.h` chooses stable destinations and supplies truthful card data;
`routine_logic.h` is the native-free scheduling/recovery policy. The runtime owns native
observations and tasks. Tests include real production headers and extracted functions;
they simulate native responses and cannot establish engine navigation or rendering.

## Staged validation

Stage 1: catalog, spawn, scheduling-policy and plan suites plus all existing suites;
full Release/x64 build to `dist/TestScript.asi`. Subsequent routine/card/activity integration
is recorded below as each stage is completed.

## Stage 2: physical daily travel

Each contract has four fixed clue destinations and a deterministic schedule offset of
up to 30 minutes. Base phases are Rest 00:00–06:00, Work 06:00–14:00, Shops 14:00–18:00,
Leisure 18:00–24:00. The offset changes routine transitions, never opening windows.
Current time and the actual game-clock rate determine selection and a conservative walking
ETA. A destination needs at least 15 game minutes for a visit after expected arrival;
closure during travel triggers another selection. Windows crossing midnight work normally.

`TASK_FOLLOW_NAV_MESH_TO_COORD` follows `act_hunting_2.c:10128` for walking speed, flags
and unconstrained heading. Travel has a five-minute gameplay deadline, a 20-second
no-progress threshold, a four-second recovery interval and at most two retries. Failed
destinations cool down for 60 seconds. Navigation failure uses the selected all-day public
fallback, otherwise an explicit waiting state. Arrival starts fixed-centre local wandering.
Tasks are issued on transitions or bounded recovery, never once per frame.

Per-frame travel never starts a global scene load or waits. It requests missing collision
and navmesh at a bounded rate and suspends tasks while the target's local navigation is
unloaded. Destination validation is at most once per second; selection retries at most
once per five seconds. Settled fallback visits reconsider the preferred destination after
60 seconds. This recheck does not reset a travelling fallback's stuck/deadline accounting.
Portrait deployment can reload its distant destination once if capture outlasted residency.

The combat policy runs first. Combat against anyone, an active combat task, pending
engagement, last-known-position search, ragdoll, getting up, lasso, being hogtied, hogtied
and riding in a vehicle suppress routine tasks. After those priorities end, the controller
selects for the current clock instead of returning to the original spawn. Time jumps and
phase changes also reselect. The main loop already suspends menus/fades, freezes gameplay
deadlines and clears contracts on player death/change. Contract cleanup resets all plan,
destination, retry and cooldown state via its existing hook. Active targets never teleport.

A stale native player-combat flag alone does not override the combat policy's completed
search. Pending/active combat tasks, new engagement and combat against other actors still
block routines. A changed accumulated pause duration forces one fresh selection even when
the outer loop skipped all routine updates during the pause/fade.

A target can enter an interior through normal AI, including during combat. The outdoor
test applies to authored spawn/arrival points, not to combat position. Routine venues are
still exterior visits; no doors, interior globals or mission actors are changed.

## Stage 3: habit clues on the existing card

The existing back panel retains its portrait, dimensions and reward. Its text now lists
occupation, town and four selected destinations under **USUAL HAUNTS**. Cached strings
come from the same immutable route IDs the controller visits. **Visits may vary** signals
the real availability/fallback policy; these are investigative habits, not live tracking.
Exterior names such as Store frontage and Slum saloon street do not claim a transaction,
game or show. The longest line is 32 characters; six separately placed lines avoid the
old unwrapped paragraphs. Pixel readability across resolutions remains an in-game check.

Only the information draw changes. The portrait capture, download handles, 32-slot policy,
card material retries, prop/item states, handoff, flip/zoom/put-away, final draw order,
corpse photography and payment functions were independently compared against `733eb83`;
all 18 checked protected functions and the complete Card constants block were identical.

## Stages 4 and 5: ambient activities; transit deferred

Arrival starts `TASK_WANDER_IN_AREA` immediately. The former forced 40–75-second
smoking/drinking stops have been removed following in-game feedback about stops in the
road. The game may choose ambient behavior while wandering; the mod does not choose a
smoking point, impose a duration, or start a scenario on arrival. Natural pauses can
still occur and need observation in game.

While Wandering, an observed `IS_PED_USING_ANY_SCENARIO` counts as a healthy task, so the
task-loss watchdog does not repeatedly cancel an ambient pause. Travel still requires
its navigation task and retains bounded recovery. Residency checks replace destination
clearance probes during an observed ambient scenario and for five seconds afterward,
so the target's own props do not invalidate the stop. Phase changes, visiting windows,
streaming and encounter priorities retain precedence. Debug text observes actual
smoking, drinking or another scenario; it does not infer activity from the destination.

`beat_town_robbery.c:3097` uses area wandering inside Keane's saloon; the current native
arguments also follow `beat_public_hanging.c:16270`. These examples support normal
wandering and interior access, not guaranteed shop interaction or minigame enrollment.
Any future explicit venue activity still needs verified access, hours/event state,
entry confirmation, ownership and interruption handling, plus truthful card wording.

Genuine poker, blackjack, five-finger fillet and theatre participation remain disabled.
Those scripts maintain private participants, seats and/or show state; no safe registration
and withdrawal protocol for our owned target was established. Sweeping and other work
animations, interior commerce and world-state construction sites are also deferred.

Ambient tram travel remains disabled: the verified passenger examples use mission-owned
carriages and warp recovery. A nonwarp approach/board/ride/disembark path with safe combat,
restraint, corpse access and borrowed-vehicle cleanup has not been established. No tram
state machine is shipped as a placeholder; targets continue walking. Detailed evidence
and the requirements for enabling these features are in
[routine-activities-and-transit.md](routine-activities-and-transit.md).
