# Train and Saint Denis tram contracts: design research

Research only, 2026-09-06. No train or tram gameplay is implemented by this change. The examples below were checked against the local `RDR3-Decompiled-Scripts-master/1491.50` sources under `C:/Users/caleb/Desktop/RDR2 Coding` and this repository's bundled `natives.h`. Decompiled parameter names and SDK comments are evidence, not a guarantee that every flag has been reverse engineered correctly. Unknown arguments remain unknown below.

## What the game actually does

Saint Denis's trolley uses the **train API**. In `rcm_for_my_art4.c`, `func_296` streams all models for config `-1083616881`, creates the trolley at the nearest track position with `_CREATE_MISSION_TRAIN(config, position, true, true, true, true)`, returns to the script loop, waits for `_HAS_TRAIN_LOADED`, and initializes it once behind `bLocal_108`. It also switches junction 0 of `NB_TROLLEY_TRACK_CONFIG`. The signed config is `0xBF69518F`, independently labelled `trolley_config` in the local femga catalog. No separate `CREATE_TRAM`, tram-speed, or tram-cleanup native was found in the bundled SDK. [Trolley initialization, lines 8335–8365][trolley-init]; [config catalog][configs].

`func_336` later disables station stops, requests departure, and sets trolley speed and cruise speed to `7.0f` once, guarded by a mission bit. Entry into a later volume changes cruise speed to `3.0f` once. The same helper issues `SET_DISABLE_RANDOM_TRAINS_THIS_FRAME(true)` on each call; that per-frame suppression is distinct from the one-time speed changes. `func_296` also initializes a separate conventional train, so its other config and 12.0f speed must not be mistaken for the trolley settings. [Trolley movement, lines 10383–10436][trolley-drive].

Two train ownership models appear in the sources:

- **Owned mission train:** `av_hobo_train_hop.c:2650–2665` creates one handle, sets both speeds to zero, disables its engine/whistle for staging, and waits for the consist to load. `beat_train_holdup.c:1723–1743` first calls `_0xF05DFAF1ADFEF2CD(config, position, direction, true)` and aborts if this returns false. Its next state waits for `_HAS_TRAIN_LOADED` before retrieving/initializing carriages. [Hobo creation][hobo-create]; [holdup preflight][holdup-create]; [holdup loaded state][holdup-loaded].
- **Ambient manager train:** `medium_update.c:24066–24106` uses `_0x331CBD247FC5DAA8` to obtain a **track index**, stores it in `Global_1425371`, checks `_DOES_TRAIN_EXIST_ON_TRACK`, then configures it through track-index natives. `train_annesburg.c:83–100` separately retrieves the streamed vehicle through `_GET_TRAIN_VEHICLE_FROM_TRACK_INDEX`. A track index and a `Vehicle` handle are not interchangeable. These globals are build-specific Rockstar state, not a stable mod API. [Ambient creation][ambient-create]; [Annesburg acquisition][annesburg].

For an initial prototype, use one owned mission consist in a verified staging location. Borrowing a live ambient train avoids creating an entire consist, but requires handling another script's scheduling, despawning and ownership. Do not seize/delete an ambient train merely because a target is aboard it.

## Verified SDK surface

All declarations in this table exist in `rdr2 scripting environment/inc/natives.h`. `Vector3` overloads are shown where available; `BOOL` is the actual SDK parameter type. Native hashes are included for the ambiguous or easily confused calls.

