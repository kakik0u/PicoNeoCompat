[CmdletBinding()]
param(
    [string]$InstallRoot = "$env:LOCALAPPDATA\PicoNeo2NvencCompat",
    [switch]$RemovePatchedDriver
)

$ErrorActionPreference = 'Stop'
$statePath = Join-Path $InstallRoot 'install-state.json'
if (-not (Test-Path -LiteralPath $statePath)) { throw "Install state was not found: $statePath" }
if (Get-Process -Name vrserver, vrmonitor, vrcompositor -ErrorAction SilentlyContinue) {
    throw 'SteamVR is running. Exit SteamVR before uninstalling.'
}

$state = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
$vrPathReg = Join-Path $state.steamVrPath 'bin\win64\vrpathreg.exe'
if (-not (Test-Path -LiteralPath $vrPathReg)) { throw "vrpathreg.exe was not found: $vrPathReg" }

& $vrPathReg removedriver $state.installedDriver
& $vrPathReg adddriver $state.sourceDriver
if ($LASTEXITCODE -ne 0) { throw "vrpathreg failed with exit code $LASTEXITCODE" }

if (Test-Path -LiteralPath $state.steamVrShim) {
    $currentHash = (Get-FileHash -LiteralPath $state.steamVrShim -Algorithm SHA256).Hash
    if ($currentHash -eq $state.shimSha256) {
        Remove-Item -LiteralPath $state.steamVrShim -Force
    }
    else {
        Write-Warning 'The SteamVR shim has changed, so it was not removed.'
    }
}

if ($RemovePatchedDriver) {
    $root = [IO.Path]::GetFullPath($InstallRoot).TrimEnd('\') + '\'
    $driver = [IO.Path]::GetFullPath([string]$state.installedDriver)
    if (-not $driver.StartsWith($root, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Refusing to remove a driver outside InstallRoot.'
    }
    if (Test-Path -LiteralPath $driver) { Remove-Item -LiteralPath $driver -Recurse -Force }
}

Remove-Item -LiteralPath $statePath -Force
Write-Host 'Original PICO driver registration restored.' -ForegroundColor Green

