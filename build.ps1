<#
.SYNOPSIS
    Builds the 32-bit d3d9 proxy and the D3D9 test app, then verifies the
    resulting PE.

.DESCRIPTION
    Dishonored.exe is 32-bit (PE machine 0x14C), so the proxy must be 32-bit
    too. A 64-bit mingw with no 32-bit runtime cannot produce it -- see README
    'Toolchain' for what to install.

.PARAMETER Clean
    Remove the build directory first.

.PARAMETER SkipTestApp
    Build only the proxy DLL.

.EXAMPLE
    .\build.ps1
    .\tests\Verify-Borderless.ps1
#>
[CmdletBinding()]
param(
    [switch]$Clean,
    [switch]$SkipTestApp
)

$ErrorActionPreference = 'Stop'
$root     = $PSScriptRoot
$buildDir = Join-Path $root 'build'
$testDir  = Join-Path $buildDir 'tests'

if ($Clean -and (Test-Path $buildDir)) { Remove-Item $buildDir -Recurse -Force }
New-Item -ItemType Directory -Force -Path $buildDir, $testDir | Out-Null

# ---------------------------------------------------------------------------
# Toolchain
# ---------------------------------------------------------------------------

function Find-Tool([string]$name) { (Get-Command $name -ErrorAction SilentlyContinue).Source }

$toolchain = $null

foreach ($candidate in 'i686-w64-mingw32-clang++', 'i686-w64-mingw32-g++', 'i686-w64-mingw32-c++') {
    $path = Find-Tool $candidate
    if ($path) { $toolchain = @{ Kind = 'gnu'; Exe = $path; ExtraFlags = @() }; break }
}

if (-not $toolchain) {
    # A 64-bit clang can cross-compile if its mingw sysroot has 32-bit runtimes.
    $clang = Find-Tool 'clang++'
    if ($clang) { $toolchain = @{ Kind = 'gnu'; Exe = $clang; ExtraFlags = @('--target=i686-w64-windows-gnu') } }
}

if (-not $toolchain) {
    $gpp = Find-Tool 'g++'
    if ($gpp) {
        # -m32 only works on a multilib build; most mingw-w64 packages are not.
        & $gpp -m32 -E -x c++ - 2>&1 | Out-Null
        $probe = Join-Path ([System.IO.Path]::GetTempPath()) 'dbl_probe.cpp'
        Set-Content -LiteralPath $probe -Value 'int main(){return 0;}'
        & $gpp -m32 $probe -o (Join-Path ([System.IO.Path]::GetTempPath()) 'dbl_probe.exe') 2>&1 | Out-Null
        $multilib = $LASTEXITCODE -eq 0
        Remove-Item $probe -ErrorAction SilentlyContinue
        if ($multilib) { $toolchain = @{ Kind = 'gnu'; Exe = $gpp; ExtraFlags = @('-m32') } }
    }
}

if (-not $toolchain) {
    $cl = Find-Tool 'cl'
    if ($cl) { $toolchain = @{ Kind = 'msvc'; Exe = $cl; ExtraFlags = @() } }
}

if (-not $toolchain) {
    Write-Host @'
No 32-bit C++ toolchain found.

Dishonored.exe is 32-bit, so d3d9.dll must be 32-bit as well. A 64-bit-only
mingw-w64 (no 32-bit runtime libraries) cannot build it.

Install one of these, then re-run:

  scoop install mingw-mstorsjo-llvm-ucrt     # llvm-mingw, targets i686 and x86_64
  scoop install mingw-winlibs -a 32bit       # gcc, i686 only (shadows an existing g++ shim)
  winget install Microsoft.VisualStudio.2022.BuildTools --override "--add Microsoft.VisualStudio.Workload.VCTools"

For MSVC, run this script from an "x86 Native Tools Command Prompt" so cl.exe
targets 32-bit.
'@ -ForegroundColor Yellow
    throw 'no 32-bit toolchain'
}

Write-Host "Toolchain: $($toolchain.Exe) [$($toolchain.Kind)] $($toolchain.ExtraFlags -join ' ')" -ForegroundColor Cyan

# ---------------------------------------------------------------------------
# Compile
# ---------------------------------------------------------------------------

