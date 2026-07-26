#include "window.h"

#include "config.h"
#include "log.h"

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

static HWND    g_hwnd      = NULL;
static WNDPROC g_origProc  = NULL;
static BOOL    g_unicode   = TRUE;
static LONG    g_enforcing = 0;   // guards re-entry via WM_SIZE/WM_WINDOWPOSCHANGING
static HANDLE  g_watchdog  = NULL;
static HANDLE  g_stopEvent = NULL;

static LRESULT CALLBACK BorderlessWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ---------------------------------------------------------------------------
// Style / geometry helpers
// ---------------------------------------------------------------------------

// Decoration bits only. WS_MINIMIZE/WS_MAXIMIZE are state, not decoration, and
// are deliberately left alone so we never lie to the game about its own state.
static const DWORD kDecoration =
    WS_CAPTION | WS_THICKFRAME | WS_BORDER | WS_DLGFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;

static const DWORD kExDecoration =
    WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_DLGMODALFRAME | WS_EX_STATICEDGE;

static DWORD SanitizeStyle(DWORD style)
{
    style &= ~kDecoration;
    style |= WS_POPUP | WS_CLIPSIBLINGS;
    return style;
}

static DWORD SanitizeExStyle(DWORD ex)
{
    ex &= ~kExDecoration;
    if (Cfg().topmost) ex |= WS_EX_TOPMOST;
    else               ex &= ~WS_EX_TOPMOST;
    return ex;
}

struct MonitorSearch {
    int   want;    // 1-based index
    int   seen;
    RECT  rect;
    BOOL  found;
};

static BOOL CALLBACK MonitorEnumProc(HMONITOR mon, HDC, LPRECT, LPARAM param)
{
    MonitorSearch* s = (MonitorSearch*)param;
    s->seen++;
    if (s->seen != s->want) return TRUE;

    MONITORINFO mi;
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoA(mon, &mi)) {
        s->rect  = mi.rcMonitor;
        s->found = TRUE;
    }
    return FALSE;
}

// Full-screen rect of the monitor selected by TargetMonitorIndex.
static BOOL GetMonitorRect(HWND hwnd, RECT* out)
{
    const Config& cfg = Cfg();

    if (cfg.targetMonitorIndex > 0) {
        MonitorSearch s;
        s.want  = cfg.targetMonitorIndex;
        s.seen  = 0;
        s.found = FALSE;
        SetRectEmpty(&s.rect);
        EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, (LPARAM)&s);
        if (s.found) {
            *out = s.rect;
            return TRUE;
        }
        LOGE("TargetMonitorIndex %d not found, falling back to the window's monitor",
             cfg.targetMonitorIndex);
    }

    HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi;
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoA(mon, &mi)) return FALSE;
    *out = mi.rcMonitor;
    return TRUE;
}

// Final window rect: the monitor rect, or a centred rect when the user pinned
// an explicit resolution.
static BOOL GetTargetRect(HWND hwnd, RECT* out)
{
    RECT mon;
    if (!GetMonitorRect(hwnd, &mon)) return FALSE;

    const Config& cfg = Cfg();
    const int monW = mon.right - mon.left;
    const int monH = mon.bottom - mon.top;

    int w = cfg.resolutionWidth  > 0 ? cfg.resolutionWidth  : monW;
    int h = cfg.resolutionHeight > 0 ? cfg.resolutionHeight : monH;
    if (w < 64) w = 64;
    if (h < 64) h = 64;

    out->left   = mon.left + (monW - w) / 2;
    out->top    = mon.top + (monH - h) / 2;
    out->right  = out->left + w;
    out->bottom = out->top + h;
    return TRUE;
}

BOOL Win_GetTargetSize(HWND hwnd, int* width, int* height)
{
    RECT rc;
    if (!GetTargetRect(hwnd ? hwnd : g_hwnd, &rc)) return FALSE;
    if (width)  *width  = rc.right - rc.left;
    if (height) *height = rc.bottom - rc.top;
    return TRUE;
}

// ---------------------------------------------------------------------------
// Enforcement
// ---------------------------------------------------------------------------

void Win_Enforce(BOOL async)
{
    HWND hwnd = g_hwnd;
    if (!hwnd || !IsWindow(hwnd)) return;
    if (!Cfg().enabled) return;

    // Our own SetWindowLong/SetWindowPos below re-enter this WndProc; the guard
    // keeps that from recursing.
    if (InterlockedCompareExchange(&g_enforcing, 1, 0) != 0) return;

    RECT target;
    if (GetTargetRect(hwnd, &target)) {
        const DWORD style   = (DWORD)GetWindowLongA(hwnd, GWL_STYLE);
        const DWORD exStyle = (DWORD)GetWindowLongA(hwnd, GWL_EXSTYLE);
        const DWORD wantStyle   = SanitizeStyle(style);
        const DWORD wantExStyle = SanitizeExStyle(exStyle);

        BOOL restyled = FALSE;
        if (style != wantStyle) {
            SetWindowLongA(hwnd, GWL_STYLE, (LONG)wantStyle);
            restyled = TRUE;
            LOGD("style %x -> %x", style, wantStyle);
        }
        if (exStyle != wantExStyle) {
            SetWindowLongA(hwnd, GWL_EXSTYLE, (LONG)wantExStyle);
            restyled = TRUE;
            LOGD("exstyle %x -> %x", exStyle, wantExStyle);
        }

        RECT current;
        GetWindowRect(hwnd, &current);
        if (restyled || !EqualRect(&current, &target)) {
            UINT flags = SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOCOPYBITS;
            if (restyled) flags |= SWP_FRAMECHANGED;
            if (async)    flags |= SWP_ASYNCWINDOWPOS;

            HWND after = Cfg().topmost ? HWND_TOPMOST : HWND_NOTOPMOST;
            SetWindowPos(hwnd, after, target.left, target.top, target.right - target.left,
                         target.bottom - target.top, flags);
            LOGD("enforced rect %d,%d %dx%d", target.left, target.top,
                 target.right - target.left, target.bottom - target.top);
        }
    }

    InterlockedExchange(&g_enforcing, 0);
}

