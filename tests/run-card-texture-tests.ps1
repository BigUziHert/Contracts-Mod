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
$bountyHeader = @('#pragma once', '// Generated from production source; do not edit.', 'namespace Card {')
foreach ($bountyConstant in @('kCardCustomTexture', 'kTextureSettleMs')) {
    $bountyMatches = [regex]::Matches($bountyData, ('constexpr\s+\w+\s+' + $bountyConstant + '\s*=[^;]+;'))
    if ($bountyMatches.Count -ne 1) { throw "Expected exactly one production constant: $bountyConstant" }
    $bountyHeader += $bountyMatches[0].Value
}
$bountyHeader += '}'
# Function definitions have their closing brace at column zero; nested braces are indented.
# Requiring exactly one complete definition avoids matching the forward declarations.
foreach ($bountyFunction in @('ApplyCardCustomTexture', 'RefreshCardTextureAfterTransition', 'MaintainPortraitAndCard')) {
    $bountyPattern = '(?ms)^static void ' + $bountyFunction + '\(\)\s*\{.*?^\}'
    $bountyMatches = [regex]::Matches($bountySource, $bountyPattern)
    if ($bountyMatches.Count -ne 1) { throw "Expected exactly one production function: $bountyFunction" }
    $bountyLine = 1 + ([regex]::Matches($bountySource.Substring(0, $bountyMatches[0].Index), '\n')).Count
    $bountyHeader += '#line {0} "{1}"' -f $bountyLine, $bountySourcePath.Replace('\', '/')
    $bountyHeader += $bountyMatches[0].Value
}
$bountyExactHeader = $bountyHeader -join "`r`n"
[IO.File]::WriteAllText((Join-Path $bountyOutput 'card_texture_under_test.h'), $bountyExactHeader)

# A regression control changes only the settling guard back to one-shot behavior.
# The same test must reject it at the simulated delayed native material reset.
$bountySettlingGuard = 'if (Cd.customApplied && RuntimeNowMs() >= Cd.textureRefreshUntilMs) return;'
if (-not $bountyExactHeader.Contains($bountySettlingGuard)) {
    throw 'Production settling guard changed; update the one-shot regression control explicitly.'
}
$bountyOldHeader = $bountyExactHeader.Replace($bountySettlingGuard, 'if (Cd.customApplied) return;')
[IO.File]::WriteAllText((Join-Path $bountyOutput 'card_texture_old_under_test.h'), $bountyOldHeader)

$bountyCommands = @('@echo off', ('call "{0}" >nul' -f $bountyVcVars), 'if errorlevel 1 exit /b 1')
foreach ($bountyVariant in @('card_texture_tests', 'card_texture_old_control')) {
    $bountyExtra = if ($bountyVariant -eq 'card_texture_old_control') { '/DCARD_TEXTURE_OLD_ONESHOT' } else { '' }
    $bountyCommands += 'cl /nologo /std:c++20 /EHsc /W4 /WX /MT /Od {0} /I"{1}" /Fo"{2}" /Fe"{3}" "{4}"' -f `
        $bountyExtra, $bountyOutput, (Join-Path $bountyOutput ($bountyVariant + '.obj')), `
        (Join-Path $bountyOutput ($bountyVariant + '.exe')), (Join-Path $PSScriptRoot 'card_texture_tests.cpp')
    $bountyCommands += 'if errorlevel 1 exit /b 1'
}
$bountyCommands += '"' + (Join-Path $bountyOutput 'card_texture_tests.exe') + '"'
$bountyCommands += 'if errorlevel 1 exit /b 1'
$bountyControlLog = Join-Path $bountyOutput 'card_texture_old_control.log'
$bountyCommands += '"{0}" >"{1}" 2>&1' -f (Join-Path $bountyOutput 'card_texture_old_control.exe'), $bountyControlLog
$bountyCommands += 'if not errorlevel 1 exit /b 1'
$bountyCommands += 'exit /b 0'
$bountyCommandFile = Join-Path $bountyOutput 'run-card-texture-tests.cmd'
[IO.File]::WriteAllLines($bountyCommandFile, $bountyCommands, [Text.Encoding]::Default)
& $env:ComSpec /d /c $bountyCommandFile
if ($LASTEXITCODE -ne 0) { throw 'Card texture regression tests failed.' }
$bountyExpectedFailure = 'FAILED: same-handle native reset is repaired during startup maintenance'
if ([IO.File]::ReadAllText($bountyControlLog).Trim() -ne $bountyExpectedFailure) {
    throw 'One-shot control did not fail at the expected material-reset regression.'
}
Write-Host 'One-shot regression control rejected at the expected native material reset.'
