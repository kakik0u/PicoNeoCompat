[CmdletBinding()]
param(
    [string]$PicoDriverPath,

    [string]$SteamVrPath,

    [string]$InstallRoot = "$env:LOCALAPPDATA\PicoNeo2NvencCompat",

    [switch]$Force
)

$ErrorActionPreference = 'Stop'

function Assert-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Run PowerShell as Administrator. SteamVR''s program directory must be updated.'
    }
}

function Find-BytePattern {
    param([byte[]]$Data, [byte[]]$Pattern)
    $matches = [Collections.Generic.List[int]]::new()
    for ($i = 0; $i -le $Data.Length - $Pattern.Length; $i++) {
        $same = $true
        for ($j = 0; $j -lt $Pattern.Length; $j++) {
            if ($Data[$i + $j] -ne $Pattern[$j]) { $same = $false; break }
        }
        if ($same) { $matches.Add($i) }
    }
    return $matches.ToArray()
}

function Find-PicoDriver {
    $candidates = [Collections.Generic.List[string]]::new()
    foreach ($drive in Get-PSDrive -PSProvider FileSystem) {
        $root = $drive.Root
        $candidates.Add((Join-Path $root 'Program Files (x86)\Streaming Assistant\driver'))
        $candidates.Add((Join-Path $root 'SteamLibrary\steamapps\common\PicoLink\driver'))
        $candidates.Add((Join-Path $root 'Program Files (x86)\Steam\steamapps\common\PicoLink\driver'))
    }
    foreach ($candidate in $candidates) {
        if ((Test-Path -LiteralPath (Join-Path $candidate 'driver.vrdrivermanifest')) -and
            (Test-Path -LiteralPath (Join-Path $candidate 'bin\win64\VEncPlugin.dll'))) {
            return $candidate
        }
    }
    return $null
}

function Find-SteamVr {
    $candidates = [Collections.Generic.List[string]]::new()
    $steamRegistry = Get-ItemProperty -LiteralPath 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue
    if ($steamRegistry.SteamPath) {
        $candidates.Add((Join-Path ([string]$steamRegistry.SteamPath) 'steamapps\common\SteamVR'))
    }
    foreach ($drive in Get-PSDrive -PSProvider FileSystem) {
        $root = $drive.Root
        $candidates.Add((Join-Path $root 'Program Files (x86)\Steam\steamapps\common\SteamVR'))
        $candidates.Add((Join-Path $root 'SteamLibrary\steamapps\common\SteamVR'))
    }
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath (Join-Path $candidate 'bin\win64\vrpathreg.exe')) {
            return $candidate
        }
    }
    return $null
}

Assert-Administrator

if (Get-Process -Name vrserver, vrmonitor, vrcompositor -ErrorAction SilentlyContinue) {
    throw 'SteamVR is running. Exit SteamVR before installing.'
}

if (-not $PicoDriverPath) {
    $PicoDriverPath = Find-PicoDriver
    if (-not $PicoDriverPath) {
        $PicoDriverPath = Read-Host 'PICO driver folder (the folder containing driver.vrdrivermanifest)'
    }
}
if (-not $SteamVrPath) {
    $SteamVrPath = Find-SteamVr
    if (-not $SteamVrPath) {
        $SteamVrPath = Read-Host 'SteamVR folder'
    }
}

Write-Host "PICO driver: $PicoDriverPath"
Write-Host "SteamVR:     $SteamVrPath"

