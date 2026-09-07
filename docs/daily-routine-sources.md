# Daily routine activity source notes

Research used the owner's local sources under `C:\Users\caleb\Desktop\RDR2 Coding`. Decompiled citations below refer to `RDR3-Decompiled-Scripts-master\1491.50`. Scenario type definitions refer to `rdr3_discoveries-master\animations\scenarios\scenario_types_with_conditional_anims.lua`. Existing town/area coordinates and their limitations remain documented in `routine-location-sources.md` and in `RoutineData::kLocations`.

## What Rockstar's examples demonstrate

- `event_area_appleseed_stg5.c:1710-1780` assigns separate work and night scenario groups for Appleseed stages. `func_269` at lines 9430-9436 chooses the work group when `func_164(64)` is true, otherwise the night group. `func_164`, lines 6587-6745, extracts the hour and checks windows; flag 64 means 06:00–20:00 (lines 6655-6662). The packed clock comes from `Global_1899515` and hour bits at lines 15300-15308. Lines 2965-2988 issue `TASK_USE_RANDOM_SCENARIO_IN_GROUP` using that time-selected group. This is direct evidence for choosing activities according to the current world time, including a separate night branch. The mod uses the owner's exact windows, not Appleseed's hours.
- `event_area_appleseed_stg1.c:3502-3543` is a different path: named `EventAreas/Appleseed/LCMP_Workers`, shelter/stalled variants and foreman schedules are set on **validated persistent-character handles**. It only issues `TASK_PERSISTENT_CHARACTER` when the matching existing actor exists and is not a mission entity. This is not evidence that arbitrary custom spawned bounty actors can use those named schedules. The mod never calls these schedule APIs, modifies global scenario groups, or releases its target to ambient population.
- `hideout_hangingdogranch_es.c:4831-4866` builds a sequence with either nearest scenario-chain or nearest scenario-point use, optionally followed by `TASK_WANDER_IN_VOLUME` or `TASK_WANDER_IN_AREA`. Separate warp cases are explicitly present in that source. The mod adopts activity plus ambient fallback composition and uses the normal point task only. A blanket nearest-chain task cannot guarantee that its chain matches lunch, sleep, or a particular job, so selection is restricted to known existing scenario types. Work uses suitable standalone work scenarios; it does not start unverified pickup-only chain fragments.
- `beat_laramie_gang_rustling.c:2453-2461` demonstrates a sequence of navigation followed by a non-warp scenario-chain task. `beat_drunk_dueler.c:2496` uses `TASK_USE_SCENARIO_POINT(0, point, 0, -1, true, false, 0, false, -1.0f, false)`. The mod uses this normal-entry, non-warp argument pattern with its owned ped. No fixed conditional animation is forced.

## Point discovery, compatibility and observation

`native3.c:80910-80929`, `homeinvasion.c:91608-91629`, and `loanshark_sellhorse1.c:1264-1278` enumerate existing scenario points into script arrays, then check/use individual entries. The bridge passes the RAGE array capacity header followed by eight-byte `Any` slots, with bounded capacity 64. It does not create, relocate, enable, reserve globally, or delete world points.

`interactive_campfire.c:2823` uses `_CAN_PED_USE_SCENARIO_POINT(ped, point, 0, 0, 1)`. The bridge uses the same flags and separately checks point existence, active state, type enablement and occupancy. A failed point enters a cooldown; another eligible candidate may be selected immediately. Existing valid activity is not rejected merely because its own actor occupies its point. `interactive_campfire.c:2581` distinguishes `_PED_IS_IN_SCENARIO_BASE` from broader scenario task activity. The bridge requires the exact observed point and type, scenario base state, and no scenario exit before initially classifying an intended activity as running. After entry is confirmed, `IS_PED_ACTIVE_IN_SCENARIO(ped, 1)` also preserves healthy conditional/graph transitions away from the base. `ambient_fishing_scenario.c:103-141` uses that active predicate for ongoing liveness while using base for initialization; `av_cat_catch_bird.c:1938` explicitly recognizes an active scenario outside its base. A transition is not grounds to repeatedly restart a healthy task.

The SDK declaration for `IS_PED_ACTIVE_IN_SCENARIO` misleadingly calls its second parameter `scenario`. Scripts such as `abigail2_1_intro.c:9736` pass 0 or 1 as a mode flag, not a point handle. The bridge uses mode 1 for confirmed-activity liveness and separately uses `PED::IS_PED_USING_THIS_SCENARIO` for point identity.

The relevant available SDK hashes are:

