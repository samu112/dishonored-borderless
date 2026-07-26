// Minimal, dependency-free logger for the d3d9 proxy.
//
// Deliberately avoids the C++ standard library and CRT stdio: this DLL is
// loaded into a 32-bit game process, and every non-system DLL it drags in
// (libc++, libunwind, libwinpthread) is another file that has to sit next to
// the game or it silently fails to launch. Formatting uses wvsprintfA from
// user32, which is always resident.
#pragma once

#include <windows.h>

enum LogLevel {
    LOG_OFF   = 0,
    LOG_ERROR = 1,
    LOG_INFO  = 2,
    LOG_DEBUG = 3,
    LOG_TRACE = 4,
};

// Opens the log file next to the DLL, falling back to %LOCALAPPDATA% when the
// game directory is not writable. Safe to call from DllMain.
void Log_Init(HINSTANCE self);
void Log_Shutdown();
void Log_SetLevel(LogLevel level);
LogLevel Log_GetLevel();

// wvsprintfA formatting: supports %s %d %u %x %c, NOT %p or %f.
void Log_Write(LogLevel level, const char* fmt, ...);

#define LOGE(...) Log_Write(LOG_ERROR, __VA_ARGS__)
#define LOGI(...) Log_Write(LOG_INFO, __VA_ARGS__)
#define LOGD(...) Log_Write(LOG_DEBUG, __VA_ARGS__)
#define LOGT(...) Log_Write(LOG_TRACE, __VA_ARGS__)
