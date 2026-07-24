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
// Per platform:
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
