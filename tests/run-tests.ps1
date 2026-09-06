param(
    [string]$VisualStudio = 'C:\Program Files\Microsoft Visual Studio\2022\Community'
)

$ErrorActionPreference = 'Stop'
$bountyRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$bountyOutput = Join-Path $bountyRoot 'tmp\tests'
$bountyVcVars = Join-Path $VisualStudio 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path -LiteralPath $bountyVcVars)) {
    throw 'Visual Studio C++ tools not found; pass -VisualStudio with your installation directory.'
}
New-Item -ItemType Directory -Path $bountyOutput -Force | Out-Null
$bountyCommands = @('@echo off', ('call "{0}" >nul' -f $bountyVcVars), 'if errorlevel 1 exit /b 1')
foreach ($bountyTest in @('target_ai_logic_tests', 'handoff_logic_tests', 'keyboard_tests')) {
    $bountySource = Join-Path $PSScriptRoot ($bountyTest + '.cpp')
    $bountyExe = Join-Path $bountyOutput ($bountyTest + '.exe')
    $bountyObj = Join-Path $bountyOutput ($bountyTest + '.obj')
    $bountyExtra = ''
    if ($bountyTest -eq 'keyboard_tests') {
        $bountyExtra = '/wd4100 /I"' + (Join-Path $PSScriptRoot 'keyboard_winapi_stub') + '"'
    }
    $bountyCommands += 'cl /nologo /std:c++20 /EHsc /W4 /WX /MT /Od {0} /Fo"{1}" /Fe"{2}" "{3}"' -f $bountyExtra, $bountyObj, $bountyExe, $bountySource
    $bountyCommands += 'if errorlevel 1 exit /b 1'
    $bountyCommands += '"' + $bountyExe + '"'
    $bountyCommands += 'if errorlevel 1 exit /b 1'
}
$bountyCommands += 'exit /b 0'
$bountyCommandFile = Join-Path $bountyOutput 'run-tests.cmd'
[IO.File]::WriteAllLines($bountyCommandFile, $bountyCommands, [Text.Encoding]::Default)
& $env:ComSpec /d /c $bountyCommandFile
if ($LASTEXITCODE -ne 0) { throw 'Regression tests failed.' }
& (Join-Path $PSScriptRoot 'run-card-texture-tests.ps1') -VisualStudio $VisualStudio
& (Join-Path $PSScriptRoot 'run-spawn-tests.ps1') -VisualStudio $VisualStudio
& (Join-Path $PSScriptRoot 'run-portrait-start-tests.ps1') -VisualStudio $VisualStudio
