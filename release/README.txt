===============================================================================
 DishonoredBorderless 1.0.2
 Borderless windowed mode for Dishonored (2012)
===============================================================================

Dishonored has no borderless windowed option. Its "windowed" mode gives you a
titlebar and a fixed size, and its fullscreen mode is exclusive fullscreen, so
alt-tabbing makes the whole display flicker and re-mode for several seconds.

This gives you real borderless fullscreen: no border, filling your monitor,
and alt-tab is instant.


-------------------------------------------------------------------------------
 INSTALL
-------------------------------------------------------------------------------

Copy these two files:

    d3d9.dll
    DishonoredBorderless.ini

into your Dishonored\Binaries\Win32 folder -- the one with Dishonored.exe in
it. On a default Steam install that is:

    C:\Program Files (x86)\Steam\steamapps\common\Dishonored\Binaries\Win32

If your Steam library lives on another drive, look for Dishonored\Binaries\Win32
wherever that library is.

That is the whole installation. Launch the game normally; there is nothing to
enable and no in-game menu. Leave the game's own video settings on fullscreen --
the mod turns that into borderless for you.

One thing to check first: if you already run ReShade, ENB or dgVoodoo, they use
the d3d9.dll filename too. Do not overwrite theirs -- see COMPATIBILITY.


-------------------------------------------------------------------------------
 UNINSTALL
-------------------------------------------------------------------------------

Delete these from Binaries\Win32:

    d3d9.dll
    DishonoredBorderless.ini
    DishonoredBorderless.log

Nothing else on your system is touched. No registry keys, no game files
modified, no launcher, no background process. The game goes back to exactly
how it was.


-------------------------------------------------------------------------------
 "IS THIS SAFE?" -- WHAT THE DLL ACTUALLY DOES
-------------------------------------------------------------------------------

Fair question. A d3d9.dll dropped next to a game executable is the same shape
as a DLL hijack, and your antivirus may well say so. Here is exactly what it
does, and you can verify all of it from the log the mod writes.

Windows loads a DLL from the program's own folder before the one in System32.
So when Dishonored asks for d3d9.dll it gets this one. This DLL immediately
loads the real C:\WINDOWS\system32\d3d9.dll and forwards every call to it. It
changes two things on the way past:

  1. When the game creates its Direct3D device asking for exclusive fullscreen,
     the request is rewritten to windowed. Whatever resolution you picked in the
     game's own options is left untouched and scaled up to fill the window.
  2. The game's window is stripped of its border and held at monitor size,
     because the engine tries to put its own frame back.

That is all. It does not modify any game file, and it does not touch your saves,
achievements or Steam account.

It writes exactly one file: DishonoredBorderless.log, next to the DLL -- or in
%LOCALAPPDATA%\DishonoredBorderless\ instead, if the game folder turns out to be
read-only. That log is plain text and you can read it.

It makes no network connections. You do not have to take that on faith: open
d3d9.dll in any PE viewer and look at its import table. The only things it
imports are USER32, KERNEL32 and the Windows C runtime -- no networking library
is present, so it could not phone home even if it wanted to.

SHA-256 of d3d9.dll:
7a3894eca35a1824d64b17f97cec6daf10e93335f90383d2c18463a0bc11e93d

The full C++ source is published alongside this download, as a separate file on
the same mod page. Nothing in this DLL is hidden from you.


-------------------------------------------------------------------------------
 SETTINGS (optional)
-------------------------------------------------------------------------------

Everything works out of the box. If you want to change something, edit
DishonoredBorderless.ini in Binaries\Win32; every setting is explained inside
the file. The ones people actually change:

  ResolutionWidth / ResolutionHeight
      0 = fill the monitor (default). Any smaller size is centred on screen and
      the game renders at that size, so this doubles as a resolution override.

  TargetMonitorIndex
      0 = whichever monitor the game opens on (default). 1, 2, 3... = a
      specific monitor, if it lands on the wrong one.

  BlockMinimizeOnFocusLoss
      1 = stop the game minimising when it loses focus (default).

  Enabled
      0 = turn the whole thing off without uninstalling.


