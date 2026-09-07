param([string]$VisualStudio = 'C:\Program Files\Microsoft Visual Studio\2022\Community')

$ErrorActionPreference = 'Stop'
$bountyRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$bountyOutput = Join-Path $bountyRoot 'tmp\tests'
$bountySourcePath = Join-Path $bountyRoot 'rdr2 scripting environment\samples\Pools\script.cpp'
$bountyVcVars = Join-Path $VisualStudio 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path -LiteralPath $bountyVcVars)) { throw 'Visual Studio C++ tools not found; pass -VisualStudio.' }
New-Item -ItemType Directory -Path $bountyOutput -Force | Out-Null
$bountySource = [IO.File]::ReadAllText($bountySourcePath)
$bountyHeader = @('#pragma once', '// Extracted production search-area behavior; do not edit.')
foreach ($bountyPattern in @(
    '(?ms)^static void AddSearchBlip\(\)\s*\{.*?^\}',
    '(?ms)^static void UpdateSearchArea\(\)\s*\{.*?^\}'
)) {
    $bountyMatches = [regex]::Matches($bountySource, $bountyPattern)
    if ($bountyMatches.Count -ne 1) { throw "Expected exactly one production search function matching: $bountyPattern" }
    $bountyLine = 1 + ([regex]::Matches($bountySource.Substring(0, $bountyMatches[0].Index), '\n')).Count
    $bountyHeader += '#line {0} "{1}"' -f $bountyLine, $bountySourcePath.Replace('\', '/')
    $bountyHeader += $bountyMatches[0].Value
}
$bountyMain = [regex]::Matches($bountySource, '(?ms)^void ScriptMain\(\)\s*\{.*?^\}')
if ($bountyMain.Count -ne 1) { throw 'Expected exactly one ScriptMain for search-area integration.' }
$bountyTail = [regex]::Matches($bountyMain[0].Value,
    '(?s)(?<calls>UpdateSearchArea\(\);\s*TraceCardInspection\(\);\s*UpdateRoutineDebug\(\);\s*UpdateCard\(\);[^\r\n]*\s*WAIT\(0\);)\s*\}\s*\}$')
if ($bountyTail.Count -ne 1) { throw 'Search-area update must precede inspection trace and the protected debug/card/WAIT tail.' }
if ([regex]::Matches($bountyMain[0].Value, 'UpdateSearchArea\(\);').Count -ne 1) {
    throw 'Expected exactly one search-area update in ScriptMain.'
}
$bountyHeader += 'static void RunProductionSearchFrameTail() {'
$bountyHeader += $bountyTail[0].Groups['calls'].Value
$bountyHeader += '}'
[IO.File]::WriteAllText((Join-Path $bountyOutput 'search_area_under_test.h'), ($bountyHeader -join "`r`n"))
$bountyCommands = @('@echo off', ('call "{0}" >nul' -f $bountyVcVars), 'if errorlevel 1 exit /b 1')
$bountyCommands += 'cl /nologo /std:c++20 /EHsc /W4 /WX /MT /Od /I"{0}" /Fo"{1}" /Fe"{2}" "{3}"' -f `
    $bountyOutput, (Join-Path $bountyOutput 'search_area_tests.obj'), (Join-Path $bountyOutput 'search_area_tests.exe'), `
    (Join-Path $PSScriptRoot 'search_area_tests.cpp')
$bountyCommands += 'if errorlevel 1 exit /b 1'
$bountyCommands += '"' + (Join-Path $bountyOutput 'search_area_tests.exe') + '"'
$bountyCommands += 'if errorlevel 1 exit /b 1'
$bountyCommands += 'exit /b 0'
$bountyCommandFile = Join-Path $bountyOutput 'run-search-area-tests.cmd'
[IO.File]::WriteAllLines($bountyCommandFile, $bountyCommands, [Text.Encoding]::Default)
& $env:ComSpec /d /c $bountyCommandFile
if ($LASTEXITCODE -ne 0) { throw 'Search-area regression tests failed.' }
