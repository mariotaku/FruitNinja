// Analysed: 2026-04-25T14:45
//
// MenuBackground — ChangeBackground + GetCurrentBackground.
// Binary: ChangeBackground @ 0x0016ae8c, GetCurrentBackground @ 0x0016af28.
// File-static g_BackgroundTexture (Mortar::SmartPtr<Mortar::Texture>) at BSS 0x231500
//   (_ZL17backgroundTexture, GOT+0x000452d4+0xfc).
// See docs/structs/items.md §ChangeBackground for full RE notes.

#include "MenuBackground.h"
#include "asset/TextureManager.h"
#include <cstdio>
#include <cstring>

// File-static background texture — binary BSS 0x231500 (_ZL17backgroundTexture).
// Binary: Mortar::SmartPtr<Texture> at _ZL17backgroundTexture.
static Mortar::SmartPtr<Mortar::Texture> g_BackgroundTexture;

// IsFastHardware — binary: reads a hardware-capability flag set during
// platform init. Port: stub returning true (no slow-hardware path needed).
// Binary: the slow branch appends "_sml" suffix (rodata 0x001bc7a5).
static bool IsFastHardware() {
    return true;
}

// ChangeBackground @ 0x0016ae8c
// Binary behaviour:
//   bool fast = IsFastHardware();
//   if (texName == NULL) texName = "gb_game";  // 0x001bc79d
//   const char* suffix = fast ? "" : "_sml";   // fast: 0x001bda4c; slow: 0x001bc7a5
//   char buf[64]; OS_SPrintf(buf, 64, "%s%s.tex", texName, suffix);  // fmt 0x001bc7aa
//   Mortar::SmartPtr<Texture> tmp; TextureManager::LoadLocalisedTexture(&tmp, buf);
//   g_backgroundTexture = tmp;
void ChangeBackground(const char* texName) {
    bool fast = IsFastHardware();
    if (texName == NULL) {
        texName = "gb_game";  // rodata 0x001bc79d — default background name
    }
    const char* suffix = fast ? "" : "_sml";  // fast: empty (0x001bda4c); slow: "_sml" (0x001bc7a5)
    char buf[64];
    snprintf(buf, sizeof(buf), "%s%s.tex", texName, suffix);  // fmt 0x001bc7aa
    Mortar::SmartPtr<Mortar::Texture> tmp = Mortar::TextureManager::LoadLocalisedTexture(buf);
    g_BackgroundTexture = tmp;
}

// GetCurrentBackground @ 0x0016af28
// Reads back from the same file-static slot. Returns raw pointer (NULL if empty).
Mortar::Texture* GetCurrentBackground() {
    return g_BackgroundTexture.Get();
}

void UnloadBackground() {
    g_BackgroundTexture.SetNull();
}
