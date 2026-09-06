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
$bountyHeader = @('#pragma once', '// Extracted from production source; do not edit.', 'namespace Tune {')
foreach ($bountyConstant in @('kPayoutMinCents', 'kPayoutMaxCents', 'kFullPayMinutes', 'kWantedPayoutMult', 'kPayoutStepCents', 'kTrailEnableDist')) {
    $bountyMatches = [regex]::Matches($bountyData, ('constexpr\s+\w+\s+' + $bountyConstant + '\s*=[^;]+;'))
    if ($bountyMatches.Count -ne 1) { throw "Expected exactly one production constant: $bountyConstant" }
    $bountyHeader += $bountyMatches[0].Value
}
$bountyHeader += '}'
$bountyPatterns = @(
    '(?ms)^static int ComputePayoutCents\(\)\s*\{.*?^\}',
    '(?ms)^static void UpdateCrimeTracking\(\)\s*\{.*?^\}',
    '(?ms)^static void ClearContract\(bool deleteTarget\)\s*\{.*?^\}',
    '(?ms)^static void CheckTargetLost\(\)\s*\{.*?^\}',
    '(?ms)^static void UpdateTrails\(\)\s*\{.*?^\}'
)
foreach ($bountyPattern in $bountyPatterns) {
    $bountyMatches = [regex]::Matches($bountySource, $bountyPattern)
    if ($bountyMatches.Count -ne 1) { throw "Expected exactly one production declaration: $bountyPattern" }
    $bountyLine = 1 + ([regex]::Matches($bountySource.Substring(0, $bountyMatches[0].Index), '\n')).Count
    $bountyHeader += '#line {0} "{1}"' -f $bountyLine, $bountySourcePath.Replace('\', '/')
    $bountyHeader += $bountyMatches[0].Value
}
# Extract the real post-interaction guard through its next state check. The do/while
# supplies ScriptMain's loop context so its actual continue bypasses the sentinel.
$bountyGatePattern = '(?ms)(?<=if \(g_state != CONTRACT_PAID\) UpdateGiverPrompt\(\);\r?\n)\s*if \(!PlayerAvailable\(\).*?^\t\tCheckTargetLost\(\);'
$bountyGateMatches = [regex]::Matches($bountySource, $bountyGatePattern)
if ($bountyGateMatches.Count -ne 1) { throw 'Expected exactly one post-interaction player/pause guard.' }
$bountyGateLine = 1 + ([regex]::Matches($bountySource.Substring(0, $bountyGateMatches[0].Index), '\n')).Count
$bountyHeader += 'static void RunPostInteractionGate() { do {'
$bountyHeader += '#line {0} "{1}"' -f $bountyGateLine, $bountySourcePath.Replace('\', '/')
$bountyHeader += $bountyGateMatches[0].Value
$bountyHeader += 'world.passedGate = true; } while (false); }'
[IO.File]::WriteAllText((Join-Path $bountyOutput 'contract_lifecycle_under_test.h'), ($bountyHeader -join "`r`n"))
$bountyCommands = @('@echo off', ('call "{0}" >nul' -f $bountyVcVars), 'if errorlevel 1 exit /b 1')
$bountyCommands += 'cl /nologo /std:c++20 /EHsc /W4 /WX /MT /Od /I"{0}" /Fo"{1}" /Fe"{2}" "{3}"' -f `
    $bountyOutput, (Join-Path $bountyOutput 'contract_lifecycle_tests.obj'), (Join-Path $bountyOutput 'contract_lifecycle_tests.exe'), `
    (Join-Path $PSScriptRoot 'contract_lifecycle_tests.cpp')
$bountyCommands += 'if errorlevel 1 exit /b 1'
$bountyCommands += '"' + (Join-Path $bountyOutput 'contract_lifecycle_tests.exe') + '"'
$bountyCommands += 'if errorlevel 1 exit /b 1'
$bountyCommands += 'exit /b 0'
$bountyCommandFile = Join-Path $bountyOutput 'run-contract-lifecycle-tests.cmd'
[IO.File]::WriteAllLines($bountyCommandFile, $bountyCommands, [Text.Encoding]::Default)
& $env:ComSpec /d /c $bountyCommandFile
if ($LASTEXITCODE -ne 0) { throw 'Contract lifecycle regression tests failed.' }
