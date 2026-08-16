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

static const char* PriorityName(SDL_LogPriority priority) {
    switch (priority) {
        case SDL_LOG_PRIORITY_VERBOSE:  return "VERBOSE";
        case SDL_LOG_PRIORITY_DEBUG:    return "DEBUG";
        case SDL_LOG_PRIORITY_INFO:     return "INFO";
        case SDL_LOG_PRIORITY_WARN:     return "WARN";
        case SDL_LOG_PRIORITY_ERROR:    return "ERROR";
        case SDL_LOG_PRIORITY_CRITICAL: return "CRITICAL";
        default:                        return "INFO";
    }
}

void Log(LogLevel level, const char* tag, const char* fmt, ...) {
    char msgbuf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(msgbuf, sizeof(msgbuf), fmt, ap);
    va_end(ap);

    SDL_LogPriority priority = ToSDLPriority(level);

    // Port specific: never call into SDL's logger before SDL_Init().
    // LG's webOS SDL2 (/usr/lib/libSDL2-2.0.so.0.2.1) routes SDL_LogMessageV
    // through PmLog, and that path unconditionally calls a function pointer in
    // .bss -- `PmLogGetContext`, dlsym'd from libPmLogLib.so.3 during SDL_Init.
    // Before SDL_Init that pointer is still NULL, so the `blx r3` jumps to 0 and
    // the process dies with SIGSEGV at pc=0 before printing anything. Confirmed
    // on-device (uh6100) with a minimal probe: SDL_LogInfo before SDL_Init
    // segfaults, after SDL_Init it is fine (default and custom output function
    // alike, and still fine after SDL_Quit).
    // Write the line ourselves in that window, in the same format the SDL output
    // function produces (FnSdlLogToStdout, mainSDL.cpp / mainEmscripten.cpp), so
    // pre-init logs are indistinguishable from post-init ones.
    if (SDL_WasInit(0) == 0) {
        std::fprintf(stdout, "[%06u][%s][%s] %s\n", g_LogTick,
                     PriorityName(priority), tag, msgbuf);
        std::fflush(stdout);
        return;
    }

    SDL_LogMessage(SDL_LOG_CATEGORY_APPLICATION, priority,
                   "[%s] %s", tag, msgbuf);
}

} // namespace Debug

#endif // !__bada__
