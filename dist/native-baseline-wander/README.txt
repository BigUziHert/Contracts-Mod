Bounty Contracts - original wander handoff
Build: native-baseline-wander-v3

Removed SET_PED_KEEP_TASK(true) immediately after TASK_WANDER_IN_AREA to match
the owner's supplied working wander path. No other AI behavior changed in v3.
The effect on endless smoking still needs an in-game check with a fresh target
before combat. Combat's existing SET_PED_KEEP_TASK calls remain unchanged.

Includes the U update: U requests a new bounty in every contract state; I inspects
the current card. U puts away this mod's card and waits for active handoffs or
temporary interaction restrictions. Repeated U presses during preparation share
one request. A photographed bounty replaced before hand-in is abandoned without
payment. Money already granted by the clerk is credited once before replacement.

INSTALL
Fully exit RDR2. Copy TestScript.asi beside RDR2.exe, replacing this mod's previous
ASI. Keep one copy loaded, then restart. ScriptHookRDR2 and an ASI loader are
required. An in-game ASI reload is unsuitable for a clean test.

If needed, merge the included lml folder with the game's lml folder for the native
Contract Information title. Requires Lenny's Mod Loader. Existing identical
BountyContracts title resources can be kept.

VALIDATION
Release x64 build, all 10 target AI bridge scenario groups, and all 11 startup
trace checks passed. In-game smoking duration and the U behavior need testing.

Release x64, 256000 bytes
SHA256: 398A06976576DD2565225B3EB28576552D24E37BA1CDACB3F2E32423C1AE29F4
