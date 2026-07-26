// Borderless window enforcement: WndProc subclass + watchdog (plan.md 4.3, 4.6).
#pragma once

#include <windows.h>

// Subclasses hwnd, applies the borderless style/geometry, and starts the
// watchdog. Safe to call repeatedly; re-attaches if the game recreated its
// window.
void Win_Attach(HWND hwnd);

// Restores the original WndProc and stops the watchdog.
void Win_Detach();

HWND Win_GetWindow();

// plan.md 4.3.1 fallback: EnumWindows filtered by our own process id.
HWND Win_FindGameWindow();

// Target client size for this window, used to size the D3D9 backbuffer.
BOOL Win_GetTargetSize(HWND hwnd, int* width, int* height);

// Re-applies style and geometry now. `async` avoids blocking when called from
// the watchdog thread rather than the window's own thread.
void Win_Enforce(BOOL async);
