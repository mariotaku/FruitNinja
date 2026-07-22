// Port specific: webOS-only. See AppDirSDL.h for the contract/rationale.
#if defined(FRUIT_PLATFORM_WEBOS)

#include "AppDirSDL.h"

#include <unistd.h>
#include <climits>

std::string fn_webos_app_dir() {
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) {
        return std::string(".");
    }
    buf[n] = '\0';
    std::string exePath(buf);
    size_t slash = exePath.find_last_of('/');
    if (slash == std::string::npos) return std::string(".");
    return exePath.substr(0, slash);
}

#endif // FRUIT_PLATFORM_WEBOS
