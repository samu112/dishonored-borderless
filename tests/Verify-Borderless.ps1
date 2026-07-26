<#
.SYNOPSIS
    Runs the D3D9 test app with the proxy beside it and asserts, from a separate
    process, that the window really came up borderless and desktop-sized.

.DESCRIPTION
    plan.md 4.5.1 and the part of 4.5.2 that does not need the real game. Two
    scenarios run:

      1. Defaults: fill the monitor the app opens on.
      2. TargetMonitorIndex=2 at an explicit 1600x900, which exercises monitor
         selection and the centring arithmetic against a non-zero monitor
         origin. Skipped on a single-monitor setup.

    Each scenario exercises the whole chain: export names resolve, the DLL
    loads, CreateDevice is rewritten from exclusive-fullscreen to windowed, the
    WndProc subclass installs, DPI awareness is applied, a mid-run Reset back to
    exclusive fullscreen is turned around too, and the geometry is still right
    when the app exits.

    The test app briefly covers a display with a blue window. That is expected.

.EXAMPLE
    .\tests\Verify-Borderless.ps1
    .\tests\Verify-Borderless.ps1 -SkipMultiMonitor
#>
[CmdletBinding()]
param(
    [string]$BuildDir = (Join-Path $PSScriptRoot '..\build'),
    [int]$DurationMs = 4500,
    [switch]$SkipMultiMonitor
)

$ErrorActionPreference = 'Stop'

# This script compares physical pixel coordinates. A DPI-unaware process is fed
# virtualised numbers by Windows (2194x1234 instead of 3840x2160 at 175%), which
# would make every geometry assertion below quietly meaningless.
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

public static class Native
{
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }

    [StructLayout(LayoutKind.Sequential)]
    public struct MONITORINFO { public int cbSize; public RECT rcMonitor; public RECT rcWork; public int dwFlags; }

    private delegate bool MonitorEnumProc(IntPtr monitor, IntPtr dc, IntPtr rect, IntPtr data);

    [DllImport("user32.dll")] private static extern bool EnumDisplayMonitors(IntPtr dc, IntPtr clip, MonitorEnumProc callback, IntPtr data);
    [DllImport("user32.dll")] private static extern bool GetMonitorInfo(IntPtr monitor, ref MONITORINFO info);

    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr context);
    [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out RECT rect);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr window, out RECT rect);
    [DllImport("user32.dll")] public static extern uint GetWindowLong(IntPtr window, int index);
    [DllImport("user32.dll")] public static extern IntPtr MonitorFromWindow(IntPtr window, uint flags);

    // Same API and same order the DLL uses for TargetMonitorIndex, so index N
    // here is index N there.
    public static RECT[] Monitors()
    {
        List<RECT> found = new List<RECT>();
        EnumDisplayMonitors(IntPtr.Zero, IntPtr.Zero, delegate(IntPtr monitor, IntPtr dc, IntPtr rect, IntPtr data) {
            MONITORINFO info = new MONITORINFO();
            info.cbSize = Marshal.SizeOf(typeof(MONITORINFO));
            if (GetMonitorInfo(monitor, ref info)) found.Add(info.rcMonitor);
            return true;
        }, IntPtr.Zero);
        return found.ToArray();
    }

    public static RECT MonitorOf(IntPtr window)
    {
        MONITORINFO info = new MONITORINFO();
        info.cbSize = Marshal.SizeOf(typeof(MONITORINFO));
        GetMonitorInfo(MonitorFromWindow(window, 2 /* MONITOR_DEFAULTTONEAREST */), ref info);
        return info.rcMonitor;
    }
}
'@

try { [void][Native]::SetProcessDpiAwarenessContext([IntPtr]::new(-4)) } catch { }
try { [void][Native]::SetProcessDPIAware() } catch { }

# 0x80000000 does not fit in PowerShell's default Int32 literal, hence the L.
$WS_POPUP      = [uint32]0x80000000L
$WS_CAPTION    = [uint32]0x00C00000
$WS_THICKFRAME = [uint32]0x00040000

$script:failures = @()
$script:checks   = 0

