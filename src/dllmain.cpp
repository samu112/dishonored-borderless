#include <windows.h>

#include "config.h"
#include "log.h"
#include "proxy.h"
#include "window.h"

// DPI_AWARENESS_CONTEXT values, spelled out so this builds against older SDK
// headers. Negative pseudo-handles, per the Win32 documentation.
#define DBL_DPI_CTX_PER_MONITOR_AWARE_V2 ((HANDLE)-4)
#define DBL_DPI_CTX_PER_MONITOR_AWARE    ((HANDLE)-3)
#define DBL_DPI_CTX_SYSTEM_AWARE         ((HANDLE)-2)

typedef BOOL(WINAPI* PFN_SetProcessDpiAwarenessContext)(HANDLE);
typedef BOOL(WINAPI* PFN_SetProcessDPIAware)(void);
typedef BOOL(WINAPI* PFN_IsProcessDPIAware)(void);

// Must happen before the game creates its window, which is well before
// Direct3DCreate9 is called - so it happens here, in DllMain. Only user32 is
// touched, and user32 is already loaded by the time a statically imported DLL
// gets DLL_PROCESS_ATTACH, so there is no LoadLibrary under the loader lock.
static void ApplyDpiAwareness()
{
    if (!Cfg().setDpiAwareness) {
        LOGI("SetDpiAwareness=0: leaving the process DPI-unaware");
        return;
    }

    HMODULE user32 = GetModuleHandleA("user32.dll");
    if (!user32) {
        LOGE("user32.dll is not loaded yet; skipping DPI awareness");
        return;
    }

    BOOL applied = FALSE;

    PFN_SetProcessDpiAwarenessContext setContext =
        (PFN_SetProcessDpiAwarenessContext)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
    if (setContext) {
        applied = setContext(DBL_DPI_CTX_PER_MONITOR_AWARE_V2);
        if (!applied) {
            LOGD("per-monitor-v2 rejected (%u), trying per-monitor", GetLastError());
            applied = setContext(DBL_DPI_CTX_PER_MONITOR_AWARE);
        }
        if (!applied) {
            LOGD("per-monitor rejected (%u), trying system-aware", GetLastError());
            applied = setContext(DBL_DPI_CTX_SYSTEM_AWARE);
        }
    }

    if (!applied) {
        PFN_SetProcessDPIAware legacy = (PFN_SetProcessDPIAware)GetProcAddress(user32, "SetProcessDPIAware");
        if (legacy) applied = legacy();
    }

    // Worth stating outright in the log: if the exe's manifest already declares
    // a DPI awareness, every call above is a no-op and returns failure. On a
    // scaled display that is the difference between a crisp desktop-sized
    // window and a blurry upscaled one, and it is otherwise invisible.
    PFN_IsProcessDPIAware isAware = (PFN_IsProcessDPIAware)GetProcAddress(user32, "IsProcessDPIAware");
    const BOOL aware = isAware ? isAware() : FALSE;
    LOGI("DPI awareness: applied=%s, IsProcessDPIAware=%s (virtual screen %dx%d)",
         applied ? "yes" : "no", aware ? "yes" : "no",
         GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN));
    if (!aware) {
        LOGE("the process is DPI-unaware: on a scaled display the window will be "
             "upscaled by the compositor. See README 'Blurry image'.");
    }
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(instance);
        Proxy_Init(instance);
        Config_Load(instance);
        Log_Init(instance);
        LOGI("config: %s (%s)", Cfg().iniPath, Cfg().iniFound ? "loaded" : "missing, using defaults");
        LOGI("settings: enabled=%d res=%dx%d monitor=%d wndproc=%d windowed=%d watchdog=%ums",
             Cfg().enabled, Cfg().resolutionWidth, Cfg().resolutionHeight, Cfg().targetMonitorIndex,
             Cfg().enableWndProcHook, Cfg().forceWindowed, Cfg().enforceIntervalMs);
        ApplyDpiAwareness();
        break;

    case DLL_PROCESS_DETACH:
        Win_Detach();
        Proxy_Shutdown();
        LOGI("----- DishonoredBorderless detached -----");
        Log_Shutdown();
        break;

    default:
        break;
    }
    return TRUE;
}