// ---------------------------------------------------------------------------
// Window discovery (plan.md 4.3.1)
// ---------------------------------------------------------------------------

struct WindowSearch {
    DWORD pid;
    HWND  best;
};

static BOOL CALLBACK FindWindowProc(HWND hwnd, LPARAM param)
{
    WindowSearch* s = (WindowSearch*)param;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != s->pid) return TRUE;
    if (!IsWindowVisible(hwnd)) return TRUE;
    if (GetWindow(hwnd, GW_OWNER) != NULL) return TRUE;   // skip tool/owned windows

    RECT rc;
    if (!GetWindowRect(hwnd, &rc)) return TRUE;
    if (rc.right - rc.left < 64 || rc.bottom - rc.top < 64) return TRUE;

    s->best = hwnd;
    return FALSE;
}

HWND Win_FindGameWindow()
{
    WindowSearch s;
    s.pid  = GetCurrentProcessId();
    s.best = NULL;
    EnumWindows(FindWindowProc, (LPARAM)&s);
    return s.best;
}

HWND Win_GetWindow() { return g_hwnd; }

// ---------------------------------------------------------------------------
// Watchdog (plan.md 4.6)
// ---------------------------------------------------------------------------

static DWORD WINAPI WatchdogProc(LPVOID)
{
    const DWORD period = Cfg().enforceIntervalMs;
    LOGI("watchdog started (%u ms)", period);

    for (;;) {
        if (WaitForSingleObject(g_stopEvent, period) == WAIT_OBJECT_0) break;

        HWND hwnd = g_hwnd;
        if (!hwnd || !IsWindow(hwnd)) {
            // The game tore its window down and (probably) built a new one.
            HWND replacement = Win_FindGameWindow();
            if (replacement && replacement != hwnd) {
                LOGI("window recreated, re-attaching to %x", (unsigned)(UINT_PTR)replacement);
                g_hwnd     = NULL;
                g_origProc = NULL;
                Win_Attach(replacement);
            }
            continue;
        }

        // Something else may have replaced our subclass; put it back.
        WNDPROC installed = g_unicode
                                ? (WNDPROC)GetWindowLongPtrW(hwnd, GWLP_WNDPROC)
                                : (WNDPROC)GetWindowLongPtrA(hwnd, GWLP_WNDPROC);
        if (Cfg().enableWndProcHook && installed != BorderlessWndProc) {
            LOGI("subclass lost, reinstalling");
            g_origProc = NULL;
            Win_Attach(hwnd);
            continue;
        }

        Win_Enforce(TRUE);
    }

    LOGI("watchdog stopped");
    return 0;
}

static void StartWatchdog()
{
    if (g_watchdog || Cfg().enforceIntervalMs == 0) return;
    if (!g_stopEvent) g_stopEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    g_watchdog = CreateThread(NULL, 0, WatchdogProc, NULL, 0, NULL);
    if (!g_watchdog) LOGE("CreateThread for watchdog failed (%u)", GetLastError());
}

static void StopWatchdog()
{
    if (!g_watchdog) return;
    if (g_stopEvent) SetEvent(g_stopEvent);
    WaitForSingleObject(g_watchdog, 2000);
    CloseHandle(g_watchdog);
    g_watchdog = NULL;
}

// ---------------------------------------------------------------------------
// Subclass
// ---------------------------------------------------------------------------

