#ifndef __bada__

#include "debug/Logger.h"
#include <SDL.h>
#include <cstdio>
#include <cstdarg>

namespace Debug {

unsigned int g_LogTick = 0;

static SDL_LogPriority ToSDLPriority(LogLevel level) {
    switch (level) {
        case LogLevel_Verbose: return SDL_LOG_PRIORITY_VERBOSE;
        case LogLevel_Debug:   return SDL_LOG_PRIORITY_DEBUG;
        case LogLevel_Info:    return SDL_LOG_PRIORITY_INFO;
        case LogLevel_Warn:    return SDL_LOG_PRIORITY_WARN;
        case LogLevel_Error:   return SDL_LOG_PRIORITY_ERROR;
        default:               return SDL_LOG_PRIORITY_INFO;
    }
}

void Log(LogLevel level, const char* tag, const char* fmt, ...) {
    char msgbuf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(msgbuf, sizeof(msgbuf), fmt, ap);
    va_end(ap);
    SDL_LogMessage(SDL_LOG_CATEGORY_APPLICATION, ToSDLPriority(level),
                   "[%s] %s", tag, msgbuf);
}

} // namespace Debug

#endif // !__bada__
