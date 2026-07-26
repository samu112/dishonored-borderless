#include "hooks_d3d9.h"

#include <d3d9.h>

#include "config.h"
#include "log.h"
#include "window.h"

// Why vtable patching and not the COM wrapper objects plan.md 4.2.1 sketches:
// the game keeps the genuine IDirect3D9 / IDirect3DDevice9 pointers, so nothing
// that compares interface pointers (or that hooks the same vtable, e.g. the
// Steam overlay) sees anything unusual, and we do not have to hand-forward the
// ~119 methods of IDirect3DDevice9 - every one of which is a chance to get a
// signature subtly wrong. We only need two call sites.
//
// Slot numbers come from the interface declaration order in d3d9.h and are
// fixed by COM's ABI:
//   IDirect3D9:        0..2 IUnknown, 3..15 adapter/caps queries, 16 CreateDevice
//   IDirect3DDevice9:  0..2 IUnknown, 3..15 caps/swapchain queries, 16 Reset
static const int kSlot_CreateDevice = 16;
static const int kSlot_Reset        = 16;

typedef HRESULT(WINAPI* PFN_CreateDevice)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD,
                                          D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
typedef HRESULT(WINAPI* PFN_Reset)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);

static PFN_CreateDevice g_realCreateDevice = NULL;
static PFN_Reset        g_realReset        = NULL;

// ---------------------------------------------------------------------------
// vtable patching
// ---------------------------------------------------------------------------

static BOOL PatchSlot(void* object, int slot, void* hook, void** original)
{
    if (!object) return FALSE;

    void** vtable = *(void***)object;

    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(&vtable[slot], &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) {
        LOGE("vtable slot %d is not readable memory", slot);
        return FALSE;
    }
    if (vtable[slot] == hook) return FALSE;   // already ours

    DWORD previous = 0;
    if (!VirtualProtect(&vtable[slot], sizeof(void*), PAGE_READWRITE, &previous)) {
        LOGE("VirtualProtect on vtable slot %d failed (%u)", slot, GetLastError());
        return FALSE;
    }

    *original    = vtable[slot];
    vtable[slot] = hook;

    DWORD ignored = 0;
    VirtualProtect(&vtable[slot], sizeof(void*), previous, &ignored);
    FlushInstructionCache(GetCurrentProcess(), &vtable[slot], sizeof(void*));
    return TRUE;
}

// ---------------------------------------------------------------------------
// Present parameter rewriting (plan.md 4.2.2)
// ---------------------------------------------------------------------------

static const char* Bool(BOOL b) { return b ? "TRUE" : "FALSE"; }

static void LogParams(const char* tag, const D3DPRESENT_PARAMETERS* pp)
{
    LOGI("%s: Windowed=%s %ux%u fmt=%u refresh=%u interval=%x hDeviceWindow=%x", tag,
         Bool(pp->Windowed), pp->BackBufferWidth, pp->BackBufferHeight, (unsigned)pp->BackBufferFormat,
         pp->FullScreen_RefreshRateInHz, pp->PresentationInterval,
         (unsigned)(UINT_PTR)pp->hDeviceWindow);
}

static void RewritePresentParams(D3DPRESENT_PARAMETERS* pp, HWND hwnd)
{
    const Config& cfg = Cfg();

    if (cfg.forceWindowed && !pp->Windowed) {
        pp->Windowed                    = TRUE;
        pp->FullScreen_RefreshRateInHz  = 0;   // must be 0 in windowed mode

        // A format picked for an exclusive-fullscreen mode is not necessarily
        // presentable windowed; D3DFMT_UNKNOWN means "whatever the desktop is".
        if (pp->BackBufferFormat != D3DFMT_X8R8G8B8 && pp->BackBufferFormat != D3DFMT_A8R8G8B8) {
            pp->BackBufferFormat = D3DFMT_UNKNOWN;
        }
    }

    if (pp->Windowed) {
        // Only these three intervals are legal for a windowed swap chain.
        const UINT interval = pp->PresentationInterval;
        if (interval != D3DPRESENT_INTERVAL_DEFAULT && interval != D3DPRESENT_INTERVAL_ONE &&
            interval != D3DPRESENT_INTERVAL_IMMEDIATE) {
            LOGI("presentation interval %x is invalid windowed, using INTERVAL_ONE", interval);
            pp->PresentationInterval = D3DPRESENT_INTERVAL_ONE;
        }
    }

    // The backbuffer size is deliberately left exactly as the game asked for it.
    //
    // An earlier version rewrote it to the window size. That is unsound: the
    // game does not re-query the device afterwards, so it goes on setting its
    // viewport and laying out its UI for the size it *requested*, draws into
    // one corner of a larger buffer, and never clears the rest. Dishonored does
    // precisely this if you select 1080p on a 4K monitor -- the menu renders at
    // quarter size in the top-left with stale frames littering the remainder.
    // It went unnoticed because at the monitor's own resolution the rewrite was
    // a no-op.
    //
    // Nothing is lost by honouring the request. A windowed Present already
    // scales the backbuffer to the client area, so a 1080p backbuffer still
    // fills a 4K borderless window; the game just renders fewer pixels, which
    // is what choosing 1080p is supposed to mean.
    (void)hwnd;
}