| Purpose | Available declaration / location | Meaning established here |
| --- | --- | --- |
| Stream a config | `int _GET_NUM_CARS_FROM_TRAIN_CONFIG(Hash)`; `Hash _GET_TRAIN_MODEL_FROM_TRAIN_CONFIG_BY_CAR_INDEX(Hash, int)` — lines 11459–11461 | Enumerate the config's car models, request each, wait for all. A config hash is not a single vehicle-model hash. |
| Preflight | `BOOL _0xF05DFAF1ADFEF2CD(Hash, Vector3, BOOL direction, BOOL p5)` — lines 11402–11403 | Used before creation by holdup and the ambient manager. Exact checks and the last flag's meaning are unverified; do not rename it to a definitive pool/track-clearance test. |
| Create | `Vehicle _CREATE_MISSION_TRAIN(Hash configHash, Vector3, BOOL direction, BOOL passengers, BOOL p6, BOOL conductor)` — lines 11900–11905, hash `0xC239DBD9A57D2A71` | `p6` remains undocumented. SDK comment says conductor plus a speed assignment enables train AI. Trolley example enables passengers and conductor; holdup disables both. |
| Wait for consist | `BOOL _HAS_TRAIN_LOADED(Vehicle)` — line 11900 | Creation success alone is not readiness of every carriage/scenario. |
| Position | `Vector3 _GET_NEAREST_TRAIN_TRACK_POSITION(Vector3)` — line 11422; `void SET_MISSION_TRAIN_COORDS(Vehicle, Vector3)` — line 11508 | Snap staging positions to verified track. Nearest track alone cannot select the intended route at a junction. |
| Drive | `void SET_TRAIN_SPEED(Vehicle, float)`; `void SET_TRAIN_CRUISE_SPEED(Vehicle, float)`; `void _SET_TRAIN_MAX_SPEED(Vehicle, float)` — lines 11451–11454 | Scripts use speed/cruise changes on transitions. The SDK documents a 30.0 maximum for max-speed; this is not a recommended trolley operating speed. |
| Station control | `_SET_TRAIN_STOPS_FOR_STATIONS(Vehicle, BOOL)`; `IS_TRAIN_WAITING_AT_STATION(Vehicle)`; `_SET_TRAIN_HALT(Vehicle)`; `_SET_TRAIN_LEAVE_STATION(Vehicle)` — lines 11464–11470 | Enabled station behavior depends on the track/config. Disabling station stops requires a separate mission stopping policy. |
| Carriages | `Vehicle GET_TRAIN_CARRIAGE(Vehicle, int)` — line 11503 | Validate each returned vehicle; index/count depends on the chosen config. Holdup enumerates against config count at lines 1798–1818. |
| Local stop / route | `_ADD_TRAIN_TEMPORARY_STOP(Vehicle, int trackIndex, Vector3)` — line 11853; `_SET_TRAIN_TRACK_JUNCTION_SWITCH(Hash trainTrack, int junctionIndex, BOOL)` — line 11583 | Hobo event uses a temporary stop at lines 2673–2684. Junction changes affect the route, not just an entity; restore only known changes. |
| Ambient lookup | `_GET_TRAIN_TRACK_FROM_TRAIN_VEHICLE(Vehicle)`; `_GET_TRAIN_VEHICLE_FROM_TRACK_INDEX(int)`; `_DOES_TRAIN_EXIST_ON_TRACK(int)` — lines 11380–11394 | Manager track existence and live vehicle existence are different checks. |
| Passenger task | `TASK_RIDE_TRAIN(Ped, Vehicle, int scenarioPoint, Hash scenarioHash)` — line 10472; `TASK_USE_NEAREST_TRAIN_SCENARIO_TO_COORD_WARP(Ped, Vector3, float distance)` — line 10465 | There must be a usable carriage scenario. A generic car seat index is not the `scenarioPoint` argument. |
| Scenario validation | `DOES_SCENARIO_POINT_EXIST(int)` — line 10385; `FIND_SCENARIO_OF_TYPE_HASH(Vector3, Hash, float, Any p5, BOOL)` — line 10481; `TASK_USE_SCENARIO_POINT` — line 10441 | Check points before issuing tasks; the exact passenger hash below includes Rockstar's `VEHICHLE` spelling. |
| Fixed attachment | `ATTACH_ENTITY_TO_ENTITY` — lines 1959–1960; `DETACH_ENTITY(Entity, BOOL, BOOL)` — line 1972 | The full ped attachment has 17 expanded arguments; use the actual SDK declaration, not a GTA signature. Unknown trailing flags remain unknown. |
| Release/delete | `DELETE_MISSION_TRAIN(Vehicle*)`; `SET_MISSION_TRAIN_AS_NO_LONGER_NEEDED(Vehicle*, int flags)` — lines 11504–11506 | SDK documents `0 = DEFAULT`, `1 = KEEP_OLD_SPEED`. Release hands control away; it does not confirm destruction. |
| Population / collision | `PED::_GET_NUM_FREE_SLOTS_IN_PED_POOL()` — line 6249; `STREAMING::REQUEST_COLLISION_AT_COORD(Vector3)` — line 9709; `ENTITY::HAS_COLLISION_LOADED_AROUND_ENTITY(Entity)` — line 2031 | Passenger/conductor peds add pool pressure. Model readiness does not establish collision or scenario readiness. |

