# Town coverage and native ambient wandering — 6 September 2026

This build adds 36 outdoor candidate areas, bringing the catalog to 63 across seven
towns. Every enabled site is reachable by the occupations that actual contract
generation selects, and every site receives the existing named red debug marker.

| Town | Candidate areas |
| --- | ---: |
| Rhodes | 9 |
| Blackwater | 7 |
| Valentine | 9 |
| Strawberry | 7 |
| Saint Denis | 17 |
| Van Horn | 7 |
| Annesburg | 7 |

The additions cover stations, the second Valentine saloon, theatre approaches,
Saint Denis shops/markets/stables, Strawberry's southern loop, Blackwater's waterfront,
and both new towns. The owner's supplied Rhodes camp (`1353,-1160,82`) and Blackwater
northern civic approach (`-816,-1221,43.5`) are included. The nearby sourced Rhodes
homes remain a separate stop. The exact Blackwater tailor doorstep has no verified
outdoor point; its broader waterfront shop street is included.

Blackwater's supplied point is a public outdoor approach with normal runtime checks,
not an active construction job. Research found late-story civic IPL/navmesh queries,
but neither establishes that a construction crew is working. Construction-specific
work, shop purchases, minigame enrollment, show attendance and tram travel remain
disabled. Business-front visiting windows remain authored habits, not business hours.
The [existing-town source review](routine-existing-sites-proposal.md) and
[new-town source review](routine-new-towns-proposal.md) record exact coordinates and
their context; map pixels were not used to guess ground height.

## Behavior changes

Arrival now starts native area wandering directly. Removed the forced smoking/drinking
scenario assignment and 40–75-second dwell. The game may choose its own ambient pauses;
the debug panel reports actual smoke, drink or other scenario observations. An active
ambient scenario during Wandering suppresses dropped-task recovery and optional
60-second fallback reconsideration. Normal phase changes, closures, unloaded navigation
and encounter/restraint priorities retain precedence. After an ambient pause ends,
bounded task recovery and deferred fallback reconsideration resume.

Destination revalidation uses residency checks during an ambient scenario and for five
seconds afterward to avoid rejecting the target's own props. Full clearance checks
resume after that interval; selecting another stop clears the allowance. Initial
spawn, deployment and placement safety checks are unchanged. No active target is
teleported. Native evidence: `beat_public_hanging.c:16263–16270` for navigation followed
by area wander; `beat_town_robbery.c:3097,14391` for interior area wander and scenario
observation; bundled `natives.h:7071–7072,9999` for the used declarations.

Saint Denis now generates laborers as well as dock workers. Otherwise its laborer-only
market/stable destinations could appear as markers yet never occur on an issued route.
Van Horn uses its boat-crew model and Annesburg its town laborer model. Town selection
uses explicit town identifiers for these model/occupation mappings.

## Validation and artifact

`tests/run-tests.ps1` passed all 20 suites. Relevant checks: plan 156,021; spawn/catalog
2,366; runtime 630; logic 8,470; card 107,693; debug markers 977; debug bridge 1,333.
Coverage includes every enabled site's selection by an issued profile, complete
same-town routes, bounded card text and search circles, ambient pauses and deferred
retries, unchanged travel recovery, and encounter priority. Protected `script.cpp`,
`contract_data.h`, `routine_spawn.h`, `routine_card.h` and `target_ai.h` have no changes
from `a7b243d`; the portrait/card/handoff/payment implementations and final draw order
therefore retain that build's code.

Release/x64 succeeded with the following explicit workspace-only output overrides:

```powershell
$bountyMsbuild = 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe'
$bountyRoot = (Get-Location).Path
& $bountyMsbuild (Join-Path $bountyRoot 'rdr2 scripting environment\samples\Pools\Pools.vcxproj') /t:Build /p:Configuration=Release /p:Platform=x64 "/p:OutDir=$bountyRoot/dist/" "/p:IntDir=$bountyRoot/tmp/Release-x64/" /m /nologo /verbosity:minimal
```

`dist/TestScript.asi`: 314,880 bytes, SHA-256
`D8533549A3D7885B2136D8F22B87A668BCCC8B34B6EB89B28FC60B9F3A6D75B1`.
This supersedes the prior startup-diagnostic ASI and retains its trace logging. No file
was installed into the game directory. The owner's successful U request after a full
restart is recorded in the startup investigation; the earlier FFFFFFFF cause remains
unconfirmed.

## In-game checks still required

1. Fully restart the game with this ASI. Inspect named red markers in all seven towns,
   including the two supplied points; use F8 to hide/show the display.
2. Issue a contract, check its portrait and habits, and follow the target through an
   arrival and a clock transition. It should wander on arrival without forced smoke.
3. Observe any naturally chosen pause, including at a fallback. It should not restart
   every minute. Check the new narrow frontages, wharves and mine approach for blocked
   paths, wrong levels, water/rail edges and reachable corpse locations.
4. Provoke, lasso, hogtie and release the target. Verify encounter priority, then kill,
   photograph and collect once. Test the civic approach in the available story layouts;
   report its XYZ and debug stop name if geometry rejects it or a route looks wrong.

Automated native shims do not establish game navigation, ambient activity selection,
marker appearance, font readability or the absence of an engine crash.
