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
$bountyHeader = @('#pragma once', '// Generated from production source; do not edit.', 'namespace Tune {')
foreach ($bountyConstant in @('kStreamTimeoutMs', 'kPedSpawnRetryMs', 'kPedSpawnRetryDelayMs')) {
    $bountyMatches = [regex]::Matches($bountyData, ('constexpr\s+\w+\s+' + $bountyConstant + '\s*=[^;]+;'))
    if ($bountyMatches.Count -ne 1) { throw "Expected exactly one production constant: $bountyConstant" }
    $bountyHeader += $bountyMatches[0].Value
}
$bountyHeader += '}'
$bountyHeader += 'namespace Card {'
foreach ($bountyConstant in @('kPhotoSlotCount', 'kStateOutro')) {
    $bountyMatches = [regex]::Matches($bountyData, ('constexpr\s+\w+\s+' + $bountyConstant + '\s*=[^;]+;'))
    if ($bountyMatches.Count -ne 1) { throw "Expected exactly one production constant: $bountyConstant" }
    $bountyHeader += $bountyMatches[0].Value
}
$bountyHeader += '}'
$bountyPatterns = @(
    '(?m)^enum ContractState\s*\{[^\r\n]+\};',
    '(?m)^static ContractState\s+g_state[^\r\n]+;',
    '(?m)^enum class ContractStartFailure\s*\{[^\r\n]+\};',
    '(?m)^static ContractStartFailure lastStartFailure[^\r\n]+;',
    '(?m)^static const char\* lastPhotoStage[^\r\n]+;',
    '(?m)^static unsigned photoSlotsBound[^\r\n]+;',
    '(?m)^static ULONGLONG pausedDurationMs[^\r\n]+;',
    '(?m)^static ULONGLONG pauseStartedMs[^\r\n]+;',
    '(?ms)^static void SetRuntimePaused\(bool paused\)\s*\{.*?^\}',
    '(?ms)^static ULONGLONG RuntimeNowMs\(\)\s*\{.*?^\}',
    '(?m)^static bool LivingPed\(Ped ped\)[^\r\n]+',
    '(?m)^static bool PlayerAvailable\(\)[^\r\n]+',
    '(?ms)^struct OwnedPedRuntime\s*\{.*?^\};',
    '(?m)^static OwnedPedRuntime ownedPed;',
    '(?m)^static unsigned long long ownedPedsCreated[^\r\n]+;',
    '(?m)^static constexpr DWORD kOwnedPedDeleteRetryMs[^\r\n]+;',
    '(?m)^static constexpr DWORD kOwnedPedCleanupWaitMs[^\r\n]+;',
    '(?ms)^static void TrackOwnedPed\([^\r\n]+\)\s*\{.*?^\}',
    '(?ms)^static bool OwnedPedIdentityMatches\(\)\s*\{.*?^\}',
    '(?ms)^static void MaintainOwnedPedCleanup\(\)\s*\{.*?^\}',
    '(?ms)^static void RequestOwnedPedCleanup\([^\r\n]+\)\s*\{.*?^\}',
    '(?ms)^static void ReleaseOwnedPed\([^\r\n]+\)\s*\{.*?^\}',
    '(?ms)^template<typename Pred> static bool WaitUntil\([^\r\n]+\)\s*\{.*?^\}',
    '(?ms)^static bool LoadModel\(Hash model\)\s*\{.*?^\}',
    '(?ms)^static Ped SpawnPed\(Hash model, const Vector3& pos\)\s*\{.*?^\}',
    '(?ms)^static bool CanPrepareContract\(\)\s*\{.*?^\}',
    '(?ms)^static void StartRemoteContract\(\)\s*\{.*?^\}',
    '(?m)^static Ped remoteRequestPlayer[^\r\n]+;',
    '(?m)^static ULONGLONG remoteRequestUntilMs[^\r\n]+;',
    '(?m)^static Ped remoteCardPlayer[^\r\n]+;',
    '(?m)^static ULONGLONG remoteCardUntilMs[^\r\n]+;',
    '(?ms)^static bool ConsumeRemoteContractRequest\(bool pressed\)\s*\{.*?^\}'
)
foreach ($bountyPattern in $bountyPatterns) {
    $bountyMatches = [regex]::Matches($bountySource, $bountyPattern)
    if ($bountyMatches.Count -ne 1) { throw "Expected exactly one production declaration matching: $bountyPattern" }
    $bountyLine = 1 + ([regex]::Matches($bountySource.Substring(0, $bountyMatches[0].Index), '\n')).Count
    $bountyHeader += '#line {0} "{1}"' -f $bountyLine, $bountySourcePath.Replace('\', '/')
    $bountyHeader += $bountyMatches[0].Value
}
[IO.File]::WriteAllText((Join-Path $bountyOutput 'spawn_under_test.h'), ($bountyHeader -join "`r`n"))
$bountyCommands = @('@echo off', ('call "{0}" >nul' -f $bountyVcVars), 'if errorlevel 1 exit /b 1')
$bountyCommands += 'cl /nologo /std:c++20 /EHsc /W4 /WX /MT /Od /I"{0}" /Fo"{1}" /Fe"{2}" "{3}"' -f `
    $bountyOutput, (Join-Path $bountyOutput 'spawn_tests.obj'), (Join-Path $bountyOutput 'spawn_tests.exe'), `
    (Join-Path $PSScriptRoot 'spawn_tests.cpp')
$bountyCommands += 'if errorlevel 1 exit /b 1'
$bountyCommands += '"' + (Join-Path $bountyOutput 'spawn_tests.exe') + '"'
$bountyCommands += 'if errorlevel 1 exit /b 1'
$bountyCommands += 'exit /b 0'
$bountyCommandFile = Join-Path $bountyOutput 'run-spawn-tests.cmd'
[IO.File]::WriteAllLines($bountyCommandFile, $bountyCommands, [Text.Encoding]::Default)
& $env:ComSpec /d /c $bountyCommandFile
if ($LASTEXITCODE -ne 0) { throw 'Spawn regression tests failed.' }
