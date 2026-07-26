// plan.md 4.1: "Build a trivial D3D9 sample app and test proxy DLL there first.
// Confirm your proxy works before attaching to Dishonored."
//
// It behaves the way Dishonored does in the ways that matter: 32-bit, a static
// import of Direct3DCreate9, a plain decorated window, and a device requested
// as exclusive fullscreen at a resolution that is not the desktop's. Run it
// with build\d3d9.dll beside it and the window should come up borderless and
// desktop-sized instead.
//
//   d3d9_testapp.exe [milliseconds] [state-file]
//
// The state file receives the HWND and the geometry the app sees, so
// Verify-Borderless.ps1 can assert against it from a separate process.
#define UNICODE
#define _UNICODE

#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
#include <stdlib.h>

static const int kRequestedWidth  = 1280;
static const int kRequestedHeight = 720;

// Set once the mid-run Reset has been attempted. Reset is the path the game
// takes on an in-game resolution or fullscreen/windowed change, so the test has
// to actually travel it rather than just check that it was hooked.
static BOOL    g_resetAttempted = FALSE;
static HRESULT g_resetHr        = S_OK;

static LRESULT CALLBACK TestWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    if (msg == WM_KEYDOWN && wParam == VK_ESCAPE) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void WriteState(const char* path, HWND hwnd, IDirect3DDevice9* device)
{
    RECT window = {0, 0, 0, 0};
    RECT client = {0, 0, 0, 0};
    GetWindowRect(hwnd, &window);
    GetClientRect(hwnd, &client);

    D3DPRESENT_PARAMETERS actual;
    ZeroMemory(&actual, sizeof(actual));
    IDirect3DSwapChain9* chain = NULL;
    if (device && SUCCEEDED(device->GetSwapChain(0, &chain)) && chain) {
        chain->GetPresentParameters(&actual);
        chain->Release();
    }

    FILE* file = fopen(path, "w");
    if (!file) {
        printf("could not write the state file '%s'\n", path);
        return;
    }
    fprintf(file, "hwnd=%u\n", (unsigned)(UINT_PTR)hwnd);
    fprintf(file, "pid=%u\n", (unsigned)GetCurrentProcessId());
    fprintf(file, "windowRect=%d,%d,%d,%d\n", (int)window.left, (int)window.top,
            (int)window.right, (int)window.bottom);
    fprintf(file, "clientSize=%d,%d\n", (int)client.right, (int)client.bottom);
    fprintf(file, "requestedSize=%d,%d\n", kRequestedWidth, kRequestedHeight);
    fprintf(file, "backBufferSize=%u,%u\n", actual.BackBufferWidth, actual.BackBufferHeight);
    fprintf(file, "windowed=%d\n", actual.Windowed ? 1 : 0);
    fprintf(file, "style=%x\n", (unsigned)GetWindowLongA(hwnd, GWL_STYLE));
    fprintf(file, "exstyle=%x\n", (unsigned)GetWindowLongA(hwnd, GWL_EXSTYLE));
    fprintf(file, "resetAttempted=%d\n", g_resetAttempted ? 1 : 0);
    fprintf(file, "resetHr=%08X\n", (unsigned)g_resetHr);
    fclose(file);
}