$repoRoot = Split-Path -Parent $PSScriptRoot
$shimSource = Join-Path $repoRoot 'build\nvEncCompat64.dll'
$configSource = Join-Path $repoRoot 'nvenc_compat.ini'
$vrPathReg = Join-Path $SteamVrPath 'bin\win64\vrpathreg.exe'
$steamVrBin = Join-Path $SteamVrPath 'bin\win64'
$sourceDriver = [IO.Path]::GetFullPath($PicoDriverPath)
$targetDriver = [IO.Path]::GetFullPath((Join-Path $InstallRoot 'driver'))
$targetRoot = [IO.Path]::GetFullPath($InstallRoot).TrimEnd('\') + '\'
$statePath = Join-Path $InstallRoot 'install-state.json'

if (-not $targetDriver.StartsWith($targetRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The patched driver path escaped InstallRoot.'
}
if (-not (Test-Path -LiteralPath (Join-Path $sourceDriver 'driver.vrdrivermanifest'))) {
    throw "Not a PICO OpenVR driver directory: $sourceDriver"
}
if (-not (Test-Path -LiteralPath $shimSource)) {
    throw 'build\nvEncCompat64.dll is missing. Download the prebuilt release package, or run scripts\build.ps1.'
}
if (-not (Test-Path -LiteralPath $configSource)) {
    throw 'nvenc_compat.ini is missing.'
}
if (-not (Test-Path -LiteralPath $vrPathReg)) {
    throw "SteamVR was not found at: $SteamVrPath"
}

if (Test-Path -LiteralPath $targetDriver) {
    if (-not $Force) { throw "Patched driver already exists: $targetDriver (use -Force to replace it)" }
    Remove-Item -LiteralPath $targetDriver -Recurse -Force
}

New-Item -ItemType Directory -Force -Path $InstallRoot | Out-Null
Copy-Item -LiteralPath $sourceDriver -Destination $targetDriver -Recurse

$plugin = Join-Path $targetDriver 'bin\win64\VEncPlugin.dll'
if (-not (Test-Path -LiteralPath $plugin)) { throw "VEncPlugin.dll was not found: $plugin" }
Copy-Item -LiteralPath $plugin -Destination "$plugin.original" -Force

$oldName = [Text.Encoding]::Unicode.GetBytes('nvEncodeAPI64.dll')
$newName = [Text.Encoding]::Unicode.GetBytes('nvEncCompat64.dll')
if ($oldName.Length -ne $newName.Length) { throw 'Internal error: DLL names differ in length.' }

$pluginBytes = [IO.File]::ReadAllBytes($plugin)
$positions = @(Find-BytePattern -Data $pluginBytes -Pattern $oldName)
if ($positions.Count -ne 1) {
    throw "Expected exactly one UTF-16 nvEncodeAPI64.dll reference, found $($positions.Count). This PICO build is not supported."
}
[Array]::Copy($newName, 0, $pluginBytes, $positions[0], $newName.Length)
[IO.File]::WriteAllBytes($plugin, $pluginBytes)

$ini = Join-Path $targetDriver 'bin\win64\RVRPlugin.ini'
if (Test-Path -LiteralPath $ini) {
    $iniText = [IO.File]::ReadAllText($ini)
    if ($iniText -match '(?m)^\s*HEVC\s*=') {
        $iniText = [Text.RegularExpressions.Regex]::Replace($iniText, '(?m)^\s*HEVC\s*=.*$', 'HEVC=0')
    }
    else {
        $iniText += "`r`nHEVC=0`r`n"
    }
    [IO.File]::WriteAllText($ini, $iniText, [Text.Encoding]::ASCII)
}

$driverShim = Join-Path $targetDriver 'bin\win64\nvEncCompat64.dll'
$steamVrShim = Join-Path $steamVrBin 'nvEncCompat64.dll'
$driverConfig = Join-Path $targetDriver 'bin\win64\nvenc_compat.ini'
$steamVrConfig = Join-Path $steamVrBin 'nvenc_compat.ini'
if ((Test-Path -LiteralPath $steamVrShim) -and -not $Force) {
    throw "A file already exists at $steamVrShim. Use -Force only if it came from this project."
}
Copy-Item -LiteralPath $shimSource -Destination $driverShim -Force
Copy-Item -LiteralPath $shimSource -Destination $steamVrShim -Force
Copy-Item -LiteralPath $configSource -Destination $driverConfig -Force
Copy-Item -LiteralPath $configSource -Destination $steamVrConfig -Force
$shimHash = (Get-FileHash -LiteralPath $steamVrShim -Algorithm SHA256).Hash
$configHash = (Get-FileHash -LiteralPath $steamVrConfig -Algorithm SHA256).Hash

& $vrPathReg removedriver $sourceDriver
& $vrPathReg removedriver $targetDriver
& $vrPathReg adddriver $targetDriver
if ($LASTEXITCODE -ne 0) { throw "vrpathreg failed with exit code $LASTEXITCODE" }

$state = [ordered]@{
    sourceDriver = $sourceDriver
    installedDriver = $targetDriver
    steamVrPath = [IO.Path]::GetFullPath($SteamVrPath)
    steamVrShim = $steamVrShim
    shimSha256 = $shimHash
    steamVrConfig = $steamVrConfig
    configSha256 = $configHash
    installedAt = (Get-Date).ToString('o')
}
$state | ConvertTo-Json | Set-Content -LiteralPath $statePath -Encoding UTF8

Write-Host ''
Write-Host 'Installation complete.' -ForegroundColor Green
Write-Host "Patched driver: $targetDriver"
Write-Host "State file:     $statePath"
Write-Host 'Start SteamVR. If SteamVR blocks pico after a previous crash, open Settings > Startup/Shutdown > Manage Add-ons and unblock it once.'
