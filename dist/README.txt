Bounty Contracts

Installation
1. Install Alexander Blade's ScriptHookRDR2 and an ASI loader for RDR2.
2. Close the game and copy TestScript.asi into the folder containing RDR2.exe,
   replacing the previous copy of this mod. Keep only one copy loaded.
3. For the native "Contract Information" card title, install Lenny's Mod Loader
   and merge the included lml folder into the game's existing lml folder.

ScriptHookRDR2 and Lenny's Mod Loader are not included. The LML resource supplies
only the card-title text. Portrait generation and bounty gameplay do not require
it. Without the resource, the code attempts a fallback, but the native title may
be missing. The no-LML configuration has not yet been tested in-game.

Playing
- Visit a station clerk and select Get Contract to receive the target's card,
  or press U to receive a contract remotely. Pressing U during an unfinished
  hunt ends it and issues a new contract; after the corpse photo, U reopens
  the card so the reward can still be collected.
- Find the target and complete the hunt.
- Photograph the target's corpse when prompted, then return to the clerk for
  payment.
- Press I to inspect the current contract card again. Use the game's Zoom,
  Flip, and Put Away prompts while inspecting it.

Validation
The portrait fix was confirmed working in-game from the user's screenshots of
dev-10-sp. Repeated contracts through U, with a new target on every card, were
confirmed in-game with the portrait slot rotation and download-handle fix.

Development
Ongoing work uses dev. Updates to main are made when explicitly requested.
