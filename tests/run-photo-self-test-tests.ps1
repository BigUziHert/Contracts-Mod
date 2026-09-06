param(
    [string]$VisualStudio = 'C:\Program Files\Microsoft Visual Studio\2022\Community'
)

$ErrorActionPreference = 'Stop'
$bountyRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$bountyOutput = Join-Path $bountyRoot 'tmp\tests'
$bountySourcePath = Join-Path $bountyRoot 'rdr2 scripting environment\samples\Pools\script.cpp'
$bountyDataPath = Join-Path $bountyRoot 'rdr2 scripting environment\samples\Pools\contract_data.h'
$bountyVcVars = Join-Path $VisualStudio 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path -LiteralPath $bountyVcVars)) {
    throw 'Visual Studio C++ tools not found; pass -VisualStudio with your installation directory.'
}
New-Item -ItemType Directory -Path $bountyOutput -Force | Out-Null
$bountySource = [IO.File]::ReadAllText($bountySourcePath)
$bountyData = [IO.File]::ReadAllText($bountyDataPath)
$bountyStateHeader = @('#pragma once', '// Generated from production source; do not edit.', 'namespace Card {')
foreach ($bountyConstant in @('kPhotoSlot', 'kPhotoCacheType', 'kPhotoNameMs')) {
    $bountyMatches = [regex]::Matches($bountyData, ('constexpr\s+[^;\r\n]+?\b' + $bountyConstant + '\s*=[^;]+;'))
    if ($bountyMatches.Count -ne 1) { throw "Expected exactly one production constant: $bountyConstant" }
    $bountyStateHeader += $bountyMatches[0].Value
}
$bountyStateHeader += '}'
$bountyState = [regex]::Matches($bountySource, '(?ms)^\s*// target portrait\s*\r?\n(.*?)^\s*// hand-in')
if ($bountyState.Count -ne 1) { throw 'Expected exactly one target portrait state block.' }
$bountyStateHeader += 'static struct PortraitState {'
$bountyDefinition = [regex]::Matches($bountySource, '(?m)^\s*const ContractDef\* def = nullptr;')
if ($bountyDefinition.Count -ne 1) { throw 'Expected exactly one production contract definition pointer.' }
$bountyStateHeader += $bountyDefinition[0].Value
$bountyStateHeader += $bountyState[0].Groups[1].Value
$bountyStateHeader += '} C;'
foreach ($bountyPattern in @(
    '(?m)^enum class ContractStartFailure[^\r\n]+;',
    '(?m)^static ContractStartFailure lastStartFailure[^\r\n]+;',
    '(?m)^static const char\* lastPhotoStage[^\r\n]+;',
    '(?m)^static unsigned photoTestBindAttempts[^\r\n]+;',
    '(?m)^static unsigned photoTestBindingTransitions[^\r\n]+;',
    '(?m)^static bool photoTestPlainCard[^\r\n]+;',
    '(?m)^static const char\* photoTestControl[^\r\n]+;'
)) {
    $bountyMatches = [regex]::Matches($bountySource, $bountyPattern)
    if ($bountyMatches.Count -ne 1) { throw "Expected exactly one production declaration: $bountyPattern" }
    $bountyStateHeader += $bountyMatches[0].Value
}
$bountyHeader = @('#pragma once', '// Generated from production source; do not edit.')
foreach ($bountyPattern in @(
    '(?ms)^template<typename Pred> static bool WaitUntil\([^\r\n]+\)\s*\{.*?^\}',
    '(?ms)^static bool ProbeExistingPhoto\(char \(&name\)\[64\]\)\s*\{.*?^\}',
    '(?ms)^static bool RunPhotoCacheSelfTest\(Ped subject\)\s*\{.*?^\}'
)) {
    $bountyMatches = [regex]::Matches($bountySource, $bountyPattern)
    if ($bountyMatches.Count -ne 1) { throw "Expected exactly one production function: $bountyPattern" }
    $bountyLine = 1 + ([regex]::Matches($bountySource.Substring(0, $bountyMatches[0].Index), '\n')).Count
    $bountyHeader += '#line {0} "{1}"' -f $bountyLine, $bountySourcePath.Replace('\', '/')
    $bountyHeader += $bountyMatches[0].Value
}
$bountyCaller = [regex]::Matches($bountySource, '(?ms)^#ifdef BOUNTY_PHOTO_SELF_TEST\s*\r?\n(\s*ENTITY::SET_ENTITY_VISIBLE\(ped, false\);.*?)(?=^#endif)')
if ($bountyCaller.Count -ne 1) { throw 'Expected exactly one diagnostic caller branch.' }
$bountyHeader += 'static Ped RunPhotoTestAsCaller(Ped ped) {'
$bountyHeader += 'const ContractDef def{}; // Stand-in metadata supplied by the real caller parameter.'
$bountyHeader += $bountyCaller[0].Groups[1].Value
$bountyHeader += '}'
[IO.File]::WriteAllText((Join-Path $bountyOutput 'photo_self_test_state.h'), ($bountyStateHeader -join "`r`n"))
[IO.File]::WriteAllText((Join-Path $bountyOutput 'photo_self_test_under_test.h'), ($bountyHeader -join "`r`n"))
$bountyCommands = @('@echo off', ('call "{0}" >nul' -f $bountyVcVars), 'if errorlevel 1 exit /b 1')
$bountyCommands += 'cl /nologo /std:c++20 /EHsc /W4 /WX /MT /Od /I"{0}" /Fo"{1}" /Fe"{2}" "{3}"' -f `
    $bountyOutput, (Join-Path $bountyOutput 'photo_self_test_tests.obj'), (Join-Path $bountyOutput 'photo_self_test_tests.exe'), `
    (Join-Path $PSScriptRoot 'photo_self_test_tests.cpp')
$bountyCommands += 'if errorlevel 1 exit /b 1'
$bountyCommands += '"' + (Join-Path $bountyOutput 'photo_self_test_tests.exe') + '"'
$bountyCommands += 'if errorlevel 1 exit /b 1'
$bountyCommands += 'exit /b 0'
$bountyCommandFile = Join-Path $bountyOutput 'run-photo-self-test-tests.cmd'
[IO.File]::WriteAllLines($bountyCommandFile, $bountyCommands, [Text.Encoding]::Default)
& $env:ComSpec /d /c $bountyCommandFile
if ($LASTEXITCODE -ne 0) { throw 'Photo self-test driver regression tests failed.' }