static LRESULT CallOriginal(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (!g_origProc) return DefWindowProcW(hwnd, msg, wParam, lParam);
    return g_unicode ? CallWindowProcW(g_origProc, hwnd, msg, wParam, lParam)
                     : CallWindowProcA(g_origProc, hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK BorderlessWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (hwnd != g_hwnd || !Cfg().enabled) return CallOriginal(hwnd, msg, wParam, lParam);

    switch (msg) {
    case WM_NCCALCSIZE:
        // The client area is the whole window rect. This is what actually makes
        // the window borderless, and it survives the game re-adding WS_CAPTION
        // behind our back.
        return 0;

    case WM_STYLECHANGING: {
        // Intercepts the game's SetWindowLong(GWL_STYLE) at the source, so its
        // style is never applied and then visibly corrected.
        STYLESTRUCT* ss = (STYLESTRUCT*)lParam;
        if (ss) {
            if (wParam == (WPARAM)GWL_STYLE)        ss->styleNew = SanitizeStyle(ss->styleNew);
            else if (wParam == (WPARAM)GWL_EXSTYLE) ss->styleNew = SanitizeExStyle(ss->styleNew);
        }
        break;
    }

    case WM_WINDOWPOSCHANGING: {
        WINDOWPOS* wp = (WINDOWPOS*)lParam;
        RECT target;
        if (wp && !(wp->flags & SWP_HIDEWINDOW) && GetTargetRect(hwnd, &target)) {
            wp->x  = target.left;
            wp->y  = target.top;
            wp->cx = target.right - target.left;
            wp->cy = target.bottom - target.top;
            wp->flags &= ~(SWP_NOSIZE | SWP_NOMOVE);
        }
        break;
    }

    case WM_GETMINMAXINFO: {
        LRESULT     result = CallOriginal(hwnd, msg, wParam, lParam);
        MINMAXINFO* mmi    = (MINMAXINFO*)lParam;
        RECT        target;
        if (mmi && GetTargetRect(hwnd, &target)) {
            const int w = target.right - target.left;
            const int h = target.bottom - target.top;
            mmi->ptMinTrackSize.x = 1;
            mmi->ptMinTrackSize.y = 1;
            mmi->ptMaxTrackSize.x = w > mmi->ptMaxTrackSize.x ? w : mmi->ptMaxTrackSize.x;
            mmi->ptMaxTrackSize.y = h > mmi->ptMaxTrackSize.y ? h : mmi->ptMaxTrackSize.y;
            mmi->ptMaxSize.x      = w;
            mmi->ptMaxSize.y      = h;
            mmi->ptMaxPosition.x  = target.left;
            mmi->ptMaxPosition.y  = target.top;
        }
        return result;
    }

    case WM_SYSCOMMAND:
        // plan.md lines 30/38: the documented flicker/minimise symptom. Forcing
        // Windowed=TRUE removes D3D9's own auto-minimise, this covers the game
        // asking for it explicitly.
        if (Cfg().blockMinimizeOnFocusLoss && (wParam & 0xFFF0) == SC_MINIMIZE) {
            LOGD("swallowed SC_MINIMIZE");
            return 0;
        }
        break;

    case WM_SIZE:
    case WM_MOVE:
    case WM_ACTIVATE:
    case WM_ACTIVATEAPP:
    case WM_SETFOCUS:
    case WM_DISPLAYCHANGE:
    case WM_DPICHANGED: {
        LRESULT result = CallOriginal(hwnd, msg, wParam, lParam);
        Win_Enforce(FALSE);
        return result;
    }

    case WM_NCDESTROY: {
        LOGI("window %x destroyed", (unsigned)(UINT_PTR)hwnd);
        LRESULT result = CallOriginal(hwnd, msg, wParam, lParam);
        if (g_origProc) {
            if (g_unicode) SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)g_origProc);
            else           SetWindowLongPtrA(hwnd, GWLP_WNDPROC, (LONG_PTR)g_origProc);
        }
        g_hwnd     = NULL;
        g_origProc = NULL;
        return result;
    }

    default:
        break;
    }

    return CallOriginal(hwnd, msg, wParam, lParam);
}

void Win_Attach(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd)) {
        LOGE("Win_Attach called with an invalid window");
        return;
    }
    if (!Cfg().enabled) return;

    if (g_hwnd == hwnd && g_origProc) {
        Win_Enforce(FALSE);
        return;
    }

    g_hwnd    = hwnd;
    g_unicode = IsWindowUnicode(hwnd);

    char cls[128] = "";
    GetClassNameA(hwnd, cls, sizeof(cls));
    LOGI("attaching to window %x (class '%s', %s)", (unsigned)(UINT_PTR)hwnd, cls,
         g_unicode ? "unicode" : "ansi");

    if (Cfg().enableWndProcHook) {
        WNDPROC previous = g_unicode
                               ? (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)BorderlessWndProc)
                               : (WNDPROC)SetWindowLongPtrA(hwnd, GWLP_WNDPROC, (LONG_PTR)BorderlessWndProc);
        if (!previous) {
            LOGE("SetWindowLongPtr(GWLP_WNDPROC) failed (%u)", GetLastError());
        } else if (previous != BorderlessWndProc) {
            g_origProc = previous;
        }
    } else {
        LOGI("EnableWndProcHook=0: geometry is maintained by the watchdog only");
    }

    Win_Enforce(FALSE);
    StartWatchdog();
}

void Win_Detach()
{
    StopWatchdog();

    HWND hwnd = g_hwnd;
    if (hwnd && IsWindow(hwnd) && g_origProc) {
        if (g_unicode) SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)g_origProc);
        else           SetWindowLongPtrA(hwnd, GWLP_WNDPROC, (LONG_PTR)g_origProc);
    }
    g_hwnd     = NULL;
    g_origProc = NULL;

    if (g_stopEvent) {
        CloseHandle(g_stopEvent);
        g_stopEvent = NULL;
    }
}