SDK evidence: [train/track declarations][sdk-tracks], [speed/config declarations][sdk-speed], [release declarations][sdk-release], [creation declaration and comments][sdk-create], [task declarations][sdk-tasks], [attachment signature][sdk-attach]. `NETWORK::CAN_REGISTER_MISSION_VEHICLES(int)` exists at line 5297 and the config-count comment references it; its network registration budget is **not established as a reliable single-player train-pool check**. No verified numeric single-player limit on simultaneously spawned consists was established by this research. The eight entries traversed in `train_summon.c:41–46` are manager records, not proof of an eight-train engine pool limit.

## Putting the target aboard

**Passenger is the strongest starting point.** `rcm_beau_and_penelope21.c:10329–10338` checks an existing scenario point and calls `TASK_RIDE_TRAIN(ped, carriage, point, 0)`. Its update checks whether the ride task is already running before recovering it; after 11/12 seconds of unsuccessful boarding, it changes state. `func_382` similarly issues `TASK_USE_NEAREST_TRAIN_SCENARIO_TO_COORD_WARP` near a carriage-relative offset only when the relevant task/scenario is absent. [Boarding][passenger-board]; [boarding recovery][passenger-recovery]; [carriage-relative passenger tasks][passenger-warp].

`trainrobbery_ambient.c:6573–6592` searches for `PROP_VEHICHLE_SEAT_PASSENGER_TRAIN_TG` within 4 metres and validates the result before using it. Its passenger setup sets config flag **162 (`PCF_AlwaysLeaveTrainUponArrival`) to false** at line 6605. This is useful evidence for a rider who must remain aboard; other flags in that helper implement robbery-specific interactions/cowering and should not be copied wholesale onto an armed bounty. [Passenger scenario and flags][passenger-scenario]. A prototype should prefer a reserved, verified carriage point and confirm `PED::IS_PED_ON_SPECIFIC_VEHICLE(target, carriage)` (available at SDK line 6349; used in `beat_train_holdup.c:15898–15913`). A nearby scenario search alone does not prove the intended seat was chosen.

**Worker on a carriage is plausible but needs placement validation.** `av_hobo_train_hop.c:1938–1945` computes a carriage-relative position, attaches its ped to the carriage, starts `WORLD_HUMAN_SEAT_LEDGE_NEW`, forces an AI/animation update and keeps the task. That proves a scripted ped can ride with a carriage through an attachment; it does not prove that a standing/shovelling worker animation, combat, corpse physics or lasso behavior works with the same flags. [Attached rider][hobo-attach].

A worker variant could therefore use a tested work scenario on a stationary wagon or a tested carriage-relative scenario while moving. Keep the driver separate: changing a conductor into a bounty changes the very ped responsible for driving. Station-worker models such as `U_M_O_RIGTRAINSTATIONWORKER_01` are present in the Beau/Penelope script (line 524), but a model name is not evidence of a working moving-train shovel/driver task. No exact moving worker scenario was verified here. Before combat, lasso, ragdoll, death or abandonment, explicitly leave the scenario/detach if this variant uses a rigid attachment; test whether that transition is physically safe. Do not reattach a struggling or dead target every frame.