int main(int argc, char** argv)
{
    const DWORD durationMs = argc > 1 ? (DWORD)atoi(argv[1]) : 4000;
    const char* statePath  = argc > 2 ? argv[2] : "d3d9_testapp_state.txt";

    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = TestWndProc;
    wc.hInstance     = GetModuleHandleW(NULL);
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"DishonoredBorderlessTestApp";
    if (!RegisterClassExW(&wc)) {
        printf("RegisterClassEx failed (%u)\n", (unsigned)GetLastError());
        return 2;
    }

    // A conventional decorated window, exactly what a game creates before it
    // switches to fullscreen.
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"DishonoredBorderless test app",
                                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                kRequestedWidth, kRequestedHeight, NULL, NULL, wc.hInstance, NULL);
    if (!hwnd) {
        printf("CreateWindowEx failed (%u)\n", (unsigned)GetLastError());
        return 2;
    }
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    IDirect3D9* d3d9 = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d9) {
        printf("Direct3DCreate9 returned NULL\n");
        return 3;
    }
    printf("Direct3DCreate9 ok\n");

    D3DPRESENT_PARAMETERS pp;
    ZeroMemory(&pp, sizeof(pp));
    pp.BackBufferWidth            = kRequestedWidth;
    pp.BackBufferHeight           = kRequestedHeight;
    pp.BackBufferFormat           = D3DFMT_X8R8G8B8;
    pp.BackBufferCount            = 1;
    pp.SwapEffect                 = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow              = hwnd;
    pp.Windowed                   = FALSE;   // the proxy is expected to override this
    pp.EnableAutoDepthStencil     = TRUE;
    pp.AutoDepthStencilFormat     = D3DFMT_D24S8;
    pp.FullScreen_RefreshRateInHz = D3DPRESENT_RATE_DEFAULT;
    pp.PresentationInterval       = D3DPRESENT_INTERVAL_ONE;

    // The proxy rewrites these in place, so keep the untouched request around:
    // it is what an in-game "go fullscreen" toggle would hand to Reset.
    const D3DPRESENT_PARAMETERS asRequested = pp;

    IDirect3DDevice9* device = NULL;
    HRESULT hr = d3d9->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
                                    D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &device);
    if (FAILED(hr)) {
        printf("hardware CreateDevice failed (hr=0x%08X), trying software vertex processing\n",
               (unsigned)hr);
        hr = d3d9->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
                                D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &device);
    }
    if (FAILED(hr)) {
        printf("CreateDevice failed (hr=0x%08X)\n", (unsigned)hr);
        d3d9->Release();
        return 4;
    }
    printf("CreateDevice ok: %ux%u windowed=%d\n", pp.BackBufferWidth, pp.BackBufferHeight,
           pp.Windowed ? 1 : 0);

    WriteState(statePath, hwnd, device);
    printf("state written to %s\n", statePath);
    fflush(stdout);

    const DWORD start     = GetTickCount();
    const DWORD deadline  = start + durationMs;
    const DWORD resetTime = start + (durationMs / 3);
    MSG msg;
    for (;;) {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) goto done;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (durationMs != 0 && (LONG)(GetTickCount() - deadline) >= 0) break;

        // Mid-run, ask to go back to exclusive fullscreen the way the game's
        // own video options do. The proxy has to turn this around too, not just
        // the initial CreateDevice.
        if (!g_resetAttempted && durationMs != 0 && (LONG)(GetTickCount() - resetTime) >= 0) {
            g_resetAttempted = TRUE;
            D3DPRESENT_PARAMETERS resetParams = asRequested;
            g_resetHr = device->Reset(&resetParams);
            printf("mid-run Reset(Windowed=FALSE %dx%d) -> hr=0x%08X, got %ux%u windowed=%d\n",
                   kRequestedWidth, kRequestedHeight, (unsigned)g_resetHr, resetParams.BackBufferWidth,
                   resetParams.BackBufferHeight, resetParams.Windowed ? 1 : 0);
            fflush(stdout);
            if (FAILED(g_resetHr)) break;
        }

        if (device->TestCooperativeLevel() == D3DERR_DEVICENOTRESET) {
            printf("device lost, resetting\n");
            device->Reset(&pp);
        }
        device->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
                      D3DCOLOR_XRGB(24, 96, 160), 1.0f, 0);
        if (SUCCEEDED(device->BeginScene())) {
            device->EndScene();
        }
        device->Present(NULL, NULL, NULL, NULL);
    }

done:
    // Re-read the geometry at exit: a proxy that sets things up and then loses
    // the fight would show up here and not in the first snapshot.
    WriteState(statePath, hwnd, device);
    device->Release();
    d3d9->Release();
    DestroyWindow(hwnd);
    printf("done\n");
    return 0;
}
