param([string]$VisualStudio = 'C:\Program Files\Microsoft Visual Studio\2022\Community')
$ErrorActionPreference = 'Stop'
$bountyRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$bountyOutput = Join-Path $bountyRoot 'tmp\tests'
$bountyVcVars = Join-Path $VisualStudio 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path -LiteralPath $bountyVcVars)) { throw 'Visual Studio C++ tools not found.' }
New-Item -ItemType Directory -Path $bountyOutput -Force | Out-Null
$bountyCommands = @('@echo off', ('call "{0}" >nul' -f $bountyVcVars), 'if errorlevel 1 exit /b 1')
$bountyCommands += 'cl /nologo /std:c++20 /EHsc /W4 /WX /MT /Od /I"{0}" /Fo"{1}" /Fe"{2}" "{3}"' -f `
    $bountyOutput, (Join-Path $bountyOutput 'routine_debug_view_tests.obj'), (Join-Path $bountyOutput 'routine_debug_view_tests.exe'), `
    (Join-Path $PSScriptRoot 'routine_debug_view_tests.cpp')
$bountyCommands += 'if errorlevel 1 exit /b 1'
$bountyCommands += '"' + (Join-Path $bountyOutput 'routine_debug_view_tests.exe') + '"'
$bountyCommands += 'if errorlevel 1 exit /b 1'
$bountyCommands += 'exit /b 0'
$bountyCommandFile = Join-Path $bountyOutput 'run-routine-debug-view-tests.cmd'
[IO.File]::WriteAllLines($bountyCommandFile, $bountyCommands, [Text.Encoding]::Default)
& $env:ComSpec /d /c $bountyCommandFile
if ($LASTEXITCODE -ne 0) { throw 'Routine debug view tests failed.' }
