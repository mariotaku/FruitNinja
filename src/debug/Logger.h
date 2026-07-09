#pragma once
#ifndef FN_DEBUG_LOGGER_H
#define FN_DEBUG_LOGGER_H

#ifndef __bada__

namespace Debug {
    enum LogLevel { LogLevel_Verbose = 0, LogLevel_Debug, LogLevel_Info, LogLevel_Warn, LogLevel_Error };

    // Free-running sim-tick counter, incremented once per fixed 1/60s sim
    // step (Game::stepUpdate(), src/GameSDL.cpp -- *SDL.cpp, excluded from
    // the __bada__ cross-build). Read by the log output callback
    // (FnSdlLogToStdout in mainSDL.cpp / mainEmscripten.cpp) to prefix every
    // line with "[NNNNNN]". Not reset on new-game; reflects ticks since
    // process start.
    extern unsigned int g_LogTick;

#ifdef __GNUC__
    void Log(LogLevel level, const char* tag, const char* fmt, ...)
        __attribute__((format(printf, 3, 4)));
#else
    void Log(LogLevel level, const char* tag, const char* fmt, ...);
#endif
}

#define LOG_VERBOSE(tag, fmt, ...) ::Debug::Log(::Debug::LogLevel_Verbose, (tag), (fmt), ##__VA_ARGS__)
#define LOG_DEBUG(tag, fmt, ...)   ::Debug::Log(::Debug::LogLevel_Debug,   (tag), (fmt), ##__VA_ARGS__)
#define LOG_INFO(tag, fmt, ...)    ::Debug::Log(::Debug::LogLevel_Info,    (tag), (fmt), ##__VA_ARGS__)
#define LOG_WARN(tag, fmt, ...)    ::Debug::Log(::Debug::LogLevel_Warn,    (tag), (fmt), ##__VA_ARGS__)
#define LOG_ERROR(tag, fmt, ...)   ::Debug::Log(::Debug::LogLevel_Error,   (tag), (fmt), ##__VA_ARGS__)

#else // __bada__

#define LOG_VERBOSE(tag, fmt, ...) ((void)0)
#define LOG_DEBUG(tag, fmt, ...)   ((void)0)
#define LOG_INFO(tag, fmt, ...)    ((void)0)
#define LOG_WARN(tag, fmt, ...)    ((void)0)
#define LOG_ERROR(tag, fmt, ...)   ((void)0)

#endif // __bada__

#endif // FN_DEBUG_LOGGER_H
