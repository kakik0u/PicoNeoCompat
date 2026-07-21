[CmdletBinding()]
param(
    [switch]$SkipSmokeTest
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repoRoot 'build'
$sourceDir = Join-Path $repoRoot 'src'
$includeDir = Join-Path $repoRoot 'include'

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw 'vswhere.exe was not found. Install Visual Studio 2022 or Build Tools with Desktop development with C++.'
}

$vsInstall = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsInstall) {
    throw 'A Visual C++ x64 toolchain was not found.'
}

$vcvars = Join-Path $vsInstall 'VC\Auxiliary\Build\vcvars64.bat'
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

$dll = Join-Path $buildDir 'nvEncCompat64.dll'
$smokeTest = Join-Path $buildDir 'smoke_test.exe'
$compileDll = 'call "{0}" && cl /nologo /std:c++17 /utf-8 /EHsc /O2 /W4 /wd4996 /DUNICODE /D_UNICODE /I"{1}" /LD "{2}" /link /DEF:"{3}" /OUT:"{4}"' -f `
    $vcvars, $includeDir, (Join-Path $sourceDir 'nvenc_compat.cpp'), `
    (Join-Path $sourceDir 'nvenc_compat.def'), $dll

cmd.exe /d /s /c $compileDll
if ($LASTEXITCODE -ne 0) { throw "DLL build failed with exit code $LASTEXITCODE" }

$compileTest = 'call "{0}" && cl /nologo /std:c++17 /utf-8 /EHsc /O2 /W4 /wd4996 /I"{1}" "{2}" /link /OUT:"{3}"' -f `
    $vcvars, $includeDir, (Join-Path $sourceDir 'smoke_test.cpp'), $smokeTest

cmd.exe /d /s /c $compileTest
if ($LASTEXITCODE -ne 0) { throw "Smoke-test build failed with exit code $LASTEXITCODE" }

if (-not $SkipSmokeTest) {
    Push-Location $repoRoot
    try {
        & $smokeTest
        if ($LASTEXITCODE -ne 0) { throw "Smoke test failed with exit code $LASTEXITCODE" }
    }
    finally {
        Pop-Location
    }
}
else {
    Write-Host 'Smoke-test execution skipped (the test executable was still built).'
}

Get-Item -LiteralPath $dll, $smokeTest | Select-Object FullName, Length, LastWriteTime
