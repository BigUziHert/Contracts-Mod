# Daily routine system — 2026-09-07

This supersedes the earlier four-phase timings and the activity-disabled descriptions in the historical routine reports. All generated human targets in all seven supported towns use this schedule without seed offsets:

| In-game time | Intended activity |
| --- | --- |
| 06:00–11:00 | Work at the assigned workplace |
| 11:00–12:00 | Lunch at the assigned saloon/meal area |
| 12:00–16:00 | Return to the same workplace |
| 16:00–19:00 | Errands and public wandering |
| 19:00–03:00 | Saloon, social or campfire activity |
| 03:00–06:00 | Rest or sleep at the assigned nighttime area |

The seed varies destination and activity preferences while the contract retains its plan. Lunch has its own route slot; it never replaces the work slot. Midnight belongs to evening, and the clock is reconsidered after phase changes, substantial forward/backward skips, loading and interruption. The mod does not transfer its spawned actor to a named persistent-character schedule or population cleanup.

## Activities and recovery

Travel still uses walking navmesh tasks and accepts arrival within the authored area's 45 m radius. Discovery then enumerates existing world points nearby and filters known work, meal, social and rest types for the current phase. A point's native entry task supplies its actual approach and normal entry animation. No point, prop, chair or bed is created; no activity teleports the target. Work scenarios meet the requirement without borrowing an unfiltered chain that could contain unrelated activities.

The activity controller has separate ambient, entering, active, exiting and suspended states. A submitted scenario task is entry, not successful work/eating/sleeping. Initial confirmation requires the target using the exact selected point in its scenario base state. After confirmation, the broader native activity predicate preserves normal scenario graph transitions away from that base. An active point is retained without repeated task assignment. Compatibility/availability is rechecked; occupancy checks recognize the target itself. Entry allows up to two minutes for the walk and animation; a dropped native entry task fails after a 1.5-second grace. Failed points are excluded for two minutes; no available point retains wandering and retries after 20 seconds. Normal exits receive eight seconds before a gentle clear retry, followed by a further eight seconds before bounded ordinary wandering recovery. Higher-priority events submit no routine task or exit hint.

The ordinary route controller retains its navigation progress checks and destination cooldowns. All-destination failure keeps ambient wandering around the last accepted area, never an indefinite stand-still task or a teleport. Successful native ambient scenarios are preserved within their window. A schedule transition can request an exit even when the next venue is unavailable. Availability is not inferred from an exterior frontage or a successful task submission.

## Source and engine limits

See [the Rockstar source evidence](daily-routine-sources.md) and [destination provenance](routine-location-sources.md). The source distinguishes Appleseed's time-selected scenario groups and Rockstar's scenario/wander composition from private persistent-character scheduling. The mod does not enable/disable global scenario groups, reserve or evict population actors, launch minigames or alter mission globals.

Strawberry and Annesburg have no verified usable local saloon in the reviewed sources. Their cards explicitly name public lunch areas; only a compatible observed eating/drinking point earns that observed label. Nighttime anchors are source-supported neighborhoods/public areas, not claimed beds; rest/sleep uses only discovered compatible world points, otherwise ambient fallback remains visible. Interior readiness and nearby navigation checks cannot prove an unlocked path through every door. Entry deadlines and cooldowns handle rejected paths, but animation, props, stairs, doors and collision still require in-game checks. Simulated-native tests do **not** prove navigation or animation quality.

## Automated verification and build

`tests/run-tests.ps1` passed in full. This includes the existing combat, search, handoff, payment, capture, identity, cleanup, startup and display regressions. Relevant new/updated results: schedule 51,848 checks; plan/data 185,420; activity policy 399; native activity bridge 40; integrated routine runtime 2,540; card 107,693; debug bridge 6,139. Additional tests cover confirmed non-base scenario transitions and a healthy lunch retained through an 11:50 pause/revalidation.

Release x64 MSBuild `/t:Rebuild` passed with zero warnings and zero errors. `OutDir` was explicitly set to the workspace `dist` directory and `IntDir` to workspace `tmp/Release-x64`. No build was installed into the game directory. `dist/TestScript.asi` is 340,992 bytes, SHA-256 `B6DBD87BD71F2147036A3C3F6FF56DDF4186591EB2C6FC739E965CC05AE58F01`. Full local logs are `tmp/daily-routine-regressions.txt` and `tmp/daily-routine-release-build.txt`.

## In-game transition checklist

1. In each supported town, observe 05:59→06:00 (rest to work), 10:59→11:00 (lunch), 11:59→12:00 (same workplace), 15:59→16:00 (errands), 18:59→19:00 (evening), 23:59→00:00 (stay evening), and 02:59→03:00 (rest). Allow real walking time after each boundary.
2. Read Intended, Doing and Next in F8 debug. Watch scenario entry and exit; eating/sleeping must appear only during a confirmed matching activity. Verify no walk, animation or weapon-draw position snaps.
3. Occupy a chosen point or make an interior unavailable. Expect another compatible point or 45 m wandering with retries; the failed point must not be selected every frame. Verify a healthy activity is left alone for the remainder of its window.
4. Skip time forward/backward, pause/load, and leave/revisit an area. Verify the current-time activity resumes, the target retains its identity, and the work destination stays the same after lunch.
5. Provoke during travel and every activity; escape and finish the search. Lasso, hogtie, release and knock down the target. Routine entry/exit/travel must yield, then recover for the current time.
6. Kill/photograph targets during activities and complete handoff/payment; also cancel/replace a contract. Verify the owned target and its props clean up normally, unrelated actors remain, and payment occurs once.