$sources = @(
    Join-Path $root 'src\dllmain.cpp'
    Join-Path $root 'src\d3d9_proxy.cpp'
    Join-Path $root 'src\hooks_d3d9.cpp'
    Join-Path $root 'src\window.cpp'
    Join-Path $root 'src\config.cpp'
    Join-Path $root 'src\log.cpp'
)
$defFile = Join-Path $root 'src\d3d9.def'
$dllPath = Join-Path $buildDir 'd3d9.dll'
$exePath = Join-Path $testDir 'd3d9_testapp.exe'
$testSrc = Join-Path $root 'tests\d3d9_testapp.cpp'

function Invoke-Compiler([string[]]$compilerArgs, [string]$what) {
    Write-Host "Building $what..." -ForegroundColor Cyan
    & $toolchain.Exe @compilerArgs 2>&1 | ForEach-Object { Write-Host "  $_" }
    if ($LASTEXITCODE -ne 0) { throw "$what failed (exit $LASTEXITCODE)" }
}

if ($toolchain.Kind -eq 'gnu') {
    # -fno-exceptions/-fno-rtti and -static keep libc++/libunwind/libwinpthread
    # out of the import table: a DLL that needs a runtime the game directory
    # does not have simply fails to load, with no error shown.
    $common = $toolchain.ExtraFlags + @(
        '-std=c++11', '-O2', '-Wall', '-Wextra', '-fno-exceptions', '-fno-rtti',
        '-DWIN32_LEAN_AND_MEAN', '-DNOMINMAX'
    )

    Invoke-Compiler ($common + @(
        '-shared', '-static', '-static-libgcc', '-static-libstdc++',
        '-o', $dllPath) + $sources + @($defFile,
        '-Wl,--kill-at',    # undecorated __stdcall export names
        '-luser32', '-lkernel32')) 'd3d9.dll'

    if (-not $SkipTestApp) {
        Invoke-Compiler ($common + @(
            '-static', '-static-libgcc', '-static-libstdc++',
            '-o', $exePath, $testSrc,
            '-ld3d9', '-luser32', '-lgdi32')) 'd3d9_testapp.exe'
    }
} else {
    # Alternate path for a Visual Studio install. Must be an x86 command prompt.
    $objDir = Join-Path $buildDir 'obj'
    New-Item -ItemType Directory -Force -Path $objDir | Out-Null
    Invoke-Compiler (@(
        '/nologo', '/O2', '/MT', '/EHs-c-', '/GR-', '/W3',
        '/DWIN32_LEAN_AND_MEAN', '/DNOMINMAX', "/Fo$objDir\", '/LD') + $sources + @(
        '/link', "/DEF:$defFile", "/OUT:$dllPath", 'user32.lib', 'kernel32.lib')) 'd3d9.dll'

    if (-not $SkipTestApp) {
        Invoke-Compiler @(
            '/nologo', '/O2', '/MT', '/W3', '/DWIN32_LEAN_AND_MEAN', "/Fo$objDir\",
            $testSrc, '/link', "/OUT:$exePath", 'd3d9.lib', 'user32.lib', 'gdi32.lib') 'd3d9_testapp.exe'
    }
}

Copy-Item (Join-Path $root 'DishonoredBorderless.ini') $buildDir -Force

# ---------------------------------------------------------------------------
# Verify the binary we just produced
# ---------------------------------------------------------------------------

Write-Host ''
Write-Host 'Checking build\d3d9.dll...' -ForegroundColor Cyan
& (Join-Path $root 'tools\Check-Pe.ps1') -Path $dllPath -ExpectMachine x86 `
    -RequireExports Direct3DCreate9, D3DPERF_BeginEvent, D3DPERF_EndEvent, D3DPERF_SetOptions `
    -RequireSystemImportsOnly | Out-Null

if (-not $SkipTestApp) {
    Write-Host ''
    Write-Host 'Checking build\tests\d3d9_testapp.exe...' -ForegroundColor Cyan
    & (Join-Path $root 'tools\Check-Pe.ps1') -Path $exePath -ExpectMachine x86 | Out-Null
}

Write-Host ''
Write-Host 'Build OK.' -ForegroundColor Green
Write-Host '  next: .\tests\Verify-Borderless.ps1     (runs the proxy against the test app)'
Write-Host '        .\install.ps1                     (copies it into the game)'
