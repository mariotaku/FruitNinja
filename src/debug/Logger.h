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

#ifdef FN_LOG_ERRORS_ONLY

// Port specific: errors-only builds (web Release).  Everything below Error
// compiles to nothing, so no string is formatted and no call is made.
//
// Why: on web every surviving log line reaches the browser console, and
// Chrome DevTools captures a stack trace for each one while the Performance
// panel is recording -- ~230 lines cost ~650ms in a 26s profile, more than any
// single GL call, which distorted a profile badly enough to misdirect
// optimisation work.  Stripping at compile time removes the cost outright
// rather than managing it in the shell.
//
// LOG_ERROR is deliberately kept: errors are rare, and a silent web build is
// far harder to diagnose than a slightly noisy one.  Debug web builds
// (FN_WEB_DEBUG) do not define this and keep every tier.
#define LOG_VERBOSE(tag, fmt, ...) ((void)0)
#define LOG_DEBUG(tag, fmt, ...)   ((void)0)
#define LOG_INFO(tag, fmt, ...)    ((void)0)
#define LOG_WARN(tag, fmt, ...)    ((void)0)
#define LOG_ERROR(tag, fmt, ...)   ::Debug::Log(::Debug::LogLevel_Error,   (tag), (fmt), ##__VA_ARGS__)

#else // !FN_LOG_ERRORS_ONLY

#define LOG_VERBOSE(tag, fmt, ...) ::Debug::Log(::Debug::LogLevel_Verbose, (tag), (fmt), ##__VA_ARGS__)
#define LOG_DEBUG(tag, fmt, ...)   ::Debug::Log(::Debug::LogLevel_Debug,   (tag), (fmt), ##__VA_ARGS__)
#define LOG_INFO(tag, fmt, ...)    ::Debug::Log(::Debug::LogLevel_Info,    (tag), (fmt), ##__VA_ARGS__)
#define LOG_WARN(tag, fmt, ...)    ::Debug::Log(::Debug::LogLevel_Warn,    (tag), (fmt), ##__VA_ARGS__)
#define LOG_ERROR(tag, fmt, ...)   ::Debug::Log(::Debug::LogLevel_Error,   (tag), (fmt), ##__VA_ARGS__)

#endif // FN_LOG_ERRORS_ONLY

#else // __bada__

#define LOG_VERBOSE(tag, fmt, ...) ((void)0)
#define LOG_DEBUG(tag, fmt, ...)   ((void)0)
#define LOG_INFO(tag, fmt, ...)    ((void)0)
#define LOG_WARN(tag, fmt, ...)    ((void)0)
#define LOG_ERROR(tag, fmt, ...)   ((void)0)

#endif // __bada__

#endif // FN_DEBUG_LOGGER_H
