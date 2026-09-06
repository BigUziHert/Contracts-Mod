param(
    [string]$VisualStudio = 'C:\Program Files\Microsoft Visual Studio\2022\Community'
)

$ErrorActionPreference = 'Stop'
$bountyRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$bountyOutput = Join-Path $bountyRoot 'tmp\tests'
$bountySourcePath = Join-Path $bountyRoot 'rdr2 scripting environment\samples\Pools\script.cpp'
$bountyDataPath = Join-Path $bountyRoot 'rdr2 scripting environment\samples\Pools\contract_data.h'
$bountyVcVars = Join-Path $VisualStudio 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path -LiteralPath $bountyVcVars)) { throw 'Visual Studio C++ tools not found.' }
New-Item -ItemType Directory -Path $bountyOutput -Force | Out-Null
$bountySource = [IO.File]::ReadAllText($bountySourcePath)
$bountyData = [IO.File]::ReadAllText($bountyDataPath)
$bountyHeader = @('#pragma once', '// Generated from production source; do not edit.', 'namespace Tune {')
foreach ($bountyConstant in @('kArmedChancePct', 'kGunVsKnifePct', 'kReAggroSightDist', 'kRetainSightDist', 'kDeAggroGraceMs', 'kTargetSearchMs')) {
    $bountyMatches = [regex]::Matches($bountyData, ('constexpr\s+\w+\s+' + $bountyConstant + '\s*=[^;]+;'))
    if ($bountyMatches.Count -ne 1) { throw "Expected exactly one production constant: $bountyConstant" }
    $bountyHeader += $bountyMatches[0].Value
}
$bountyHeader += '}'
foreach ($bountyFunction in @('StartWander', 'SetupHumanTarget', 'EnterCombat', 'PlayerProvoked', 'UpdateHumanTarget')) {
    $bountyMatches = [regex]::Matches($bountySource, ('(?ms)^static (?:void|bool) ' + $bountyFunction + '\([^\r\n]+\)\s*\{.*?^\}'))
    if ($bountyMatches.Count -ne 1) { throw "Expected exactly one production function: $bountyFunction" }
    $bountyLine = 1 + ([regex]::Matches($bountySource.Substring(0, $bountyMatches[0].Index), '\n')).Count
    $bountyHeader += '#line {0} "{1}"' -f $bountyLine, $bountySourcePath.Replace('\', '/')
    $bountyHeader += $bountyMatches[0].Value
}
[IO.File]::WriteAllText((Join-Path $bountyOutput 'target_ai_bridge_under_test.h'), ($bountyHeader -join "`r`n"))
$bountyCommands = @('@echo off', ('call "{0}" >nul' -f $bountyVcVars), 'if errorlevel 1 exit /b 1')
$bountyCommands += 'cl /nologo /std:c++20 /EHsc /W4 /WX /MT /Od /I"{0}" /Fo"{1}" /Fe"{2}" "{3}"' -f `
    $bountyOutput, (Join-Path $bountyOutput 'target_ai_bridge_tests.obj'), (Join-Path $bountyOutput 'target_ai_bridge_tests.exe'), `
    (Join-Path $PSScriptRoot 'target_ai_bridge_tests.cpp')
$bountyCommands += 'if errorlevel 1 exit /b 1'
$bountyCommands += '"' + (Join-Path $bountyOutput 'target_ai_bridge_tests.exe') + '"'
$bountyCommands += 'if errorlevel 1 exit /b 1'
$bountyCommands += 'exit /b 0'
$bountyCommandFile = Join-Path $bountyOutput 'run-target-ai-bridge-tests.cmd'
[IO.File]::WriteAllLines($bountyCommandFile, $bountyCommands, [Text.Encoding]::Default)
& $env:ComSpec /d /c $bountyCommandFile
if ($LASTEXITCODE -ne 0) { throw 'Target AI bridge regression tests failed.' }
