#ifndef FN_CRASH_HANDLER_H
#define FN_CRASH_HANDLER_H

// Win32 SetUnhandledExceptionFilter + DbgHelp stack walker.
// Debug + Windows only -- elsewhere this is a no-op.
//
// Call once near the top of main(). On unhandled SEH exception (AV,
// stack overflow, illegal instruction, etc.) the handler prints the
// exception code, faulting address, and a symbolised stack trace to
// stderr before letting the OS terminate the process.

namespace FN {
void InstallCrashHandler();
}

#endif
