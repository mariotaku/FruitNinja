// Port specific: see SaveDirSDL.h for the contract/rationale.
#include "SaveDirSDL.h"

#include <SDL.h>

#if defined(FRUIT_PLATFORM_WEBOS)
#include "AppDirSDL.h"
#endif

#if defined(_WIN32)
#include <direct.h>      // _mkdir on Windows
#else
#include <sys/stat.h>    // mkdir on POSIX / Emscripten
#endif

namespace {

void fn_mkdir(const std::string& path) {
#if defined(_WIN32)
    _mkdir(path.c_str());
#else
    mkdir(path.c_str(), 0755);
#endif
}

} // namespace

std::string Mortar_ResolveSaveDir(const char* appDir) {
#if defined(__EMSCRIPTEN__)
    (void)appDir;
    // IDBFS mount -- see mainEmscripten.cpp; mounted (and FS.mkdir'd) there
    // before Game::init runs, so no fn_mkdir needed here.
    return std::string("/save");
#elif defined(FRUIT_PLATFORM_WEBOS)
    std::string dir = std::string(appDir) + "/Save";
    fn_mkdir(dir);
    return dir;
#else
    char* p = SDL_GetPrefPath("Halfbrick", "FruitNinja"); // SDL creates it
    std::string s = p ? std::string(p) : std::string(appDir);
    if (p) SDL_free(p);
    // Strip the trailing slash SDL_GetPrefPath appends, so callers can
    // uniformly join with "/" + filename without a double slash.
    if (!s.empty() && (s[s.size() - 1] == '/' || s[s.size() - 1] == '\\')) {
        s.erase(s.size() - 1);
    }
    return s;
#endif
}
