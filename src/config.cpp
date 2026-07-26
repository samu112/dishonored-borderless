#include "config.h"

static Config g_cfg;
static BOOL   g_loaded = FALSE;

static const char* kSection = "Borderless";

static LogLevel ParseLevel(const char* s)
{
    if (lstrcmpiA(s, "off") == 0)   return LOG_OFF;
    if (lstrcmpiA(s, "error") == 0) return LOG_ERROR;
    if (lstrcmpiA(s, "debug") == 0) return LOG_DEBUG;
    if (lstrcmpiA(s, "trace") == 0) return LOG_TRACE;
    return LOG_INFO;
}

static BOOL ReadBool(const char* key, BOOL fallback)
{
    return GetPrivateProfileIntA(kSection, key, fallback ? 1 : 0, g_cfg.iniPath) != 0;
}

void Config_Load(HINSTANCE self)
{
    if (g_loaded) return;
    g_loaded = TRUE;

    // Defaults, used verbatim when the .ini is absent.
    g_cfg.enabled                 = TRUE;
    g_cfg.resolutionWidth         = 0;
    g_cfg.resolutionHeight        = 0;
    g_cfg.targetMonitorIndex      = 0;
    g_cfg.enableWndProcHook       = TRUE;
    g_cfg.forceWindowed           = TRUE;
    g_cfg.blockMinimizeOnFocusLoss= TRUE;
    g_cfg.topmost                 = FALSE;
    g_cfg.setDpiAwareness         = TRUE;
    g_cfg.enforceIntervalMs       = 250;
    g_cfg.logLevel                = LOG_INFO;
    lstrcpyA(g_cfg.logFile, "DishonoredBorderless.log");

    char path[MAX_PATH];
    if (!GetModuleFileNameA(self, path, MAX_PATH)) {
        g_cfg.iniPath[0] = '\0';
        g_cfg.iniFound   = FALSE;
        return;
    }
    for (int i = lstrlenA(path) - 1; i >= 0; --i) {
        if (path[i] == '\\' || path[i] == '/') { path[i + 1] = '\0'; break; }
    }
    lstrcpynA(g_cfg.iniPath, path, MAX_PATH);
    lstrcatA(g_cfg.iniPath, "DishonoredBorderless.ini");
    g_cfg.iniFound = GetFileAttributesA(g_cfg.iniPath) != INVALID_FILE_ATTRIBUTES;
    if (!g_cfg.iniFound) return;

    g_cfg.enabled                  = ReadBool("Enabled", g_cfg.enabled);
    g_cfg.resolutionWidth          = (int)GetPrivateProfileIntA(kSection, "ResolutionWidth", 0, g_cfg.iniPath);
    g_cfg.resolutionHeight         = (int)GetPrivateProfileIntA(kSection, "ResolutionHeight", 0, g_cfg.iniPath);
    g_cfg.targetMonitorIndex       = (int)GetPrivateProfileIntA(kSection, "TargetMonitorIndex", 0, g_cfg.iniPath);
    g_cfg.enableWndProcHook        = ReadBool("EnableWndProcHook", g_cfg.enableWndProcHook);
    g_cfg.forceWindowed            = ReadBool("ForceWindowed", g_cfg.forceWindowed);
    // MatchBackBufferToWindow is intentionally not read: removed in 1.0.1.
    g_cfg.blockMinimizeOnFocusLoss = ReadBool("BlockMinimizeOnFocusLoss", g_cfg.blockMinimizeOnFocusLoss);
    g_cfg.topmost                  = ReadBool("Topmost", g_cfg.topmost);
    g_cfg.setDpiAwareness          = ReadBool("SetDpiAwareness", g_cfg.setDpiAwareness);
    g_cfg.enforceIntervalMs        = GetPrivateProfileIntA(kSection, "EnforceIntervalMs", 250, g_cfg.iniPath);

    char level[32] = "info";
    GetPrivateProfileStringA(kSection, "LogLevel", "info", level, sizeof(level), g_cfg.iniPath);
    g_cfg.logLevel = ParseLevel(level);

    GetPrivateProfileStringA(kSection, "LogFile", "DishonoredBorderless.log", g_cfg.logFile,
                             sizeof(g_cfg.logFile), g_cfg.iniPath);

    if (g_cfg.resolutionWidth < 0)  g_cfg.resolutionWidth = 0;
    if (g_cfg.resolutionHeight < 0) g_cfg.resolutionHeight = 0;
    if (g_cfg.targetMonitorIndex < 0) g_cfg.targetMonitorIndex = 0;
    if (g_cfg.enforceIntervalMs && g_cfg.enforceIntervalMs < 50) g_cfg.enforceIntervalMs = 50;
}

const Config& Cfg() { return g_cfg; }
