<#
.SYNOPSIS
    Assembles the redistributable zip.

.DESCRIPTION
    Stages release\ plus build\d3d9.dll and DishonoredBorderless.ini into
    dist\staging, writes checksums.txt, and zips it.

    The DLL is copied, never rebuilt. The bytes in build\d3d9.dll are the ones
    that were verified in the real game; rebuilding would produce different
    bytes with no evidence behind them. If you change the source, rebuild,
    re-test in the game, then re-package.

    tools\Check-Pe.ps1 gates the package on the three defects that make the
    game refuse to start with no error message at all -- a stranger should
    never be the one to discover those.

.PARAMETER Version
    Goes in the zip filename. Keep it in step with README.txt.

.EXAMPLE
    .\package.ps1
    .\package.ps1 -Version 1.0.1
#>
[CmdletBinding()]
param(
    [string]$Version = '1.0.0'
)

$ErrorActionPreference = 'Stop'
$root    = $PSScriptRoot
$dist    = Join-Path $root 'dist'
$staging = Join-Path $dist 'staging'
$dll     = Join-Path $root 'build\d3d9.dll'
$ini     = Join-Path $root 'DishonoredBorderless.ini'

if (-not (Test-Path $dll)) { throw "build\d3d9.dll is missing -- run .\build.ps1 first" }

# ---------------------------------------------------------------------------
# Gate: never ship a DLL that cannot load
# ---------------------------------------------------------------------------
Write-Host 'Checking the PE...' -ForegroundColor Cyan
& (Join-Path $root 'tools\Check-Pe.ps1') -Path $dll -ExpectMachine x86 `
    -RequireExports Direct3DCreate9, D3DPERF_BeginEvent, D3DPERF_EndEvent, D3DPERF_SetOptions `
    -RequireSystemImportsOnly | Out-Null
Write-Host '  ok' -ForegroundColor Green

# The README quotes the DLL's hash. If they disagree, the README is lying to
# exactly the people who bothered to check, which is worse than not quoting it.
$hash = (Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash.ToLowerInvariant()
$readmeSource = Join-Path $root 'release\README.txt'
if ((Get-Content -Raw $readmeSource) -notmatch [regex]::Escape($hash)) {
    Write-Host "  README.txt does not quote the current hash." -ForegroundColor Red
    Write-Host "  build\d3d9.dll is $hash" -ForegroundColor Red
    throw 'update the SHA-256 in release\README.txt'
}
Write-Host "  README hash matches ($($hash.Substring(0,16))...)" -ForegroundColor Green

# The "is this safe?" section points at the source. A dangling pointer there is
# worse than saying nothing -- it is the first thing a sceptical reader checks.
$placeholder = (Get-Content -Raw $readmeSource) -match '<fill in before posting'

# ---------------------------------------------------------------------------
# Stage
# ---------------------------------------------------------------------------
if (Test-Path $staging) { Remove-Item $staging -Recurse -Force }
New-Item -ItemType Directory -Force -Path $staging | Out-Null

Copy-Item $dll $staging
Copy-Item $ini $staging
Copy-Item (Join-Path $root 'release\*') $staging

# ---------------------------------------------------------------------------
# checksums.txt, in the format Get-FileHash and sha256sum both read
# ---------------------------------------------------------------------------
$lines = Get-ChildItem $staging -File | Sort-Object Name | ForEach-Object {
    '{0}  {1}' -f (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant(), $_.Name
}
Set-Content -LiteralPath (Join-Path $staging 'checksums.txt') -Value $lines -Encoding ASCII

# ---------------------------------------------------------------------------
# Zip
# ---------------------------------------------------------------------------
$zip = Join-Path $dist "DishonoredBorderless-$Version.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Join-Path $staging '*') -DestinationPath $zip -CompressionLevel Optimal

Write-Host ''
Write-Host "packaged $zip" -ForegroundColor Green
if ($placeholder) {
    Write-Host '  NOTE: README.txt still has the source-link placeholder in it.' -ForegroundColor Yellow
    Write-Host '        Fill it in and re-package before you post this anywhere.' -ForegroundColor Yellow
}
Get-ChildItem $staging -File | Sort-Object Name |
    ForEach-Object { '  {0,-28} {1,8:N0} bytes' -f $_.Name, $_.Length } | Write-Host
Write-Host ''
Write-Host ('  zip SHA-256: ' + (Get-FileHash $zip -Algorithm SHA256).Hash.ToLowerInvariant())

# ---------------------------------------------------------------------------
# Source zip -- a second file on the same mod page, so "the source is
# published alongside this" in README.txt stays true with no external hosting
# to rot. Everything needed to rebuild the DLL and re-verify it.
# ---------------------------------------------------------------------------
$srcStaging = Join-Path $dist 'staging-source'
if (Test-Path $srcStaging) { Remove-Item $srcStaging -Recurse -Force }
New-Item -ItemType Directory -Force -Path $srcStaging | Out-Null

foreach ($item in 'src', 'tests', 'tools', 'release') {
    Copy-Item (Join-Path $root $item) $srcStaging -Recurse
}
foreach ($file in 'build.ps1', 'install.ps1', 'package.ps1', 'DishonoredBorderless.ini',
                  'README.md', 'FINDINGS.md', 'plan.md') {
    Copy-Item (Join-Path $root $file) $srcStaging
}
Copy-Item (Join-Path $root 'release\LICENSE.txt') $srcStaging

$srcZip = Join-Path $dist "DishonoredBorderless-$Version-source.zip"
if (Test-Path $srcZip) { Remove-Item $srcZip -Force }
Compress-Archive -Path (Join-Path $srcStaging '*') -DestinationPath $srcZip -CompressionLevel Optimal

Write-Host ''
Write-Host "packaged $srcZip" -ForegroundColor Green
Write-Host ('  {0:N0} files, {1:N0} KB' -f (Get-ChildItem $srcStaging -Recurse -File).Count,
                                           ((Get-Item $srcZip).Length / 1KB))
Write-Host ('  zip SHA-256: ' + (Get-FileHash $srcZip -Algorithm SHA256).Hash.ToLowerInvariant())
