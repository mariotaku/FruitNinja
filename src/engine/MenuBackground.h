//
// MenuBackground — background texture management for the menu/game screen.
// Binary source file: MenuBackground.cpp (_ZN14MenuBackground* symbols).
//
// ChangeBackground (v1.6.1) @ 0x001cc938
//   Selects the background texture by name, appends platform suffix, and
//   loads via TextureManager::LoadLocalisedTexture into g_BackgroundTexture.
//
// GetCurrentBackground (v1.6.1) — address TBD (TODO: confirm via search_functions)
//   Returns the currently loaded background texture (reads g_BackgroundTexture).
//
// UpdateBackground (v1.6.1) @ 0x001cc9f0
//   Reads the currently equipped background item and calls ChangeBackground().
//
// g_BackgroundTexture (file-static) — binary BSS 0x231500
//   (_ZL17backgroundTexture = Mortar::SmartPtr<Mortar::Texture>)
//

#ifndef FN_MENU_BACKGROUND_H
#define FN_MENU_BACKGROUND_H

#include "util/SmartPtr.h"
#include "asset/Texture.h"

// IsFastHardware — binary: _Z14IsFastHardwarev @0x0011f394 (v1.6.1)
// Returns true if the hardware is fast (always true in port; no slow-hardware path on SDL2).
bool IsFastHardware();

// ChangeBackground (v1.6.1) @ 0x001cc938
// Loads the named background texture (e.g. "gb_game") into the global slot.
// If texName == NULL, uses the default "gb_game" (rodata 0x001bc79d).
// Appends ".tex" (fast hardware) or "_sml.tex" (slow) and calls
// TextureManager::LoadLocalisedTexture.
void ChangeBackground(const char* texName);

// GetCurrentBackground (v1.6.1) — address TBD
// Returns a raw pointer to the currently loaded background texture.
// Returns NULL if no background has been loaded yet.
Mortar::Texture* GetCurrentBackground();

// UpdateBackground (v1.6.1) @ 0x001cc9f0
// Reads ItemManager::GetEquipped(ITEM_TYPE_BACKGROUND)->m_pTextureName and
// calls ChangeBackground() to sync the rendered background with the equipped item.
// Called each game-draw frame so shop equips take effect immediately.
void UpdateBackground();

// UnloadBackground — port-only cleanup hook. Nulls the file-static
// g_BackgroundTexture SmartPtr so its GL texture name is released while the GL
// context is still current.
//
// Called from BOTH:
//   - GameExit (v1.6.1 GameExit @0x001cfed4 step 1) -- the per-session teardown.
//   - GameDestroy -- still required: GameExit only runs when a task state was
//     live (GameTaskExit gates on GameTaskState::initialized), and quitting
//     during the splash dispatches SplashExit instead. Without the GameDestroy
//     call the background texture could survive into atexit, i.e. past
//     SDL_GL_DeleteContext.
// Idempotent; ChangeBackground re-loads on demand.
void UnloadBackground();

#endif // FN_MENU_BACKGROUND_H
