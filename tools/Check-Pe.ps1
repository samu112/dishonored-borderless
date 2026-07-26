<#
.SYNOPSIS
    Reads a PE file's machine type, export names and import DLL names, and
    optionally asserts them.

.DESCRIPTION
    Three things can silently break the proxy in ways that produce no error
    message at all -- the game just refuses to start:

      1. The DLL is built 64-bit. Dishonored.exe is 32-bit.
      2. The linker decorates the __stdcall exports ("Direct3DCreate9@4"), so
         the game's import of "Direct3DCreate9" resolves to nothing.
      3. The DLL imports a non-system runtime (libc++.dll, libwinpthread-1.dll)
         that is not sitting next to the game executable.

    build.ps1 runs this against build\d3d9.dll on every build.

.EXAMPLE
    .\tools\Check-Pe.ps1 -Path build\d3d9.dll -ExpectMachine x86 `
        -RequireExports Direct3DCreate9,D3DPERF_BeginEvent -RequireSystemImportsOnly
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Path,
    [ValidateSet('x86', 'x64', 'any')][string]$ExpectMachine = 'any',
    [string[]]$RequireExports = @(),
    [switch]$RequireSystemImportsOnly,
    [switch]$Quiet
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Path)) { throw "no such file: $Path" }
$bytes = [System.IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $Path))

function Get-U16($offset) { [BitConverter]::ToUInt16($bytes, $offset) }
function Get-U32($offset) { [BitConverter]::ToUInt32($bytes, $offset) }

function Get-AsciiZ($offset) {
    $end = $offset
    while ($end -lt $bytes.Length -and $bytes[$end] -ne 0) { $end++ }
    [System.Text.Encoding]::ASCII.GetString($bytes, $offset, $end - $offset)
}

if ((Get-U16 0) -ne 0x5A4D) { throw "$Path is not a PE file (no MZ signature)" }
$pe = Get-U32 0x3C
if ((Get-U32 $pe) -ne 0x00004550) { throw "$Path is not a PE file (no PE signature)" }

$machineWord = Get-U16 ($pe + 4)
$machine = switch ($machineWord) {
    0x014C { 'x86' }
    0x8664 { 'x64' }
    0xAA64 { 'arm64' }
    default { ('0x{0:X4}' -f $machineWord) }
}

$sectionCount   = Get-U16 ($pe + 6)
$optionalSize   = Get-U16 ($pe + 20)
$optionalOffset = $pe + 24
$optionalMagic  = Get-U16 $optionalOffset
$dataDirOffset  = $optionalOffset + $(if ($optionalMagic -eq 0x20B) { 112 } else { 96 })

$exportRva = Get-U32 $dataDirOffset
$importRva = Get-U32 ($dataDirOffset + 8)

$sections = @()
$sectionBase = $optionalOffset + $optionalSize
for ($i = 0; $i -lt $sectionCount; $i++) {
    $o = $sectionBase + ($i * 40)
    $sections += [pscustomobject]@{
        VirtualSize    = Get-U32 ($o + 8)
        VirtualAddress = Get-U32 ($o + 12)
        RawSize        = Get-U32 ($o + 16)
        RawAddress     = Get-U32 ($o + 20)
    }
}

function ConvertTo-Offset($rva) {
    foreach ($s in $sections) {
        $span = [Math]::Max($s.VirtualSize, $s.RawSize)
        if ($rva -ge $s.VirtualAddress -and $rva -lt ($s.VirtualAddress + $span)) {
            return $s.RawAddress + ($rva - $s.VirtualAddress)
        }
    }
    return -1
}

$exports = @()
if ($exportRva -ne 0) {
    $dir = ConvertTo-Offset $exportRva
    if ($dir -ge 0) {
        $nameCount   = Get-U32 ($dir + 24)
        $namesRva    = Get-U32 ($dir + 32)
        $namesOffset = ConvertTo-Offset $namesRva
        for ($i = 0; $i -lt $nameCount; $i++) {
            $nameRva = Get-U32 ($namesOffset + ($i * 4))
            $exports += Get-AsciiZ (ConvertTo-Offset $nameRva)
        }
    }
}

$imports = @()
if ($importRva -ne 0) {
    $o = ConvertTo-Offset $importRva
    while ($o -ge 0) {
        $nameRva = Get-U32 ($o + 12)
        if ($nameRva -eq 0) { break }
        $imports += Get-AsciiZ (ConvertTo-Offset $nameRva)
        $o += 20
    }
}

$result = [pscustomobject]@{
    Path    = (Resolve-Path -LiteralPath $Path).Path
    Machine = $machine
    Exports = $exports
    Imports = $imports
}

if (-not $Quiet) {
    Write-Host "  machine : $machine"
    Write-Host "  exports : $($exports -join ', ')"
    Write-Host "  imports : $($imports -join ', ')"
}

$failures = @()

if ($ExpectMachine -ne 'any' -and $machine -ne $ExpectMachine) {
    $failures += "machine is $machine, expected $ExpectMachine"
}

foreach ($required in $RequireExports) {
    if ($exports -notcontains $required) {
        $decorated = $exports | Where-Object { $_ -like "*$required*" }
        $hint = if ($decorated) { " (found '$($decorated -join "', '")' -- name decoration, check --kill-at and src\d3d9.def)" } else { '' }
        $failures += "missing export '$required'$hint"
    }
}

if ($RequireSystemImportsOnly) {
    # Everything here ships with Windows itself.
    $systemImports = @(
        'kernel32.dll', 'user32.dll', 'gdi32.dll', 'advapi32.dll', 'shell32.dll',
        'ole32.dll', 'oleaut32.dll', 'shlwapi.dll', 'msvcrt.dll', 'ucrtbase.dll',
        'd3d9.dll', 'dwmapi.dll', 'shcore.dll', 'version.dll', 'winmm.dll'
    )
    foreach ($import in $imports) {
        $lower = $import.ToLowerInvariant()
        if ($systemImports -contains $lower) { continue }
        if ($lower -like 'api-ms-win-*') { continue }
        if ($lower -like 'ext-ms-win-*') { continue }
        $failures += "imports non-system DLL '$import' -- it would have to ship beside the game exe"
    }
}

if ($failures.Count -gt 0) {
    foreach ($failure in $failures) { Write-Host "  FAIL: $failure" -ForegroundColor Red }
    throw "PE checks failed for $Path"
}

$result
