// Proxy exports for d3d9.dll (plan.md 4.2.1).
//
// Dishonored.exe statically imports exactly four symbols from d3d9.dll:
// Direct3DCreate9, D3DPERF_BeginEvent, D3DPERF_EndEvent and D3DPERF_SetOptions.
// The rest are exported anyway so this DLL is a drop-in for anything else that
// happens to sit in the same directory.
#include <windows.h>
#include <d3d9.h>

#include "config.h"
#include "hooks_d3d9.h"
#include "log.h"
#include "proxy.h"

static HMODULE          g_real = NULL;
static HINSTANCE        g_self = NULL;
static CRITICAL_SECTION g_loadLock;
static BOOL             g_loadLockReady = FALSE;

void Proxy_Init(HINSTANCE self)
{
    g_self = self;
    if (!g_loadLockReady) {
        InitializeCriticalSection(&g_loadLock);
        g_loadLockReady = TRUE;
    }
}

void Proxy_Shutdown()
{
    // The real d3d9.dll is intentionally not freed: other modules in the
    // process may still hold interfaces that live inside it.
    if (g_loadLockReady) {
        DeleteCriticalSection(&g_loadLock);
        g_loadLockReady = FALSE;
    }
}

// Loaded lazily, never from DllMain: LoadLibrary under the loader lock is a
// deadlock waiting to happen.
static HMODULE RealD3D9()
{
    if (g_real) return g_real;
    if (g_loadLockReady) EnterCriticalSection(&g_loadLock);

    if (!g_real) {
        char path[MAX_PATH];
        // In a 32-bit process this resolves to SysWOW64 directly, so we never
        // depend on filesystem redirection behaving.
        UINT len = GetSystemDirectoryA(path, MAX_PATH);
        if (len == 0 || len > MAX_PATH - 12) {
            lstrcpyA(path, "C:\\Windows\\System32");
        }
        lstrcatA(path, "\\d3d9.dll");

        HMODULE module = LoadLibraryA(path);
        if (!module) {
            LOGE("LoadLibrary('%s') failed (%u)", path, GetLastError());
        } else if (module == (HMODULE)g_self) {
            LOGE("refusing to proxy onto ourselves ('%s')", path);
            module = NULL;
        } else {
            LOGI("loaded the system d3d9 from '%s'", path);
        }
        g_real = module;
    }

    if (g_loadLockReady) LeaveCriticalSection(&g_loadLock);
    return g_real;
}

static FARPROC RealProc(const char* name)
{
    HMODULE module = RealD3D9();
    if (!module) return NULL;
    FARPROC proc = GetProcAddress(module, name);
    if (!proc) LOGE("the system d3d9.dll has no export '%s'", name);
    return proc;
}

// ---------------------------------------------------------------------------
// Exports
// ---------------------------------------------------------------------------

extern "C" {

IDirect3D9* WINAPI Direct3DCreate9(UINT sdkVersion)
{
    typedef IDirect3D9*(WINAPI * PFN)(UINT);
    PFN real = (PFN)RealProc("Direct3DCreate9");
    if (!real) return NULL;

    IDirect3D9* d3d9 = real(sdkVersion);
    LOGI("Direct3DCreate9(%u) -> %x", sdkVersion, (unsigned)(UINT_PTR)d3d9);
    if (d3d9) Hooks_InstallOnD3D9(d3d9);
    return d3d9;
}

HRESULT WINAPI Direct3DCreate9Ex(UINT sdkVersion, IDirect3D9Ex** out)
{
    typedef HRESULT(WINAPI * PFN)(UINT, IDirect3D9Ex**);
    PFN real = (PFN)RealProc("Direct3DCreate9Ex");
    if (!real) return E_NOINTERFACE;

    HRESULT hr = real(sdkVersion, out);
    LOGI("Direct3DCreate9Ex(%u) -> hr=%x", sdkVersion, (unsigned)hr);
    // IDirect3D9Ex derives from IDirect3D9, so CreateDevice is the same slot.
    if (SUCCEEDED(hr) && out && *out) Hooks_InstallOnD3D9((IDirect3D9*)*out);
    return hr;
}

int WINAPI D3DPERF_BeginEvent(D3DCOLOR color, LPCWSTR name)
{
    typedef int(WINAPI * PFN)(D3DCOLOR, LPCWSTR);
    PFN real = (PFN)RealProc("D3DPERF_BeginEvent");
    return real ? real(color, name) : -1;
}

int WINAPI D3DPERF_EndEvent(void)
{
    typedef int(WINAPI * PFN)(void);
    PFN real = (PFN)RealProc("D3DPERF_EndEvent");
    return real ? real() : -1;
}

void WINAPI D3DPERF_SetMarker(D3DCOLOR color, LPCWSTR name)
{
    typedef void(WINAPI * PFN)(D3DCOLOR, LPCWSTR);
    PFN real = (PFN)RealProc("D3DPERF_SetMarker");
    if (real) real(color, name);
}

void WINAPI D3DPERF_SetRegion(D3DCOLOR color, LPCWSTR name)
{
    typedef void(WINAPI * PFN)(D3DCOLOR, LPCWSTR);
    PFN real = (PFN)RealProc("D3DPERF_SetRegion");
    if (real) real(color, name);
}

BOOL WINAPI D3DPERF_QueryRepeatFrame(void)
{
    typedef BOOL(WINAPI * PFN)(void);
    PFN real = (PFN)RealProc("D3DPERF_QueryRepeatFrame");
    return real ? real() : FALSE;
}

void WINAPI D3DPERF_SetOptions(DWORD options)
{
    typedef void(WINAPI * PFN)(DWORD);
    PFN real = (PFN)RealProc("D3DPERF_SetOptions");
    if (real) real(options);
}

DWORD WINAPI D3DPERF_GetStatus(void)
{
    typedef DWORD(WINAPI * PFN)(void);
    PFN real = (PFN)RealProc("D3DPERF_GetStatus");
    return real ? real() : 0;
}

}  // extern "C"