## Startup, schedules and teardown

Proposed future runtime states: `RequestModels`, `CreateOnce`, `WaitForConsist`, `PlaceTarget`, `Ready`, `Running`, `Retiring`, `Finished`. These are design names, not code added to the mod.

1. Choose a validated config, track location/direction and boarding point. Request every config model, with an interruption-aware timeout across frames. Rockstar's `rcm_for_my_art4.c:16732–16752` checks every model rather than assuming one locomotive model is sufficient.
2. Perform the observed preflight and create once. Retain any returned handle while waiting; do not allocate another consist every frame because a carriage is not ready. Wait for the train, intended carriage, collision and usable scenario. Log config, handle, stage and timeout failure.
3. Place the existing bounty target, validate boarding, then start movement once. Keep a short explicit stage timeout and bounded task recovery; retain the existing no-interruption behavior for lasso/ragdoll. A stopped staging train avoids trying to board at speed.
4. Release model requests once initialization no longer needs them, as `func_612` does for every config model at `rcm_for_my_art4.c:18310–18321`. This releases streaming requests, not the train entity. Keep owned entities tracked for the hunt.
5. On replacement, cancellation, failure, payout cleanup or script shutdown, stop mission-specific monitoring, retire only owned peds/scenario points/props, and either delete the owned train or intentionally release it with an explicit policy. Do not delete a train occupied by the player; defer retirement until safe. Preserve the corpse until evidence collection is finished. In a release path, verify that it can actually depart rather than leaving a released, stopped obstacle.

The trolley source deletes its two mission trains separately at `rcm_for_my_art4.c:13381–13387` and restores altered scenario groups at 13393–13395. The holdup event deliberately chooses between deletion and `SET_MISSION_TRAIN_AS_NO_LONGER_NEEDED(&train, 1)` at lines 872–881. The hobo event deletes only its own spawned train; its borrowed-train path restores track behavior instead. [Trolley cleanup][trolley-cleanup]; [holdup release policy][holdup-cleanup]; [owned/borrowed cleanup split][hobo-cleanup]. For this mod, retain the original handles/models until later-frame disappearance is confirmed, mirroring the existing owned-ped cleanup discipline; a pointer zeroed by a delete native is not sufficient confirmation. Carriage/conductor/passenger cleanup behavior must be measured, not assumed from deletion of the locomotive handle.

Rockstar's ambient schedule is more than a speed loop. `medium_update.c:23523–23539` uses hour/state gates; lines 23569–23598 select a scheduled location using stored times and set manager flags after relocation. Lines 54304–54327 load schedule entries and pass them to an opaque track native. `train_summon.c:136–160` waits for a track/vehicle with a 15-second deadline. It also fades out and changes the game clock earlier in the interaction. `shop_train_station.c:4738–4747` requests/starts that script. [Schedule gates][schedule-gates]; [schedule selection][schedule-selection]; [schedule data][schedule-data]; [summon][summon]; [station script launch][station-launch]. Reuse the principle of explicit initialization and timed state transitions. Do not start `train_summon`, overwrite its globals, change game time or copy its world-clearing operations just to deliver a bounty at a station.

## Fit with ContractDef and existing gameplay

`ContractDef` already supplies `models`, static `spawn`/`searchRadius`, `behavior`, `onSpawned(const ContractDef&)` and `onCleanup()`. `TargetBehavior` separates one-time setup and per-frame update. [Data structures][contract-def]. In `StartContract`, the target and portrait are prepared first, `C.def`/`C.target` are assigned, behavior setup runs, and then `onSpawned` runs. `ClearContract` calls `onCleanup` before target cleanup and resets `C`. These hook names are stable navigation anchors; line numbers in `script.cpp` can move as the current audit is applied.

