# Startup crash investigation — 6 September 2026

The screenshot shows `Unknown error FFFFFFFF` while `PREPARING CONTRACT` remains on
screen. That subtitle covers the whole startup operation; it does not identify a
specific native or establish that the debug overlay caused the crash.

## Evidence

- The ScriptHook log read during investigation recorded the game session starting at
  21:28, then `TestScript.asi` unloading at 21:45:16 and loading again at 21:45:40. The
  screenshot clock was 21:46. This was an in-game module reload, not a full restart.
- Cleanup samples at 21:42:58, 21:43:14 and 21:43:32 confirmed three owned target deletions
  while reporting 0, 0 and 1 free ped slots respectively. Those samples establish pool
  pressure then, not the owner of the other peds or the pool count at the crash.
- The failure-only startup log had no new entry after the old 21:07 placement failures.
  A hard failure before a handled return cannot be located from that log.
- No matching Windows application crash entry was found in the queried interval.
  Later ScriptHook output showed a fresh game session at 21:47. In subsequent feedback,
  the owner confirmed that pressing U worked after fully relaunching the game. This
  establishes a successful restart test, not the cause of the earlier crash.

`main.cpp` unregisters the script and keyboard callback on DLL detach. It does not
explicitly retire contract/card/prompt/debug resources or preserve the session's bound
portrait-slot mask. Reloading resets those C++ fields; if game resources survive, the
next capture may reuse an already bound slot. Prior testing established download failure
from such reuse, not this FFFFFFFF crash. Full game restart is the clean test control.
Yielding contract cleanup must not be added directly to `DllMain`.

The debug build only added its include, F8 toggle and final-frame update to `script.cpp`.
Its debug observer/marker updater is not called by the preparation waits. The red dots
can remain visible without debug code executing at the failing stage. They create map
blips, not peds. The exact crash cause remains unconfirmed.

## Changes

The existing free-slot check was immediately before `CREATE_PED`, after remote town
and target-model loading. Preflight now also refuses a nonpositive free-slot count
before destination preparation and before clearing the current hunt. One available
slot still permits creation; no guessed reserve or population removal is added. Player
availability and portrait-slot exhaustion keep their prior diagnostic priority, and the
existing final creation check remains. Local Rockstar `coachrobberies_intro.c:2593–2603`
checks one free ped before proceeding; `coachrobberies.c:4831–4841` checks its required
count. This closes an ordering gap without claiming it caused the crash.

`BountyContracts-startup-trace.log` now records module start, U requests, pool preflight,
candidate preparation, owned scene-load boundaries, navigation/collision waits, model
loading, ped creation, portrait entry/return, deployment/settling and contract readiness.
Every event appends and closes the file before the next operation. Records include UTC,
process ID, module-session timestamp, build label, stage, supplied model/ped/coordinates,
known photo slot/download/stage and ownership handle. `freePeds=-1` means no measurement
was supplied; it does not mean the pool was full. The logger makes no additional game
native calls. Trace output occurs only on bounded startup paths, not ordinary AI frames.

The last line is the last recorded boundary, not a stack trace or proof that the next
native caused a crash. Portrait capture, slot rotation, card/handoff/payment functions,
scene-loading policy and target routines retain their established behavior. This is a
capacity safeguard and diagnostic build.

## Validation and next test

Native-shim regressions cover zero/negative rejection, one-slot acceptance, preserved
contracts, failure priority, immediate preflight, trace ordering around scene calls,
foreign-loader preservation and copied event data. The real extracted Windows/CRT
writer test verifies that each record is immediately readable, appends prior records,
and identifies separate module sessions. All twenty suites passed, including 2,771
spawn checks, 690 routine-spawn checks and 11 real file-writer checks. Release/x64 built
successfully into `dist` with the README's explicit output overrides. All eighteen
protected functions and `contract_data.h` match `26c1cca`. Neither automated tests nor
compilation establish the engine crash cause.

The updated ASI is 310,272 bytes, SHA-256
`072172F4D666F593E7EB3AB4E352850EFF5C434A667C8DC5224A627785D9D26D`.

Close RDR2 fully, replace the previous ASI, restart story mode and press U once. If it
crashes again, retain the new startup trace together with startup/cleanup and ScriptHook
logs. If a pool-full message appears instead, the new preflight has refused additional
preparation while preserving any existing hunt.
