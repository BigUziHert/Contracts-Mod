# Native ambient baseline restoration — 2026-09-09

The owner identified `61f3ad873a30bed37fbeb24c9d4a1cb8698c5a32` as an in-game working ambient baseline and requested that behavior together with the current physical contract card, bug fixes and loss-of-sight/search system. The replacement build is `native-baseline-card-v1`.

## Preservation

The pre-restoration HEAD, dirty-file status, binary tracked diff and complete tracked/untracked source snapshot are under `tmp/recovery-backups/before-native-baseline-20260909-170621/`. `working-source.zip` contains the prior source; `tracked.patch` and `status.txt` also record modifications and deletions. Installed game files were not overwritten. Work stays on `dev` without a reset, forced checkout or history rewrite.

## Ambient ownership

The post-card refactor retained the original 18 fixed contract definitions and native wandering path. The restoration reconnects them and removes daily-routine interception from setup and every-frame target updates. The later card/portrait implementation stays intact.

`StartWander` uses the baseline's `CLEAR_PED_TASKS(ped, true, true)`, water path preferences, and `TASK_WANDER_IN_AREA` at the original contract anchor/radius with the original `0, 0, 1` arguments. This runs at setup and search completion. Passive updates leave native scenarios alone. Flag 211 stays enabled; the scheduler's disabling override and generated profession/outfit restrictions are removed. Random outfit variation is restored.

No routine runtime, activity, rest, profile, card-plan or debug header is included by the active script/project. Historical files remain outside the active build and default test runner. The card uses the original contract description/location hint and the map uses its fixed search circle. Scripted phase destinations, forced scenarios, strolls, pauses and activity recovery no longer run.

## Retained improvements

The current `TargetAI::Step` policy and combat bridge preserve sight ranges, contact-loss grace, last-known-position search, remembered re-engagement, restraint/ragdoll/get-up guards, combat retry timing and animated scenario exit before combat. Search completion directly hands control to native wandering. Deliberate later combat/loadout fixes remain; this is not a byte-for-byte reversion of the whole old ped setup.

The physical card, portrait slot rotation, opaque download-handle handling, texture binding schedule, weapon put-away gate, clerk/cash handoffs, remote-input queue, owned-ped cleanup, pause deadlines, corpse proof, payout and scoped crime/trail handling are retained. The photo subject stays hidden until moved to the original contract position and placed on the ground. Player/subject liveness is checked before reveal. There is no routine destination/phase admission or relocation between scheduled stops.

## Validation scope

Production AI bridge tests compile without routine API stubs. They exercise long calm runs with varied scenarios/statuses, search completion with and without a lingering native-combat flag, one native wander handoff, renewed aggression, restraint and seated combat exits. Startup tests cover static contract selection, ownership/liveness and portrait failures. Static search-area tests replace route-following assertions.

The modern card and the old ambient baseline were each previously confirmed by the owner in-game. The owner subsequently tested this combined ASI and reported that the target immediately started an ambient task and that the regression was fixed. This confirms the restored ambient entry in-game; automated checks cover the additional ownership and lifecycle cases.

`tests/run-tests.ps1` completed successfully across all 16 active suites. The native AI bridge passed 10 scenario groups including 40,000 passive updates without additional task/exit calls; card texture, card startup and card inspection passed 55,804, 29,125 and 13,513 checks respectively. Spawn, portrait preparation, static search-area and contract-start checks also passed. The 12 retired daily-routine suites are outside this build's default test run.

Release x64 compiled successfully without warnings. `dist/native-baseline-card/TestScript.asi` is 257,024 bytes, SHA-256 `2F588EB26A0647C93B3D484671918211D1CF7100D3CE5058D4C625F8BA625EA8`. The binary contains the new build marker and none of the old activity-search/local-task/debug labels checked. `dist/BountyContracts-native-baseline-card.zip` packages that ASI, installation notes and the unchanged card-title LML resource. Build/test logs are in `tmp/native-baseline-card-build.log` and `tmp/native-baseline-card-tests.log`.