function Assert-That([string]$name, [bool]$condition, [string]$detail) {
    $script:checks++
    if ($condition) {
        Write-Host ("  [ok]   {0}" -f $name) -ForegroundColor Green
        if ($detail) { Write-Host ("         {0}" -f $detail) -ForegroundColor DarkGray }
    } else {
        Write-Host ("  [FAIL] {0}" -f $name) -ForegroundColor Red
        if ($detail) { Write-Host ("         {0}" -f $detail) -ForegroundColor Red }
        $script:failures += $name
    }
}

$BuildDir = (Resolve-Path -LiteralPath $BuildDir).Path
$testDir  = Join-Path $BuildDir 'tests'
$exe      = Join-Path $testDir 'd3d9_testapp.exe'
$proxy    = Join-Path $BuildDir 'd3d9.dll'
$iniSource = Join-Path $PSScriptRoot '..\DishonoredBorderless.ini'

if (-not (Test-Path $exe))   { throw "test app not built: $exe (run .\build.ps1)" }
if (-not (Test-Path $proxy)) { throw "proxy not built: $proxy (run .\build.ps1)" }

# The proxy has to be in the test app's own directory, exactly as it will be in
# the game's Binaries\Win32.
Copy-Item $proxy $testDir -Force

$stateFile = Join-Path $testDir 'state.txt'
$logFile   = Join-Path $testDir 'DishonoredBorderless.log'
$stdout    = Join-Path $testDir 'stdout.txt'
$iniTarget = Join-Path $testDir 'DishonoredBorderless.ini'

function Write-ScenarioIni([hashtable]$overrides) {
    $lines = Get-Content $iniSource
    foreach ($key in $overrides.Keys) {
        $lines = $lines -replace "^$key=.*$", "$key=$($overrides[$key])"
    }
    Set-Content -LiteralPath $iniTarget -Value $lines
}

function Read-StateFile {
    $state = @{}
    foreach ($line in Get-Content $stateFile) {
        if ($line -match '^([^=]+)=(.*)$') { $state[$Matches[1]] = $Matches[2] }
    }
    return $state
}

function Get-ExpectedRect($monitor, [int]$width, [int]$height) {
    $monitorWidth  = $monitor.Right - $monitor.Left
    $monitorHeight = $monitor.Bottom - $monitor.Top
    if ($width  -le 0) { $width  = $monitorWidth }
    if ($height -le 0) { $height = $monitorHeight }
    [pscustomobject]@{
        Left   = $monitor.Left + [int][math]::Truncate(($monitorWidth - $width) / 2)
        Top    = $monitor.Top + [int][math]::Truncate(($monitorHeight - $height) / 2)
        Width  = $width
        Height = $height
    }
}

