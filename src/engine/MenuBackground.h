// Analysed: 2026-04-25T14:45
//
// MenuBackground — background texture management for the menu/game screen.
// Binary source file: MenuBackground.cpp (_ZN14MenuBackground* symbols).
//
// ChangeBackground @ 0x0016ae8c
//   Selects the background texture by name, appends platform suffix, and
//   loads via TextureManager::LoadLocalisedTexture into g_BackgroundTexture.
//   Binary thunk @ 0x000f9708 (via GOT).
//
// GetCurrentBackground @ 0x0016af28
//   Returns the currently loaded background texture (reads g_BackgroundTexture).
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

// ChangeBackground @ 0x0016ae8c
// Loads the named background texture (e.g. "gb_game") into the global slot.
// If texName == NULL, uses the default "gb_game" (rodata 0x001bc79d).
// Appends ".tex" (fast hardware) or "_sml.tex" (slow) and calls
// TextureManager::LoadLocalisedTexture.
void ChangeBackground(const char* texName);

// GetCurrentBackground @ 0x0016af28
// Returns a raw pointer to the currently loaded background texture.
// Returns NULL if no background has been loaded yet.
Mortar::Texture* GetCurrentBackground();

// UnloadBackground — port-only cleanup hook. Nulls the file-static
// g_BackgroundTexture SmartPtr. Called from GameDestroy so the GL
// resource is released on shutdown.
void UnloadBackground();

#endif // FN_MENU_BACKGROUND_H