A future passenger/worker row can use dedicated rail behavior plus `onSpawned`/`onCleanup` to manage an owned `RailContractRuntime`: train handle, config, expected carriage identities, boarding point, spawned auxiliaries, state/deadlines and cleanup status. The runtime must survive clearing `C` while deletion is pending. `onCleanup` should request idempotent cleanup; maintenance must continue independently of the live-target behavior update because that update stops when the target dies or the contract is cleared.

There is an important limitation: **the existing `void onSpawned` cannot report asynchronous train startup failure**. A row and a hook alone cannot guarantee an actually boardable rail target before handing out a valid contract. A future implementation needs a separate rail readiness/commit stage, with rollback of provisional train/target assets on timeout. Keep that outside the working portrait producer and card/hand-off paths; don't move or rewrite `PhotographPed` to solve rail startup. Start with a stopped, locally streamed rail encounter to limit the new lifecycle surface.

`kHumanTarget` cannot be used unchanged while aboard: its wander/search behavior returns to fixed world coordinates, and its task recovery would conflict with boarding or a work scenario. A rail behavior should own the idle/boarding states, enter existing hostile behavior only after a safe exit from its scenario/attachment, and choose a valid ground or carriage recovery policy. The current radius search blip also uses the static contract spawn; a moving target needs an explicit last-seen station/route search policy before it is found. Keep discovered-target blips tied to the target entity.

Contract delivery can continue through the current station clerks, physical photo card, U key and payment flow. The station name/route can live in a row's text hints. Station-specific offer filtering, if desired later, is a separate feature: existing rows are globally randomized. Neither the protected card workflow nor the game's train-ticket/clock-changing script needs to change for this design.

## Risks to resolve before implementation

| Risk | Required prototype check / response |
| --- | --- |
| Consist and NPC capacity | Use one small consist, avoid optional ambient passengers initially, inspect free NPC slots and creation results. No invented train-pool capacity or network-budget assumption. Do not clear unrelated trains/NPCs to make room. `DELETE_ALL_TRAINS` exists but is unsuitable for per-contract cleanup. |
| Competing routes | Test both directions and nearby switches on the chosen track; nearest-track coordinates can select the wrong branch. Ambient trains can collide or contend with a mission train. Any temporary suppression/switch change needs scoped ownership and restoration. |
| Streaming and persistence | Revalidate owned train, carriages and target when traveling away/back and after fast travel. Mission ownership does not prove every carriage scenario remains available. Never recreate a lost target, change its model or recapture a replacement portrait silently. |
| Death and evidence | Shoot, hogtie/lasso, knock down and board the target while stopped and moving. Verify corpse position remains reachable, the existing 25-metre photo check still works, and train retirement cannot delete the evidence early. |
| Cleanup and replacement | Interrupt each initialization stage, replace with U, collect payout, die/reload and unload the mod. Confirm original train/carriage/ped handles retire, no duplicate consists appear, and pending retirement blocks another allocation when appropriate. |
| Task/flag uncertainty | Verify seats/work animations per config. Treat unnamed native parameters, attachment flags, opaque track calls and named SDK comments as candidates for validation; don't transfer mission-specific aggression/cowering flags to the bounty without evidence. |

