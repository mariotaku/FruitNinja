#ifndef FN_PLATFORM_SAVEDIRSDL_H
#define FN_PLATFORM_SAVEDIRSDL_H

// Port specific: single shared save_dir resolver for every SDL-backed
// platform (host, webOS, Emscripten). Both the pre-init settings load
// (mainSDL.cpp, before Game::init runs) and Game::init itself (GameSDL.cpp)
// call this so they can never disagree on the writable save directory --
// see the drift-risk comments they used to carry individually.
//
// Returns the writable save directory (no trailing slash), CREATING it if
// it doesn't already exist. Callers join with "/" + filename (FruitSaveData,
// ItemManager, SettingsSave path helpers).
//
// Test-only override: if the FN_SAVE_DIR_OVERRIDE env var is set (non-empty),
// it wins over every platform branch below and is returned as-is (directory
// created if missing). The real game never sets this var, so production
// save location is unchanged; tests/test_harness.h sets a fresh per-test
// directory before calling Game::init() so no test can read or write the
// machine-global save (see task #124 -- a leftover active powerup in the
// shared SDL_GetPrefPath save silently changed later tests' dt scaling).
// Distinct name from the Wii-only compile-time FN_SAVE_DIR macro
// (src/config.h.in) -- unrelated mechanisms, different platforms.
//
// Per platform (when FN_SAVE_DIR_OVERRIDE is not set):
//   Emscripten     -> "/save" (IDBFS mount; see mainEmscripten.cpp)
//   webOS          -> "<appDir>/Save" (writable under the dev-mode install
//                      dir; appDir comes from fn_webos_app_dir())
//   host/other SDL -> SDL_GetPrefPath("Halfbrick", "FruitNinja") (SDL
//                      creates this directory itself), falling back to
//                      appDir if SDL_GetPrefPath fails.
//
// Not used on Wii -- GameWii.cpp sets save_dir = FN_SAVE_DIR directly (GX
// backend, no SDL).
#include <string>

std::string Mortar_ResolveSaveDir(const char* appDir);

#endif // FN_PLATFORM_SAVEDIRSDL_H
