param([string]$VisualStudio = 'C:\Program Files\Microsoft Visual Studio\2022\Community')
$ErrorActionPreference = 'Stop'
$bountyRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$bountyOutput = Join-Path $bountyRoot 'tmp\tests'
$bountyVcVars = Join-Path $VisualStudio 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path -LiteralPath $bountyVcVars)) { throw 'Visual Studio C++ tools not found.' }
New-Item -ItemType Directory -Path $bountyOutput -Force | Out-Null
$bountySourcePath = Join-Path $bountyRoot 'rdr2 scripting environment\samples\Pools\script.cpp'
$bountySource = [IO.File]::ReadAllText($bountySourcePath)
$bountyHeader = @('#pragma once', '// Actual production file writer; no RDR native API is present in this test.')
foreach ($bountyPattern in @('(?m)^static ULONGLONG startupTraceSession[^\r\n]*;', '(?ms)^static void WriteStartupTrace\(const StartupTrace::Event& event\)\s*\{.*?^\}')) {
    $bountyMatches = [regex]::Matches($bountySource, $bountyPattern)
    if ($bountyMatches.Count -ne 1) { throw "Expected exactly one production declaration: $bountyPattern" }
    $bountyHeader += $bountyMatches[0].Value
}
[IO.File]::WriteAllText((Join-Path $bountyOutput 'startup_trace_writer_under_test.h'), ($bountyHeader -join "`r`n"))
$bountyCommands = @('@echo off', ('call "{0}" >nul' -f $bountyVcVars), 'if errorlevel 1 exit /b 1')
$bountyCommands += 'cl /nologo /std:c++20 /EHsc /W4 /WX /MT /Od /I"{0}" /Fo"{1}" /Fe"{2}" "{3}"' -f `
    $bountyOutput, (Join-Path $bountyOutput 'startup_trace_writer_tests.obj'), (Join-Path $bountyOutput 'startup_trace_writer_tests.exe'), `
    (Join-Path $PSScriptRoot 'startup_trace_writer_tests.cpp')
$bountyCommands += 'if errorlevel 1 exit /b 1'
$bountyCommands += '"' + (Join-Path $bountyOutput 'startup_trace_writer_tests.exe') + '"'
$bountyCommands += 'if errorlevel 1 exit /b 1'
$bountyCommands += 'exit /b 0'
$bountyCommandFile = Join-Path $bountyOutput 'run-startup-trace-writer-tests.cmd'
[IO.File]::WriteAllLines($bountyCommandFile, $bountyCommands, [Text.Encoding]::Default)
& $env:ComSpec /d /c $bountyCommandFile
if ($LASTEXITCODE -ne 0) { throw 'Startup trace writer tests failed.' }
