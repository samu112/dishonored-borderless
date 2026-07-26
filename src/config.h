// DishonoredBorderless.ini loader (plan.md 3.2).
#pragma once

#include <windows.h>

#include "log.h"

struct Config {
    BOOL     enabled;                 // master switch
    int      resolutionWidth;         // 0 = use the target monitor's width
    int      resolutionHeight;        // 0 = use the target monitor's height
    int      targetMonitorIndex;      // 0 = monitor the game window is on, 1..N = explicit
    BOOL     enableWndProcHook;       // plan.md 4.3
    BOOL     forceWindowed;           // rewrite D3DPRESENT_PARAMETERS.Windowed (plan.md 4.2.2)
    // MatchBackBufferToWindow was removed in 1.0.1. It rewrote the backbuffer
    // dimensions behind the game's back, which corrupted rendering at every
    // resolution except the monitor's own -- see RewritePresentParams. The key
    // is still read from existing ini files and ignored, so an old config does
    // not resurrect the behaviour.
    BOOL     blockMinimizeOnFocusLoss;
    BOOL     topmost;                 // off by default: topmost breaks alt-tab ergonomics
    BOOL     setDpiAwareness;         // required for correct sizing on scaled displays
    DWORD    enforceIntervalMs;       // watchdog period, 0 disables (plan.md 4.6)
    LogLevel logLevel;
    char     logFile[MAX_PATH];
    char     iniPath[MAX_PATH];
    BOOL     iniFound;
};

// Reads DishonoredBorderless.ini from the directory holding this DLL.
// Safe to call from DllMain: only kernel32 profile APIs are used.
void          Config_Load(HINSTANCE self);
const Config& Cfg();
