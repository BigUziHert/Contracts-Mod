# Bounty Contracts

A Red Dead Redemption 2 single-player mod written in C++ with Alexander Blade's ScriptHookRDR2. Station clerks offer bounty contracts with a physical photo card showing the target.

1. Visit a station clerk and choose **Get Contract**.
2. Read the card, find the target, and complete the hunt.
3. Photograph the target's corpse when prompted.
4. Return to the clerk to collect payment.

Press **I** to inspect the current contract card again. Use the game's own **Zoom**, **Flip**, and **Put Away** prompts while inspecting it.

## Installation

1. Install Alexander Blade's ScriptHookRDR2 and an ASI loader for RDR2.
2. Close the game and copy `TestScript.asi` from `dist` into the folder containing `RDR2.exe`. Replace the previous copy of this mod; keep only one copy loaded.
3. To use the native **Contract Information** card title, install Lenny's Mod Loader and merge the included `dist/lml` folder into the game's existing `lml` folder.

ScriptHookRDR2 and Lenny's Mod Loader are not included. The LML resource supplies only the card-title text; it does not provide the portrait or bounty gameplay. The code attempts a fallback without that resource, but the native title may be missing. The no-LML configuration has not yet been tested in-game.

The portrait fix was confirmed working in-game from the user's screenshots of dev-10-sp. The subsequent cleanup build has not yet been tested in-game.

## Building

Use Visual Studio 2022 with the v143 C++ toolset and Windows 10 SDK. The project is `rdr2 scripting environment/samples/Pools/Pools.vcxproj`, with configuration **Release | x64** and output name `TestScript.asi`.

Run this PowerShell command from the repository root. It overrides the project's output directory so the build stays inside the workspace; it does not install anything into the game folder.

```powershell
$bountyMsbuild = 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe'
$bountyRoot = (Get-Location).Path
$bountyBuildArgs = @(
    (Join-Path $bountyRoot 'rdr2 scripting environment\samples\Pools\Pools.vcxproj'),
    '/t:Build',
    '/p:Configuration=Release',
    '/p:Platform=x64',
    "/p:OutDir=$bountyRoot/dist/",
    "/p:IntDir=$bountyRoot/tmp/Release-x64/",
    '/m',
    '/nologo'
)
& $bountyMsbuild @bountyBuildArgs
```

Adjust the MSBuild path if Visual Studio is installed elsewhere. The ScriptHookRDR2 headers and x64 import library are included in the scripting environment.

## Development

Use `dev` for ongoing work. Update `main` when explicitly requested.

## SDK and research resources

- [Alexander Blade's ScriptHookRDR2](http://www.dev-c.com/rdr2/scripthookrdr2/)
- [alloc8or native database](https://alloc8or.re/rdr3/nativedb/)
- [alloc8or RDR3 documentation](https://alloc8or.re/rdr3/doc/)
- [femga's RDR3 discoveries](https://github.com/femga/rdr3_discoveries)
- [RDR3 script global research](https://github.com/Halen84/RDR3-Script-Global-Research)
- [RDR3 native flags and enums](https://github.com/Halen84/RDR3-Native-Flags-And-Enums)
- [RDR3 decompiled scripts](https://github.com/Halen84/RDR3-Decompiled-Scripts)
- [RDR3 parser dumps, build 1491](https://alexguirre.github.io/rage-parser-dumps/dump.html?game=rdr3&build=1491)
