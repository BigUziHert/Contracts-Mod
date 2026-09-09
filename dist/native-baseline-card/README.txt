Bounty Contracts — native ambient baseline + contract card
Build: native-baseline-card-v1

This version restores the working 61f3ad8 native wandering behavior and original
contract areas. It retains the current physical photo card, portrait/card fixes,
and combat / loss-of-sight / search / re-engagement system. A completed search
returns the target to native wandering. There are no scripted daily routes,
forced ambient activities, fallback stroll/pause loops, or routine debug overlay.

INSTALL
1. Fully exit RDR2.
2. Copy TestScript.asi beside RDR2.exe, replacing this mod's previous ASI.
   Keep only one copy loaded. ScriptHookRDR2 and an ASI loader are required.
3. If needed, merge the included lml folder with the game's lml folder for the
   native Contract Information title. Requires Lenny's Mod Loader; an existing
   identical BountyContracts title resource can be kept.
4. Restart the game. Use a full restart rather than an in-game ASI reload.

PLAY
U: new/replace unfinished contract. I: inspect its photo card.
Clerks: Get Contract / Collect Payment. Photograph the corpse before payment.
The search circle stays at the original contract area.

CHECK
Watch the unprovoked target's native ambient behavior. Then provoke him, break
line of sight, let the search finish, and check ambient behavior again. Returning
into sight should allow re-engagement. Also test repeated cards, armed card
inspection, clerk handoff, corpse photo and payment.

The old ambient version and modern card were previously tested in-game by the
owner. This combined build passes code-level checks but still needs an in-game
check for animation, pathfinding and rendering.

Release x64, 257024 bytes
SHA256: 2F588EB26A0647C93B3D484671918211D1CF7100D3CE5058D4C625F8BA625EA8
Startup trace identifier: build=native-baseline-card-v1
