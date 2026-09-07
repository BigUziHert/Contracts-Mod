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
$bountyConstant = [regex]::Matches($bountyData, 'constexpr\s+\w+\s+kSpawnAttempts\s*=[^;]+;')
if ($bountyConstant.Count -ne 1) { throw 'Expected exactly one production kSpawnAttempts.' }
$bountyHeader = @('#pragma once', '// Extracted production startup orchestration; do not edit.', 'namespace Tune {', $bountyConstant[0].Value, '}')
$bountyPatterns = @(
    '(?m)^enum ContractState\s*\{[^\r\n]+\};',
    '(?m)^enum class ContractStartFailure\s*\{[^\r\n]+\};',
    '(?m)^static ContractState\s+g_state[^\r\n]+;',
    '(?m)^static ContractStartFailure lastStartFailure[^\r\n]+;',
    '(?m)^static const char\* lastPhotoStage[^\r\n]+;',
    '(?ms)^static bool StartContract\(\)\s*\{.*?^\}',
    '(?ms)^struct PendingContractStart\s*\{.*?^\};',
    '(?m)^static PendingContractStart pendingContractStart;',
    '(?ms)^static void CancelPendingContractStart\(\)\s*\{.*?^\}',
    '(?ms)^static void RequestContractStart\(Ped giver\)\s*\{.*?^\}',
    '(?ms)^static void UpdatePendingContractStart\(\)\s*\{.*?^\}'
)
foreach ($bountyPattern in $bountyPatterns) {
    $bountyMatches = [regex]::Matches($bountySource, $bountyPattern)
    if ($bountyMatches.Count -ne 1) { throw "Expected exactly one production declaration: $bountyPattern" }
    $bountyLine = 1 + ([regex]::Matches($bountySource.Substring(0, $bountyMatches[0].Index), '\n')).Count
    $bountyHeader += '#line {0} "{1}"' -f $bountyLine, $bountySourcePath.Replace('\', '/')
    $bountyHeader += $bountyMatches[0].Value
}
[IO.File]::WriteAllText((Join-Path $bountyOutput 'contract_start_under_test.h'), ($bountyHeader -join "`r`n"))
$bountyCommands = @('@echo off', ('call "{0}" >nul' -f $bountyVcVars), 'if errorlevel 1 exit /b 1')
$bountyCommands += 'cl /nologo /std:c++20 /EHsc /W4 /WX /MT /Od /I"{0}" /Fo"{1}" /Fe"{2}" "{3}"' -f `
    $bountyOutput, (Join-Path $bountyOutput 'contract_start_tests.obj'), (Join-Path $bountyOutput 'contract_start_tests.exe'), `
    (Join-Path $PSScriptRoot 'contract_start_tests.cpp')
$bountyCommands += 'if errorlevel 1 exit /b 1'
$bountyCommands += '"' + (Join-Path $bountyOutput 'contract_start_tests.exe') + '"'
$bountyCommands += 'if errorlevel 1 exit /b 1'
$bountyCommands += 'exit /b 0'
$bountyCommandFile = Join-Path $bountyOutput 'run-contract-start-tests.cmd'
[IO.File]::WriteAllLines($bountyCommandFile, $bountyCommands, [Text.Encoding]::Default)
& $env:ComSpec /d /c $bountyCommandFile
if ($LASTEXITCODE -ne 0) { throw 'Contract start integration tests failed.' }