-------------------------------------------------------------------------------
 TROUBLESHOOTING
-------------------------------------------------------------------------------

Your antivirus flags d3d9.dll.
    Expected. The file is not code-signed, and a d3d9.dll next to a game exe
    looks structurally identical to malware even when it isn't. Check the
    SHA-256 above against the file you downloaded, and read the "IS THIS SAFE?"
    section. If you are not comfortable, don't install it -- that is a
    reasonable call.

The game does not start at all, and there is no error message.
    That is what a DLL that fails to load looks like. Delete d3d9.dll to
    confirm the game starts again. Most likely you have a 64-bit or corrupted
    download -- re-extract the zip and check the SHA-256.

The image is blurry, or the window is smaller than the desktop.
    Your display is running above 100% scaling and the game is not DPI-aware.
    Check DishonoredBorderless.log for "IsProcessDPIAware". If it says "no",
    fix it in Windows instead: right-click Dishonored.exe, Properties,
    Compatibility, "Change high DPI settings", tick "Override high DPI scaling
    behaviour", set it to "Application".

Nothing happens and no log file appears.
    Another mod already installed its own d3d9.dll -- ReShade, dgVoodoo and
    ENB all use that filename. Two d3d9 wrappers cannot be chained; you have to
    pick one.

The Windows key does nothing while the game has focus.
    Expected, and unrelated to this mod. Dishonored grabs the keyboard through
    DirectInput8, which suppresses the Windows logo key whenever the game has
    focus -- windowed or not. Alt+Tab works fine.

It goes borderless and then reverts.
    Set LogLevel=debug in the ini, reproduce it, and look at the log. Please
    report it with that log attached.

Anything else.
    DishonoredBorderless.log sits next to the DLL in Binaries\Win32 and records
    what the mod saw and what it did. It is the first thing to look at, and the
    first thing to attach to a bug report.


-------------------------------------------------------------------------------
 COMPATIBILITY
-------------------------------------------------------------------------------

Dishonored (2012), Steam edition, Windows.
Developed and tested on Windows 11 with a multi-monitor setup at 4K.
Not tested against the GOG or Epic releases -- it should work, since it keys
off the D3D9 renderer rather than anything Steam-specific, but nobody has
confirmed it.

Works alongside the Steam overlay -- the overlay hooks the same Direct3D
interfaces this does, and the two coexist because this mod hands the game the
genuine D3D9 objects rather than wrappers pretending to be them.

Not compatible with other mods that install their own d3d9.dll (ReShade, ENB,
dgVoodoo). Only one of them can have the filename.


-------------------------------------------------------------------------------
 CHANGES
-------------------------------------------------------------------------------

1.0.2
    Removed the Install.bat / Uninstall.bat / Install.ps1 helper scripts. They
    only copied two files into a folder, which you can do yourself in less time
    than it takes to read this line.

    They were dropped because a .bat that launches PowerShell with execution
    policy disabled looks exactly like a malware dropper to an automated
    scanner, whatever it actually does -- and that is not a fight worth having
    over a convenience feature. Nothing about the mod itself changed: d3d9.dll
    is byte-for-byte identical to 1.0.1 and has the same SHA-256.

1.0.1
    Fixed: choosing any resolution other than your monitor's native one in the
    game's video options produced badly corrupted rendering -- the picture drawn
    at quarter size in a corner, with fragments of old frames filling the rest.

    The mod had been resizing the Direct3D backbuffer to match the window. The
    game never re-checks that size, so it carried on drawing at the resolution
    it had asked for, into part of a larger buffer, and nothing cleared the
    remainder. Your chosen resolution is now left exactly as the game set it and
    scaled up to fill the borderless window.

    The MatchBackBufferToWindow setting is gone. If it is still in your ini it
    is ignored, and you do not need to remove it.

1.0.0
    First release.


-------------------------------------------------------------------------------
 LICENCE
-------------------------------------------------------------------------------

MIT licensed -- see LICENSE.txt. Do what you like with it.