function Invoke-Scenario {
    param(
        [string]$Name,
        [hashtable]$Ini = @{},
        [int]$ExpectMonitorIndex = 0,   # 0 = whichever monitor the window lands on
        [int]$ExpectWidth = 0,
        [int]$ExpectHeight = 0
    )

    Write-Host ''
    Write-Host "=== $Name ===" -ForegroundColor Cyan
    Write-ScenarioIni $Ini
    Remove-Item $stateFile, $logFile, $stdout -ErrorAction SilentlyContinue

    $proc = Start-Process -FilePath $exe -ArgumentList $DurationMs, $stateFile `
        -WorkingDirectory $testDir -PassThru -RedirectStandardOutput $stdout -WindowStyle Hidden

    $deadline = (Get-Date).AddSeconds(20)
    while (-not (Test-Path $stateFile) -and (Get-Date) -lt $deadline) {
        if ($proc.HasExited) { break }
        Start-Sleep -Milliseconds 100
    }

    if (-not (Test-Path $stateFile)) {
        $output = if (Test-Path $stdout) { Get-Content $stdout -Raw } else { '' }
        if ($output) { Write-Host $output }

        # D3DERR_DEVICELOST at CreateDevice: some other process already owns the
        # display in exclusive fullscreen, and D3D9 will not hand out a second
        # device until it lets go. Nothing to do with the proxy.
        if ($output -match '0x88760868') {
            $suspects = Get-Process | Where-Object { $_.MainWindowTitle } |
                Where-Object { $_.ProcessName -match 'Dishonored|Game|steam_app' } |
                Select-Object -ExpandProperty ProcessName
            $hint = if ($suspects) { " Currently running: $($suspects -join ', ')." } else { '' }
            throw "CreateDevice returned D3DERR_DEVICELOST. Another process holds the display in exclusive fullscreen -- close it and re-run.$hint"
        }
        if ($proc.HasExited) {
            throw "the test app exited with code $($proc.ExitCode) before creating a device. An exit before any output at all usually means the DLL failed to load (wrong architecture, or a missing runtime import)."
        }
        $proc.Kill()
        throw 'the test app never wrote its state file'
    }

    $hwnd = [IntPtr][int64](Read-StateFile)['hwnd']

    $windowRect = New-Object Native+RECT
    $clientRect = New-Object Native+RECT
    [void][Native]::GetWindowRect($hwnd, [ref]$windowRect)
    [void][Native]::GetClientRect($hwnd, [ref]$clientRect)
    $style = [Native]::GetWindowLong($hwnd, -16)

    $monitors = [Native]::Monitors()
    $monitor  = if ($ExpectMonitorIndex -gt 0) { $monitors[$ExpectMonitorIndex - 1] } else { [Native]::MonitorOf($hwnd) }
    $expected = Get-ExpectedRect $monitor $ExpectWidth $ExpectHeight

    $windowWidth  = $windowRect.Right - $windowRect.Left
    $windowHeight = $windowRect.Bottom - $windowRect.Top

    Write-Host ("  window   : {0},{1} {2}x{3}" -f $windowRect.Left, $windowRect.Top, $windowWidth, $windowHeight)
    Write-Host ("  client   : {0}x{1}" -f $clientRect.Right, $clientRect.Bottom)
    Write-Host ("  expected : {0},{1} {2}x{3}" -f $expected.Left, $expected.Top, $expected.Width, $expected.Height)
    Write-Host ("  style    : 0x{0:X8}" -f $style)
    Write-Host ''

    Assert-That "$Name - the window still exists" ([Native]::IsWindow($hwnd)) $null

    $initial = Read-StateFile
    Assert-That "$Name - the device was forced to windowed" ($initial['windowed'] -eq '1') `
        "D3DPRESENT_PARAMETERS.Windowed reported by GetSwapChain = $($initial['windowed']); the app asked for FALSE"

    Assert-That "$Name - no caption" (($style -band $WS_CAPTION) -eq 0) `
        ("WS_CAPTION/WS_BORDER/WS_DLGFRAME must all be clear; style=0x{0:X8}" -f $style)
    Assert-That "$Name - no resizing frame" (($style -band $WS_THICKFRAME) -eq 0) $null
    Assert-That "$Name - WS_POPUP set" (($style -band $WS_POPUP) -ne 0) $null

    Assert-That "$Name - the window is exactly where it should be" (
        $windowRect.Left -eq $expected.Left -and $windowRect.Top -eq $expected.Top -and
        $windowWidth -eq $expected.Width -and $windowHeight -eq $expected.Height
    ) "got ${windowWidth}x${windowHeight} at $($windowRect.Left),$($windowRect.Top)"

    Assert-That "$Name - the client area is the whole window (no non-client frame)" (
        $clientRect.Right -eq $windowWidth -and $clientRect.Bottom -eq $windowHeight
    ) "client $($clientRect.Right)x$($clientRect.Bottom) vs window ${windowWidth}x${windowHeight}"

    # The backbuffer must be exactly what the app asked for, and must NOT track
    # the window. Rewriting it is unsound -- the app does not re-query the
    # device, so it keeps drawing at the size it requested and fills only a
    # corner of a larger buffer. That was a real 1.0.0 bug: Dishonored at 1080p
    # on a 4K monitor rendered its menu at quarter size over stale frames.
    # These two sizes are deliberately independent here (1280x720 requested,
    # 1600x900 window), so this assertion only passes if the size is untouched.
    $backBuffer = $initial['backBufferSize'] -split ','
    $requested  = $initial['requestedSize'] -split ','
    Assert-That "$Name - the backbuffer is left exactly as the app requested" (
        [int]$backBuffer[0] -eq [int]$requested[0] -and [int]$backBuffer[1] -eq [int]$requested[1]
    ) "backbuffer $($backBuffer -join 'x'), app asked for $($requested -join 'x')"

    if (-not $proc.WaitForExit($DurationMs + 20000)) {
        $proc.Kill()
        $script:failures += "$Name - the test app did not exit"
    }

    if (Test-Path $stdout) {
        $appOutput = (Get-Content $stdout -Raw).Trim()
        Write-Host ($appOutput -replace '(?m)^', '  | ') -ForegroundColor DarkGray
    }

    # The mid-run Reset is how the game's own video options change mode. It has
    # to be turned around the same way CreateDevice was, and the window has to
    # come out of it unchanged.
    $final = Read-StateFile
    Assert-That "$Name - the mid-run Reset was attempted and succeeded" (
        $final['resetAttempted'] -eq '1' -and $final['resetHr'] -eq '00000000'
    ) "resetAttempted=$($final['resetAttempted']) resetHr=0x$($final['resetHr'])"

    $finalRect = $final['windowRect'] -split ','
    Assert-That "$Name - the geometry survived the Reset and the whole run" (
        [int]$finalRect[0] -eq $windowRect.Left -and [int]$finalRect[1] -eq $windowRect.Top -and
        [int]$finalRect[2] -eq $windowRect.Right -and [int]$finalRect[3] -eq $windowRect.Bottom
    ) "at exit: $($final['windowRect']); at startup: $($windowRect.Left),$($windowRect.Top),$($windowRect.Right),$($windowRect.Bottom)"

    if (Test-Path $logFile) {
        $log = Get-Content $logFile -Raw
        Assert-That "$Name - CreateDevice was hooked" ($log -match 'hooked IDirect3D9::CreateDevice') $null
        Assert-That "$Name - Reset was hooked and rewrote the mode" (
            ($log -match 'hooked IDirect3DDevice9::Reset') -and ($log -match 'Reset applied\s*:\s*Windowed=TRUE')
        ) 'the log must show Reset being turned back into a windowed mode'
        Assert-That "$Name - the process became DPI-aware" ($log -match 'IsProcessDPIAware=yes') `
            'without this the window is upscaled by the compositor on a scaled display'
    } else {
        Assert-That "$Name - the proxy wrote a log" $false "expected $logFile"
    }
}

