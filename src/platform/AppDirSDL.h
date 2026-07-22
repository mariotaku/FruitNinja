#ifndef FN_PLATFORM_APPDIRSDL_H
#define FN_PLATFORM_APPDIRSDL_H

// Port specific: webOS-only helper. Resolves the directory the running
// binary lives in, used to derive data_dir/save_dir at runtime (see
// Game::init in GameSDL.cpp and its call in mainSDL.cpp) instead of trusting
// FN_DATA_DIR (a compile-time path) or the process CWD -- webOS launches
// native apps with an unreliable/unspecified CWD, so neither is safe there.
//
// Only declared/defined under FRUIT_PLATFORM_WEBOS; callers must guard their
// own use the same way (see the #if defined(FRUIT_PLATFORM_WEBOS) blocks in
// GameSDL.cpp / mainSDL.cpp).
#if defined(FRUIT_PLATFORM_WEBOS)

#include <string>

// Returns the directory containing the current executable (no trailing
// slash), resolved via /proc/self/exe. Falls back to "." if unreadable.
std::string fn_webos_app_dir();

#endif // FRUIT_PLATFORM_WEBOS

#endif // FN_PLATFORM_APPDIRSDL_H
