#ifndef FN_ENGINE_UTIL_SLOWIO_H
#define FN_ENGINE_UTIL_SLOWIO_H

// Port specific: dev/test enhancement (no binary counterpart). Simulates slow
// SD-card IO on fast host/web targets so the loading UX (spinners, preload
// stalls, etc.) can be exercised without real slow storage.
//
// Gated fully OFF by default -- fn_simulate_slow_io() is a no-op unless the
// build was configured with -DFN_SIMULATE_SLOW_IO=ON (see the root
// CMakeLists.txt option block), in which case both read funnels
// (FileSystem_Direct::GetFileData in FileSystemPosix.cpp/FileSystemWin32.cpp,
// and IFile_Direct::Read in IFile_Direct.cpp) call this after their fread so
// every asset load funnel is covered identically.
//
// Delay model: bytes / (FN_SLOW_IO_KBPS * 1024) seconds of throughput-
// proportional delay, plus a fixed FN_SLOW_IO_LATENCY_US per-read latency
// (simulates seek/command overhead independent of transfer size).

#if defined(FN_SIMULATE_SLOW_IO)

#include <cstddef>
#include <thread>
#include <chrono>

#ifndef FN_SLOW_IO_KBPS
#define FN_SLOW_IO_KBPS 2048
#endif
#ifndef FN_SLOW_IO_LATENCY_US
#define FN_SLOW_IO_LATENCY_US 0
#endif

inline void fn_simulate_slow_io(size_t bytes) {
    long long throughputUs = (long long)((double)bytes / ((double)FN_SLOW_IO_KBPS * 1024.0) * 1000000.0);
    long long totalUs = throughputUs + (long long)FN_SLOW_IO_LATENCY_US;
    if (totalUs > 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(totalUs));
    }
}

#else // !FN_SIMULATE_SLOW_IO

#include <cstddef>

inline void fn_simulate_slow_io(size_t /*bytes*/) {
    // Defunct: FN_SIMULATE_SLOW_IO disabled -- no-op stub.
}

#endif // FN_SIMULATE_SLOW_IO

#endif // FN_ENGINE_UTIL_SLOWIO_H
