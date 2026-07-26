#include "log.h"

#include "config.h"

static HANDLE           g_file  = INVALID_HANDLE_VALUE;
static LogLevel         g_level = LOG_INFO;
static CRITICAL_SECTION g_lock;
static BOOL             g_lockReady = FALSE;

static const char* LevelTag(LogLevel level)
{
    switch (level) {
    case LOG_ERROR: return "ERROR";
    case LOG_INFO:  return "info ";
    case LOG_DEBUG: return "debug";
    case LOG_TRACE: return "trace";
    default:        return "?????";
    }
}

static HANDLE OpenLogAt(const char* dir, const char* name)
{
    char path[MAX_PATH];
    lstrcpynA(path, dir, MAX_PATH);
    int len = lstrlenA(path);
    if (len > 0 && path[len - 1] != '\\' && len + 1 < MAX_PATH) {
        path[len]     = '\\';
        path[len + 1] = '\0';
    }
    lstrcatA(path, name);

    HANDLE h = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        OutputDebugStringA("[DishonoredBorderless] logging to ");
        OutputDebugStringA(path);
        OutputDebugStringA("\n");
    }
    return h;
}

void Log_Init(HINSTANCE self)
{
    if (!g_lockReady) {
        InitializeCriticalSection(&g_lock);
        g_lockReady = TRUE;
    }
    if (g_file != INVALID_HANDLE_VALUE) return;

    const Config& cfg = Cfg();
    g_level = cfg.logLevel;
    if (g_level == LOG_OFF) return;

    // Preferred location: beside the DLL, i.e. the game's Binaries\Win32.
    char dir[MAX_PATH];
    if (GetModuleFileNameA(self, dir, MAX_PATH)) {
        for (int i = lstrlenA(dir) - 1; i >= 0; --i) {
            if (dir[i] == '\\' || dir[i] == '/') { dir[i] = '\0'; break; }
        }
        g_file = OpenLogAt(dir, cfg.logFile);
    }

    // Steam installs under Program Files are not always user-writable.
    if (g_file == INVALID_HANDLE_VALUE) {
        char local[MAX_PATH];
        DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", local, MAX_PATH);
        if (n > 0 && n < MAX_PATH) {
            lstrcatA(local, "\\DishonoredBorderless");
            CreateDirectoryA(local, NULL);
            g_file = OpenLogAt(local, cfg.logFile);
        }
    }

    if (g_file == INVALID_HANDLE_VALUE) {
        OutputDebugStringA("[DishonoredBorderless] could not open a log file\n");
        return;
    }

    SetFilePointer(g_file, 0, NULL, FILE_END);
    Log_Write(LOG_INFO, "----- DishonoredBorderless attached (pid %u) -----", GetCurrentProcessId());
}

void Log_Shutdown()
{
    if (!g_lockReady) return;
    EnterCriticalSection(&g_lock);
    if (g_file != INVALID_HANDLE_VALUE) {
        CloseHandle(g_file);
        g_file = INVALID_HANDLE_VALUE;
    }
    LeaveCriticalSection(&g_lock);
}

void Log_SetLevel(LogLevel level) { g_level = level; }
LogLevel Log_GetLevel() { return g_level; }

void Log_Write(LogLevel level, const char* fmt, ...)
{
    if (level > g_level || g_level == LOG_OFF) return;
    if (g_file == INVALID_HANDLE_VALUE) return;

    char body[1024];
    va_list args;
    va_start(args, fmt);
    wvsprintfA(body, fmt, args);
    va_end(args);

    SYSTEMTIME st;
    GetLocalTime(&st);

    char line[1280];
    wsprintfA(line, "[%02u:%02u:%02u.%03u] %s %s\r\n", st.wHour, st.wMinute, st.wSecond,
              st.wMilliseconds, LevelTag(level), body);

    if (g_lockReady) EnterCriticalSection(&g_lock);
    DWORD written = 0;
    WriteFile(g_file, line, (DWORD)lstrlenA(line), &written, NULL);
    if (g_lockReady) LeaveCriticalSection(&g_lock);
}