[trolley-init]: <C:/Users/caleb/Desktop/RDR2 Coding/RDR3-Decompiled-Scripts-master/1491.50/rcm_for_my_art4.c:8335>
[trolley-drive]: <C:/Users/caleb/Desktop/RDR2 Coding/RDR3-Decompiled-Scripts-master/1491.50/rcm_for_my_art4.c:10383>
[trolley-cleanup]: <C:/Users/caleb/Desktop/RDR2 Coding/RDR3-Decompiled-Scripts-master/1491.50/rcm_for_my_art4.c:13381>
[configs]: <C:/Users/caleb/Desktop/RDR2 Coding/rdr3_discoveries-master/vehicles/trains/mission_trains.lua:165>
[hobo-create]: <C:/Users/caleb/Desktop/RDR2 Coding/RDR3-Decompiled-Scripts-master/1491.50/av_hobo_train_hop.c:2650>
[hobo-attach]: <C:/Users/caleb/Desktop/RDR2 Coding/RDR3-Decompiled-Scripts-master/1491.50/av_hobo_train_hop.c:1938>
[hobo-cleanup]: <C:/Users/caleb/Desktop/RDR2 Coding/RDR3-Decompiled-Scripts-master/1491.50/av_hobo_train_hop.c:1969>
[holdup-create]: <C:/Users/caleb/Desktop/RDR2 Coding/RDR3-Decompiled-Scripts-master/1491.50/beat_train_holdup.c:1723>
[holdup-loaded]: <C:/Users/caleb/Desktop/RDR2 Coding/RDR3-Decompiled-Scripts-master/1491.50/beat_train_holdup.c:334>
[holdup-cleanup]: <C:/Users/caleb/Desktop/RDR2 Coding/RDR3-Decompiled-Scripts-master/1491.50/beat_train_holdup.c:872>
[ambient-create]: <C:/Users/caleb/Desktop/RDR2 Coding/RDR3-Decompiled-Scripts-master/1491.50/medium_update.c:24066>
[annesburg]: <C:/Users/caleb/Desktop/RDR2 Coding/RDR3-Decompiled-Scripts-master/1491.50/train_annesburg.c:83>
[sdk-tracks]: <C:/Users/caleb/Desktop/Bounty Hunting Mod/rdr2 scripting environment/inc/natives.h:11373>
[sdk-speed]: <C:/Users/caleb/Desktop/Bounty Hunting Mod/rdr2 scripting environment/inc/natives.h:11451>
[sdk-release]: <C:/Users/caleb/Desktop/Bounty Hunting Mod/rdr2 scripting environment/inc/natives.h:11503>
[sdk-create]: <C:/Users/caleb/Desktop/Bounty Hunting Mod/rdr2 scripting environment/inc/natives.h:11900>
[sdk-tasks]: <C:/Users/caleb/Desktop/Bounty Hunting Mod/rdr2 scripting environment/inc/natives.h:10451>
[sdk-attach]: <C:/Users/caleb/Desktop/Bounty Hunting Mod/rdr2 scripting environment/inc/natives.h:1959>
[passenger-board]: <C:/Users/caleb/Desktop/RDR2 Coding/RDR3-Decompiled-Scripts-master/1491.50/rcm_beau_and_penelope21.c:10329>
[passenger-recovery]: <C:/Users/caleb/Desktop/RDR2 Coding/RDR3-Decompiled-Scripts-master/1491.50/rcm_beau_and_penelope21.c:12762>
[passenger-warp]: <C:/Users/caleb/Desktop/RDR2 Coding/RDR3-Decompiled-Scripts-master/1491.50/rcm_beau_and_penelope21.c:12919>
[passenger-scenario]: <C:/Users/caleb/Desktop/RDR2 Coding/RDR3-Decompiled-Scripts-master/1491.50/trainrobbery_ambient.c:6573>
[schedule-gates]: <C:/Users/caleb/Desktop/RDR2 Coding/RDR3-Decompiled-Scripts-master/1491.50/medium_update.c:23511>
[schedule-selection]: <C:/Users/caleb/Desktop/RDR2 Coding/RDR3-Decompiled-Scripts-master/1491.50/medium_update.c:23569>
[schedule-data]: <C:/Users/caleb/Desktop/RDR2 Coding/RDR3-Decompiled-Scripts-master/1491.50/medium_update.c:54304>
[summon]: <C:/Users/caleb/Desktop/RDR2 Coding/RDR3-Decompiled-Scripts-master/1491.50/train_summon.c:136>
[station-launch]: <C:/Users/caleb/Desktop/RDR2 Coding/RDR3-Decompiled-Scripts-master/1491.50/shop_train_station.c:4738>
[contract-def]: <C:/Users/caleb/Desktop/Bounty Hunting Mod/rdr2 scripting environment/samples/Pools/contract_data.h:94>