$monitors = [Native]::Monitors()
Write-Host "Monitors (EnumDisplayMonitors order):" -ForegroundColor Cyan
for ($i = 0; $i -lt $monitors.Count; $i++) {
    Write-Host ("  {0}: {1},{2} {3}x{4}" -f ($i + 1), $monitors[$i].Left, $monitors[$i].Top,
        ($monitors[$i].Right - $monitors[$i].Left), ($monitors[$i].Bottom - $monitors[$i].Top))
}

Invoke-Scenario -Name 'defaults' -Ini @{}

if ($monitors.Count -ge 2 -and -not $SkipMultiMonitor) {
    # A non-zero monitor origin plus an explicit size: this is the only run that
    # exercises the centring arithmetic and TargetMonitorIndex at all.
    Invoke-Scenario -Name 'monitor 2 @ 1600x900' `
        -Ini @{ TargetMonitorIndex = 2; ResolutionWidth = 1600; ResolutionHeight = 900 } `
        -ExpectMonitorIndex 2 -ExpectWidth 1600 -ExpectHeight 900
} elseif ($SkipMultiMonitor) {
    Write-Host ''
    Write-Host 'Skipping the multi-monitor scenario (-SkipMultiMonitor).' -ForegroundColor Yellow
} else {
    Write-Host ''
    Write-Host 'Skipping the multi-monitor scenario: only one display.' -ForegroundColor Yellow
}

# Leave the build directory holding the shipped defaults, not a scenario's ini.
Copy-Item $iniSource $iniTarget -Force

Write-Host ''
if ($script:failures.Count -gt 0) {
    Write-Host ("FAILED: {0} of {1} checks" -f $script:failures.Count, $script:checks) -ForegroundColor Red
    foreach ($failure in $script:failures) { Write-Host "  - $failure" -ForegroundColor Red }
    exit 1
}
Write-Host ("PASSED: all {0} checks" -f $script:checks) -ForegroundColor Green