static HWND ResolveWindow(const D3DPRESENT_PARAMETERS* pp, HWND focusWindow)
{
    if (pp && pp->hDeviceWindow && IsWindow(pp->hDeviceWindow)) return pp->hDeviceWindow;
    if (focusWindow && IsWindow(focusWindow)) return focusWindow;

    HWND found = Win_FindGameWindow();
    if (found) LOGI("no device/focus window supplied; using %x from EnumWindows",
                    (unsigned)(UINT_PTR)found);
    return found;
}

// ---------------------------------------------------------------------------
// Hooks
// ---------------------------------------------------------------------------

static HRESULT WINAPI Hook_Reset(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* pp)
{
    if (!pp || !Cfg().enabled) return g_realReset(device, pp);

    LogParams("Reset requested", pp);

    HWND hwnd = ResolveWindow(pp, Win_GetWindow());
    if (hwnd) Win_Attach(hwnd);

    const D3DPRESENT_PARAMETERS asRequested = *pp;
    RewritePresentParams(pp, hwnd);
    LogParams("Reset applied  ", pp);

    HRESULT hr = g_realReset(device, pp);
    if (FAILED(hr)) {
        LOGE("Reset failed with our parameters (hr=%x); retrying with the game's own",
             (unsigned)hr);
        *pp = asRequested;
        hr  = g_realReset(device, pp);
        LOGE("retry with the game's own parameters: hr=%x", (unsigned)hr);
    } else if (hwnd) {
        Win_Enforce(FALSE);
    }
    return hr;
}

static HRESULT WINAPI Hook_CreateDevice(IDirect3D9* d3d9, UINT adapter, D3DDEVTYPE deviceType,
                                        HWND focusWindow, DWORD behaviorFlags,
                                        D3DPRESENT_PARAMETERS* pp, IDirect3DDevice9** device)
{
    if (!pp || !Cfg().enabled) {
        return g_realCreateDevice(d3d9, adapter, deviceType, focusWindow, behaviorFlags, pp, device);
    }

    LogParams("CreateDevice requested", pp);

    // Make the window borderless and desktop-sized *before* the device exists,
    // so the backbuffer we ask for matches the client area exactly and D3D9
    // never has to stretch the presented image.
    HWND hwnd = ResolveWindow(pp, focusWindow);
    if (hwnd) Win_Attach(hwnd);
    else      LOGE("could not identify the game window; sizing from the primary monitor");

    const D3DPRESENT_PARAMETERS asRequested = *pp;
    RewritePresentParams(pp, hwnd);
    LogParams("CreateDevice applied  ", pp);

    HRESULT hr = g_realCreateDevice(d3d9, adapter, deviceType, focusWindow, behaviorFlags, pp, device);
    if (FAILED(hr)) {
        LOGE("CreateDevice failed with our parameters (hr=%x); retrying with the game's own",
             (unsigned)hr);
        if (hr == D3DERR_DEVICELOST) {
            LOGE("D3DERR_DEVICELOST here usually means another process already owns the "
                 "display in exclusive fullscreen.");
        }
        *pp = asRequested;
        hr  = g_realCreateDevice(d3d9, adapter, deviceType, focusWindow, behaviorFlags, pp, device);
        LOGE("retry with the game's own parameters: hr=%x", (unsigned)hr);
        return hr;
    }

    if (device && *device) {
        if (PatchSlot(*device, kSlot_Reset, (void*)Hook_Reset, (void**)&g_realReset)) {
            LOGI("hooked IDirect3DDevice9::Reset");
        }
    }
    if (hwnd) Win_Enforce(FALSE);
    return hr;
}

void Hooks_InstallOnD3D9(IDirect3D9* d3d9)
{
    if (!d3d9) return;
    if (!Cfg().enabled) {
        LOGI("Enabled=0: leaving D3D9 untouched");
        return;
    }
    if (PatchSlot(d3d9, kSlot_CreateDevice, (void*)Hook_CreateDevice, (void**)&g_realCreateDevice)) {
        LOGI("hooked IDirect3D9::CreateDevice");
    }
}
