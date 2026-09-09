param([string]$VisualStudio = 'C:\Program Files\Microsoft Visual Studio\2022\Community')
$ErrorActionPreference = 'Stop'
$bountyRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$bountyOutput = Join-Path $bountyRoot 'tmp\tests'
$bountySourcePath = Join-Path $bountyRoot 'rdr2 scripting environment\samples\Pools\script.cpp'
$bountyDataPath = Join-Path $bountyRoot 'rdr2 scripting environment\samples\Pools\contract_data.h'
$bountyVcVars = Join-Path $VisualStudio 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path -LiteralPath $bountyVcVars)) { throw 'Visual Studio C++ tools not found; pass -VisualStudio.' }
New-Item -ItemType Directory -Path $bountyOutput -Force | Out-Null
$bountySource = [IO.File]::ReadAllText($bountySourcePath)
$bountyData = [IO.File]::ReadAllText($bountyDataPath)
$bountyHeader = @('#pragma once', '// Extracted production card startup; do not edit.', 'namespace Card {')
foreach ($bountyName in @('kItem', 'kPropModel', 'kPrimaryItem', 'kStateIntro', 'kStateOutro', 'kStartState',
    'kFlipBlackboard', 'kTitleLabel', 'kTaskStartWaitMs', 'kHandsReadyWaitMs', 'kHandsSettleMs')) {
    $bountyMatches = [regex]::Matches($bountyData, ('constexpr\s+[^;\r\n]*\b{0}\s*=[^;]+;' -f $bountyName))
    if ($bountyMatches.Count -ne 1) { throw "Expected exactly one production card constant: $bountyName" }
    $bountyHeader += $bountyMatches[0].Value
}
$bountyHeader += '}'
foreach ($bountyPattern in @(
    '(?ms)^struct CardRuntime\s*\{.*?^\};',
    '(?m)^static CardRuntime\s+Cd;',
    '(?m)^static bool LivingPed\(Ped ped\)[^\r\n]+',
    '(?m)^static bool PlayerAvailable\(\)[^\r\n]+',
    '(?ms)^static bool CanStartInteraction\(\)\s*\{.*?^\}',
    '(?ms)^static bool OwnCardTaskRunning\(\)\s*\{.*?^\}',
    '(?ms)^template<typename Pred> static bool WaitUntil\(DWORD timeoutMs, Pred pred\)\s*\{.*?^\}',
    '(?ms)^static bool CardWeaponInHand\(\)\s*\{.*?^\}',
    '(?ms)^static bool PreparePlayerForCard\(\)\s*\{.*?^\}',
    '(?ms)^static bool OpenCard\(bool reuseHandoffCard = false\)\s*\{.*?^\}'
)) {
    $bountyMatches = [regex]::Matches($bountySource, $bountyPattern)
    if ($bountyMatches.Count -ne 1) { throw "Expected exactly one production declaration: $bountyPattern" }
    $bountyLine = 1 + ([regex]::Matches($bountySource.Substring(0, $bountyMatches[0].Index), '\n')).Count
    $bountyHeader += '#line {0} "{1}"' -f $bountyLine, $bountySourcePath.Replace('\', '/')
    $bountyHeader += $bountyMatches[0].Value
}
[IO.File]::WriteAllText((Join-Path $bountyOutput 'card_start_under_test.h'), ($bountyHeader -join "`r`n"))
$bountyCommands = @('@echo off', ('call "{0}" >nul' -f $bountyVcVars), 'if errorlevel 1 exit /b 1')
$bountyCommands += 'cl /nologo /std:c++20 /EHsc /W4 /WX /MT /Od /I"{0}" /Fo"{1}" /Fe"{2}" "{3}"' -f `
    $bountyOutput, (Join-Path $bountyOutput 'card_start_tests.obj'), (Join-Path $bountyOutput 'card_start_tests.exe'), `
    (Join-Path $PSScriptRoot 'card_start_tests.cpp')
$bountyCommands += 'if errorlevel 1 exit /b 1'
$bountyCommands += '"' + (Join-Path $bountyOutput 'card_start_tests.exe') + '"'
$bountyCommands += 'if errorlevel 1 exit /b 1'
$bountyCommands += 'exit /b 0'
$bountyCommandFile = Join-Path $bountyOutput 'run-card-start-tests.cmd'
[IO.File]::WriteAllLines($bountyCommandFile, $bountyCommands, [Text.Encoding]::Default)
& $env:ComSpec /d /c $bountyCommandFile
if ($LASTEXITCODE -ne 0) { throw 'Card start regression tests failed.' }
