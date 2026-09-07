param(
    [string]$VisualStudio = 'C:\Program Files\Microsoft Visual Studio\2022\Community'
)

$ErrorActionPreference = 'Stop'
$bountyRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$bountyOutput = Join-Path $bountyRoot 'tmp\tests'
$bountyVcVars = Join-Path $VisualStudio 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path -LiteralPath $bountyVcVars)) { throw 'Visual Studio C++ tools not found.' }
New-Item -ItemType Directory -Path $bountyOutput -Force | Out-Null
$bountyDebugPath = Join-Path $bountyRoot 'rdr2 scripting environment\samples\Pools\routine_debug.h'
$bountyRuntimePath = Join-Path $bountyRoot 'rdr2 scripting environment\samples\Pools\routine_runtime.h'
$bountyScriptPath = Join-Path $bountyRoot 'rdr2 scripting environment\samples\Pools\script.cpp'
$bountyDebug = [IO.File]::ReadAllText($bountyDebugPath)
$bountyRuntime = [IO.File]::ReadAllText($bountyRuntimePath)
$bountyScript = [IO.File]::ReadAllText($bountyScriptPath)
$bountyHeader = @('#pragma once', '// Extracted production debug observers and integration; do not edit.')
foreach ($bountyName in @('routineDebugEnabled', 'routineDebugNextSampleMs', 'routineDebugLines')) {
    $bountyMatches = [regex]::Matches($bountyDebug, '(?m)^static [^\r\n]+\b' + $bountyName + '\s*(?:=|\{)[^\r\n]*;')
    if ($bountyMatches.Count -ne 1) { throw "Expected exactly one production debug global: $bountyName" }
    $bountyHeader += $bountyMatches[0].Value
}
foreach ($bountyName in @('RoutineTaskActive', 'ObserveRoutineDebugActivity', 'ObserveRoutineDebug', 'ToggleRoutineDebug', 'UpdateRoutineDebug')) {
    $bountySource = if ($bountyName -eq 'RoutineTaskActive') { $bountyRuntime } else { $bountyDebug }
    $bountyPath = if ($bountyName -eq 'RoutineTaskActive') { $bountyRuntimePath } else { $bountyDebugPath }
    $bountyMatches = [regex]::Matches($bountySource, '(?ms)^static [^\r\n]+\b' + $bountyName + '\([^\r\n]*\)\s*\{.*?^\}')
    if ($bountyMatches.Count -ne 1) { throw "Expected exactly one production debug function: $bountyName" }
    $bountyLine = 1 + ([regex]::Matches($bountySource.Substring(0, $bountyMatches[0].Index), '\n')).Count
    $bountyHeader += '#line {0} "{1}"' -f $bountyLine, $bountyPath.Replace('\', '/')
    $bountyHeader += $bountyMatches[0].Value
}
$bountyMain = [regex]::Matches($bountyScript, '(?ms)^void ScriptMain\(\)\s*\{.*?^\}')
if ($bountyMain.Count -ne 1) { throw 'Expected exactly one ScriptMain for debug integration.' }
$bountyMainText = $bountyMain[0].Value
$bountyToggle = [regex]::Matches($bountyMainText, '(?m)^\s*if \(IsKeyJustUp\(VK_F8\)\) ToggleRoutineDebug\(\);')
if ($bountyToggle.Count -ne 1) { throw 'Expected exactly one exclusive F8 release toggle in ScriptMain.' }
$bountyEarlyReturn = [regex]::Matches($bountyMainText, 'if \(!PlayerAvailable\(\) \|\| \(previousPlayer && previousPlayer != pedMe\)\)')
if ($bountyEarlyReturn.Count -ne 1 -or $bountyToggle[0].Index -ge $bountyEarlyReturn[0].Index) {
    throw 'Debug toggle must run before the first unavailable-player early return.'
}
$bountyTail = [regex]::Matches($bountyMainText, '(?s)UpdateRoutineDebug\(\);\s*UpdateCard\(\);[^\r\n]*\s*WAIT\(0\);\s*\}\s*\}$')
if ($bountyTail.Count -ne 1) { throw 'Debug rendering must precede the final protected UpdateCard/WAIT tail.' }
$bountyHeader += 'static void SampleProductionDebugKey() {'
$bountyHeader += $bountyToggle[0].Value
$bountyHeader += '}'
[IO.File]::WriteAllText((Join-Path $bountyOutput 'routine_debug_bridge_under_test.h'), ($bountyHeader -join "`r`n"))
$bountyCommands = @('@echo off', ('call "{0}" >nul' -f $bountyVcVars), 'if errorlevel 1 exit /b 1')
$bountyCommands += 'cl /nologo /std:c++20 /EHsc /W4 /WX /MT /Od /I"{0}" /Fo"{1}" /Fe"{2}" "{3}"' -f `
    $bountyOutput, (Join-Path $bountyOutput 'routine_debug_bridge_tests.obj'), (Join-Path $bountyOutput 'routine_debug_bridge_tests.exe'), `
    (Join-Path $PSScriptRoot 'routine_debug_bridge_tests.cpp')
$bountyCommands += 'if errorlevel 1 exit /b 1'
$bountyCommands += '"' + (Join-Path $bountyOutput 'routine_debug_bridge_tests.exe') + '"'
$bountyCommands += 'if errorlevel 1 exit /b 1'
$bountyCommands += 'exit /b 0'
$bountyCommandFile = Join-Path $bountyOutput 'run-routine-debug-bridge-tests.cmd'
[IO.File]::WriteAllLines($bountyCommandFile, $bountyCommands, [Text.Encoding]::Default)
& $env:ComSpec /d /c $bountyCommandFile
if ($LASTEXITCODE -ne 0) { throw 'Routine debug bridge regression tests failed.' }