| Native | Hash |
| --- | --- |
| `GET_SCENARIO_POINTS_IN_AREA` | `0x345EC3B7EBDE1CB5` |
| `DOES_SCENARIO_POINT_EXIST` | `0x841475AC96E794D1` |
| `_GET_SCENARIO_POINT_TYPE` | `0xA92450B5AE687AAF` |
| `_IS_SCENARIO_POINT_ACTIVE` | `0x0CC36D4156006509` |
| `_IS_SCENARIO_IN_USE` | `0x1ACBC313966C21F3` |
| `_GET_PED_USING_SCENARIO_POINT` | `0x5BA659955369B0E2` |
| `_CAN_PED_USE_SCENARIO_POINT` | `0xAB643407D0B26F07` |
| `TASK_USE_SCENARIO_POINT` | `0xCCDAE6324B6A821C` |
| `_PED_IS_IN_SCENARIO_BASE` | `0x02EBBB3989B7E695` |
| `IS_PED_USING_THIS_SCENARIO` | `0x9C54041BB66BCF9E` |
| `IS_PED_EXITING_SCENARIO` | `0x0C3CB2E600C8977D` |

## Activities and honest labels

All 50 type names in `routine_activity_bridge.h` were checked against actual type definitions in the local scenario catalog; they are not guessed animation names. Useful reference sections are:

| Activity | Catalog evidence |
| --- | --- |
| Work: brooms, hammering, raking, weeding, feeding, clipboard, sawing | Lines 1970-1979, 2027-2050, 2189-2213, 2234-2253, 2469-2471 |
| Lunch: actual eating or drinking scenarios | Table knife/fork eating at 7410-7425; stew at 266-276, 395-400, 409-419; coffee/drinking at 1156-1180; sitting drink at 1464-1468; table drinking at 1771-1794 |
| Public errands and evening: smoking, newspaper, seats, campfire | Smoking at 1605-1613 and 1624-1626; newspaper at 795-797 and 1711-1713; campfire at 95-194, 313-324 and 485-492; generic seats at 1438-1455, 1542-1551 and 1633-1658 |
| Night: existing ground sleep points and seated/bed rest | Ground sleep at 1527-1539; tired bench at 1063-1066; beds at 1566-1602 |

Bar-customer types at lines 7466-7496 include `NO_DRINK` conditional variants. These are labeled **social**, excluded from lunch success, and never presented as proof that a target drank. Bed types include awake resting variants, so they are labeled **rest**, while verified ground-sleep types are classified as sleep activity. Even a confirmed eating scenario describes the activity running; it does not prove a completed transaction, a meal purchase, a particular clip frame, or a finished meal. Entering, exiting and fallback remain separately observable states.

## Navigation, collision and interiors

Travel continues to use the established ground-validated exterior area. Activities are discovered within 35 metres of that area's source anchor, independently of the retained **45-metre wandering radius around the last accepted area**. Selection rejects another vertical level, using the source area's height tolerance plus one metre. Destinations remain assigned habits; discovery does not relocate or redefine a workplace.

For each authored activity point, the bridge requires loaded collision/navigation, finite coordinates, a ready collision interior when one exists, and agreement with any coordinate interior. If collision is not marked outside but no interior is available, the point is rejected. `saloon1.c:41294` and `short_update.c:12945` independently demonstrate waiting on `IS_INTERIOR_READY`; these checks do not open locked doors or make unloaded business interiors usable.

Before new entry, `GET_SAFE_COORD_FOR_PED` must find a nearby approach within three metres horizontally and 1.75 metres vertically, in the same interior, with loaded collision/navigation, no vehicle/ped obstruction and no water at the approach surface. The approach is a validation probe, never a replacement for the authored point. A standing-body ray through the actual seat/bed would reject the furniture the scenario is meant to use, so the bridge does not apply the outdoor spawn clearance test to that animation volume. Occupancy, compatibility, authored placement and entry observation instead gate use. Readiness and a nearby nav point cannot prove door/path connectivity; bounded entry failure and destination retries provide runtime recovery.

No new bed coordinates, forced interior sets, door overrides, teleportation, target replacement, population release or immediate clear during normal scenario transitions are part of activity discovery. Mission combat, restraint, capture and cleanup remain higher-priority owners of the ped's task. Returning control re-evaluates the current clock.

## Verification scope

`routine_activity_bridge_tests.cpp` exercises the real bridge with simulated natives: phase-compatible type selection, rejecting occupied/unusable points, cooldown and alternate selection, re-entry after availability changes, preservation of owned healthy scenarios, entry/base/exit distinctions, interior/navigation/collision/water/height checks and stable seeded preferences across enumeration order and handle changes. These tests verify the C++ policy and native call contract expected by the bridge. They **do not prove in-game native array behavior, animation quality, path connectivity, interior door access, streaming residency, or the availability of any particular scenario in every story/save state**. Those require the owner's in-game tests. An unavailable suitable scenario must remain visible as fallback wandering, never as successful sleep or lunch.
