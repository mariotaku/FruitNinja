#include "CrashHandler.h"

#if defined(_WIN32) && defined(_DEBUG)

#include <windows.h>
#include <dbghelp.h>
#include <cstdio>
#include <cstdlib>

#pragma comment(lib, "dbghelp.lib")

namespace {

const char* ExceptionCodeName(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:         return "ACCESS_VIOLATION";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:    return "ARRAY_BOUNDS_EXCEEDED";
    case EXCEPTION_BREAKPOINT:               return "BREAKPOINT";
    case EXCEPTION_DATATYPE_MISALIGNMENT:    return "DATATYPE_MISALIGNMENT";
    case EXCEPTION_FLT_DENORMAL_OPERAND:     return "FLT_DENORMAL_OPERAND";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:       return "FLT_DIVIDE_BY_ZERO";
    case EXCEPTION_FLT_INEXACT_RESULT:       return "FLT_INEXACT_RESULT";
    case EXCEPTION_FLT_INVALID_OPERATION:    return "FLT_INVALID_OPERATION";
    case EXCEPTION_FLT_OVERFLOW:             return "FLT_OVERFLOW";
    case EXCEPTION_FLT_STACK_CHECK:          return "FLT_STACK_CHECK";
    case EXCEPTION_FLT_UNDERFLOW:            return "FLT_UNDERFLOW";
    case EXCEPTION_ILLEGAL_INSTRUCTION:      return "ILLEGAL_INSTRUCTION";
    case EXCEPTION_IN_PAGE_ERROR:            return "IN_PAGE_ERROR";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:       return "INT_DIVIDE_BY_ZERO";
    case EXCEPTION_INT_OVERFLOW:             return "INT_OVERFLOW";
    case EXCEPTION_INVALID_DISPOSITION:      return "INVALID_DISPOSITION";
    case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "NONCONTINUABLE_EXCEPTION";
    case EXCEPTION_PRIV_INSTRUCTION:         return "PRIV_INSTRUCTION";
    case EXCEPTION_SINGLE_STEP:              return "SINGLE_STEP";
    case EXCEPTION_STACK_OVERFLOW:           return "STACK_OVERFLOW";
    default:                                 return "UNKNOWN";
    }
}

void PrintStackTrace(CONTEXT* ctx, HANDLE process, HANDLE thread) {
    STACKFRAME64 frame = {};
    DWORD machine;
#if defined(_M_X64) || defined(__x86_64__)
    machine = IMAGE_FILE_MACHINE_AMD64;
    frame.AddrPC.Offset    = ctx->Rip;
    frame.AddrFrame.Offset = ctx->Rbp;
    frame.AddrStack.Offset = ctx->Rsp;
#elif defined(_M_IX86) || defined(__i386__)
    machine = IMAGE_FILE_MACHINE_I386;
    frame.AddrPC.Offset    = ctx->Eip;
    frame.AddrFrame.Offset = ctx->Ebp;
    frame.AddrStack.Offset = ctx->Esp;
#else
    fprintf(stderr, "  (stack walk: unsupported architecture)\n");
    return;
#endif
    frame.AddrPC.Mode    = AddrModeFlat;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Mode = AddrModeFlat;

    char symbol_buf[sizeof(SYMBOL_INFO) + 512];
    SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(symbol_buf);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen   = 511;

    IMAGEHLP_LINE64 line = {};
    line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

    for (int i = 0; i < 64; ++i) {
        if (!StackWalk64(machine, process, thread, &frame, ctx,
                         nullptr, SymFunctionTableAccess64,
                         SymGetModuleBase64, nullptr)) {
            break;
        }
        if (frame.AddrPC.Offset == 0) break;

        DWORD64 displ = 0;
        const char* name = "<no symbol>";
        if (SymFromAddr(process, frame.AddrPC.Offset, &displ, symbol)) {
            name = symbol->Name;
        }

        DWORD line_displ = 0;
        if (SymGetLineFromAddr64(process, frame.AddrPC.Offset, &line_displ, &line)) {
            fprintf(stderr, "  #%-2d 0x%016llx %s+0x%llx  (%s:%lu)\n",
                    i, (unsigned long long)frame.AddrPC.Offset, name,
                    (unsigned long long)displ, line.FileName, line.LineNumber);
        } else {
            fprintf(stderr, "  #%-2d 0x%016llx %s+0x%llx\n",
                    i, (unsigned long long)frame.AddrPC.Offset, name,
                    (unsigned long long)displ);
        }
    }
}

LONG WINAPI TopLevelFilter(EXCEPTION_POINTERS* ep) {
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    void* addr = ep->ExceptionRecord->ExceptionAddress;

    fprintf(stderr, "\n==================== UNHANDLED EXCEPTION ====================\n");
    fprintf(stderr, "code: 0x%08lx (%s)\n", code, ExceptionCodeName(code));
    fprintf(stderr, "addr: %p\n", addr);

    if (code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR) {
        ULONG_PTR op   = ep->ExceptionRecord->ExceptionInformation[0];
        ULONG_PTR vaddr = ep->ExceptionRecord->ExceptionInformation[1];
        const char* opname = (op == 0) ? "read" : (op == 1) ? "write" : (op == 8) ? "exec" : "?";
        fprintf(stderr, "fault: %s at 0x%016llx\n", opname, (unsigned long long)vaddr);
    }

    HANDLE process = GetCurrentProcess();
    HANDLE thread  = GetCurrentThread();

    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    if (!SymInitialize(process, nullptr, TRUE)) {
        fprintf(stderr, "  (SymInitialize failed: %lu)\n", GetLastError());
    }

    fprintf(stderr, "stack:\n");
    PrintStackTrace(ep->ContextRecord, process, thread);
    fprintf(stderr, "=============================================================\n");
    fflush(stderr);

    SymCleanup(process);
    return EXCEPTION_EXECUTE_HANDLER;
}

} // namespace

namespace FN {
void InstallCrashHandler() {
    SetUnhandledExceptionFilter(TopLevelFilter);
}
} // namespace FN

#else // not (Windows && Debug)

namespace FN {
void InstallCrashHandler() {}
} // namespace FN

#endif
