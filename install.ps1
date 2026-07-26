<#
.SYNOPSIS
    Copies the built proxy into Dishonored\Binaries\Win32, or removes it again.

.DESCRIPTION
    Finds the game through Steam's libraryfolders.vdf (app 205100) unless
    -GamePath is given. Nothing is overwritten without saying so, and an
    existing DishonoredBorderless.ini is left alone so your settings survive a
    re-install.

.EXAMPLE
    .\install.ps1
    .\install.ps1 -GamePath 'D:\Games\Dishonored'
    .\install.ps1 -Uninstall
#>
[CmdletBinding()]
param(
    [string]$GamePath,
    [switch]$Uninstall,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$root     = $PSScriptRoot
$buildDir = Join-Path $root 'build'

# Our DLL carries its own name as an ASCII literal (log lines, ini filename).
# Read the bytes directly: Select-String -Encoding Byte does not exist in
# Windows PowerShell 5.1, and it fails as a *parameter binding* error, which
# -ErrorAction cannot suppress -- so this branch killed the script outright.
function Test-IsOurDll([string]$path) {
    try {
        $bytes = [System.IO.File]::ReadAllBytes($path)
        return [System.Text.Encoding]::ASCII.GetString($bytes).Contains('DishonoredBorderless')
    } catch {
        return $false
    }
}

function Find-DishonoredPath {
    $steamRoot = (Get-ItemProperty 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue).SteamPath
    if (-not $steamRoot) { $steamRoot = 'C:\Program Files (x86)\Steam' }

    $libraries = @($steamRoot)
    $vdf = Join-Path $steamRoot 'steamapps\libraryfolders.vdf'
    if (Test-Path $vdf) {
        foreach ($line in Get-Content $vdf) {
            if ($line -match '"path"\s+"(.+?)"') { $libraries += $Matches[1].Replace('\\', '\') }
        }
    }

    foreach ($library in $libraries) {
        $candidate = Join-Path $library 'steamapps\common\Dishonored'
        if (Test-Path (Join-Path $candidate 'Binaries\Win32\Dishonored.exe')) { return $candidate }
    }
    return $null
}

if (-not $GamePath) {
    $GamePath = Find-DishonoredPath
    if (-not $GamePath) {
        throw 'Could not find Dishonored. Pass -GamePath "<...>\steamapps\common\Dishonored".'
    }
    Write-Host "Found Dishonored at $GamePath" -ForegroundColor Cyan
}

$targetDir = Join-Path $GamePath 'Binaries\Win32'
$gameExe   = Join-Path $targetDir 'Dishonored.exe'
if (-not (Test-Path $gameExe)) { throw "no Dishonored.exe under $targetDir" }

$targetDll = Join-Path $targetDir 'd3d9.dll'
$targetIni = Join-Path $targetDir 'DishonoredBorderless.ini'

if ($Uninstall) {
    foreach ($file in @($targetDll, $targetIni, (Join-Path $targetDir 'DishonoredBorderless.log'))) {
        if (Test-Path $file) {
            Remove-Item $file -Force
            Write-Host "removed $file" -ForegroundColor Yellow
        }
    }
    Write-Host 'Uninstalled.' -ForegroundColor Green
    return
}

$sourceDll = Join-Path $buildDir 'd3d9.dll'
$sourceIni = Join-Path $root 'DishonoredBorderless.ini'
if (-not (Test-Path $sourceDll)) { throw "build\d3d9.dll is missing -- run .\build.ps1 first" }

# A d3d9.dll already here is somebody else's wrapper (ReShade, dgVoodoo, ENB).
# Overwriting it silently would break whatever it does.
if ((Test-Path $targetDll) -and -not $Force) {
    $existing = Get-Item $targetDll
    if (-not (Test-IsOurDll $targetDll)) {
        Write-Host "A d3d9.dll is already installed ($([int]($existing.Length / 1KB)) KB, $($existing.LastWriteTime))." -ForegroundColor Yellow
        Write-Host 'It is probably ReShade, dgVoodoo or an ENB. Re-run with -Force to replace it.' -ForegroundColor Yellow
        throw 'refusing to overwrite an existing d3d9.dll'
    }
}

Copy-Item $sourceDll $targetDll -Force
Write-Host "installed $targetDll" -ForegroundColor Green

if (Test-Path $targetIni) {
    Write-Host "kept your existing $targetIni" -ForegroundColor DarkGray
} else {
    Copy-Item $sourceIni $targetIni -Force
    Write-Host "installed $targetIni" -ForegroundColor Green
}

Write-Host ''
Write-Host 'Launch the game normally. If it does not go borderless, read'
Write-Host "  $targetDir\DishonoredBorderless.log"
Write-Host 'and see README "Troubleshooting".'
