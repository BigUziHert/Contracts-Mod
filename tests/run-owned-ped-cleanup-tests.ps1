param([string]$VisualStudio = 'C:\Program Files\Microsoft Visual Studio\2022\Community')
$ErrorActionPreference = 'Stop'
$bountyRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$bountyOutput = Join-Path $bountyRoot 'tmp\tests'
$bountySourcePath = Join-Path $bountyRoot 'rdr2 scripting environment\samples\Pools\script.cpp'
$bountyVcVars = Join-Path $VisualStudio 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path -LiteralPath $bountyVcVars)) { throw 'Visual Studio C++ tools not found; pass -VisualStudio.' }
New-Item -ItemType Directory -Path $bountyOutput -Force | Out-Null
$bountySource = [IO.File]::ReadAllText($bountySourcePath)
$bountyHeader = @('#pragma once', '// Extracted from production source; do not edit.')
$bountyPatterns = @(
    '(?ms)^struct OwnedPedRuntime\s*\{.*?^\};',
    '(?m)^static OwnedPedRuntime ownedPed;',
    '(?m)^static unsigned long long ownedPedsCreated[^\r\n]+;',
    '(?m)^static constexpr DWORD kOwnedPedDeleteRetryMs[^\r\n]+;',
    '(?m)^static constexpr DWORD kOwnedPedCleanupWaitMs[^\r\n]+;',
    '(?ms)^static void TrackOwnedPed\([^\r\n]+\)\s*\{.*?^\}',
    '(?ms)^static bool OwnedPedIdentityMatches\(\)\s*\{.*?^\}',
    '(?ms)^static void MaintainOwnedPedCleanup\(\)\s*\{.*?^\}',
    '(?ms)^static void RequestOwnedPedCleanup\([^\r\n]+\)\s*\{.*?^\}',
    '(?ms)^static void ReleaseOwnedPed\([^\r\n]+\)\s*\{.*?^\}'
)
foreach ($bountyPattern in $bountyPatterns) {
    $bountyMatches = [regex]::Matches($bountySource, $bountyPattern)
    if ($bountyMatches.Count -ne 1) { throw "Expected exactly one production declaration: $bountyPattern" }
    $bountyLine = 1 + ([regex]::Matches($bountySource.Substring(0, $bountyMatches[0].Index), '\n')).Count
    $bountyHeader += '#line {0} "{1}"' -f $bountyLine, $bountySourcePath.Replace('\', '/')
    $bountyHeader += $bountyMatches[0].Value
}
[IO.File]::WriteAllText((Join-Path $bountyOutput 'owned_ped_cleanup_under_test.h'), ($bountyHeader -join "`r`n"))
$bountyCommands = @('@echo off', ('call "{0}" >nul' -f $bountyVcVars), 'if errorlevel 1 exit /b 1')
$bountyCommands += 'cl /nologo /std:c++20 /EHsc /W4 /WX /MT /Od /I"{0}" /Fo"{1}" /Fe"{2}" "{3}"' -f `
    $bountyOutput, (Join-Path $bountyOutput 'owned_ped_cleanup_tests.obj'), (Join-Path $bountyOutput 'owned_ped_cleanup_tests.exe'), `
    (Join-Path $PSScriptRoot 'owned_ped_cleanup_tests.cpp')
$bountyCommands += 'if errorlevel 1 exit /b 1'
$bountyCommands += '"' + (Join-Path $bountyOutput 'owned_ped_cleanup_tests.exe') + '"'
$bountyCommands += 'if errorlevel 1 exit /b 1'
$bountyCommands += 'exit /b 0'
$bountyCommandFile = Join-Path $bountyOutput 'run-owned-ped-cleanup-tests.cmd'
[IO.File]::WriteAllLines($bountyCommandFile, $bountyCommands, [Text.Encoding]::Default)
& $env:ComSpec /d /c $bountyCommandFile
if ($LASTEXITCODE -ne 0) { throw 'Owned ped cleanup regression tests failed.' }
