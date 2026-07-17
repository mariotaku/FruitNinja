#ifndef __bada__

// Wii backend for the Debug::Log / LOG_* macros (see debug/Logger.h).
// SCAFFOLDING: writes to stdout, which the Homebrew Channel/Dolphin route to
// the console/gecko debug output depending on setup. No libogc-specific
// console API wired up yet -- see src/platform/wii/README.md.

#include "debug/Logger.h"
#include <cstdio>
#include <cstdarg>

namespace Debug {

unsigned int g_LogTick = 0;

static const char* LevelName(LogLevel level) {
    switch (level) {
        case LogLevel_Verbose: return "V";
        case LogLevel_Debug:   return "D";
        case LogLevel_Info:    return "I";
        case LogLevel_Warn:    return "W";
        case LogLevel_Error:   return "E";
        default:               return "?";
    }
}

void Log(LogLevel level, const char* tag, const char* fmt, ...) {
    char msgbuf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(msgbuf, sizeof(msgbuf), fmt, ap);
    va_end(ap);
    std::printf("[%s][%s] %s\n", LevelName(level), tag, msgbuf);
}

} // namespace Debug

#endif // !__bada__
