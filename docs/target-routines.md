# Town target routines

## Stage 1: sourced locations and initial deployment

New contracts choose an occupation and a four-destination plan in one of five towns.
`routine_locations.h` contains 27 public exterior candidate areas, including stable,
livestock, market, dock, shop-front and newspaper/campfire areas. Coordinate provenance
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
sphere loading; `marston1.c` and `beau_penelope2.c:38953,38978` for nav-region requests.
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
failure. Location-unavailable startup diagnostics use failure code 10.

The generated definition lives in stable runtime storage; the existing cleanup hook resets
its plan and controller. Portrait generation, download ownership, slot rotation and all
clerk/payment paths retain their existing implementation.

## Separate areas

- Town search: a fixed 180–260 m investigation circle covering the curated sites and their
  wandering areas. It does not follow the target or disclose its selected destination.
- Candidate radius: 7–12 m around a sourced coordinate, used only for validation.
- Wander radius: 18–26 m around a fixed validated destination point.
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
