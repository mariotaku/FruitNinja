//
// MenuBackground — ChangeBackground + GetCurrentBackground + UpdateBackground.
// v1.6.1: ChangeBackground @ 0x001cc938, UpdateBackground @ 0x001cc9f0.
//   GetCurrentBackground address TBD (TODO: confirm via search_functions).
// File-static g_BackgroundTexture (Mortar::SmartPtr<Mortar::Texture>) at BSS 0x231500
//   (_ZL17backgroundTexture, GOT+0x000452d4+0xfc).

#include "MenuBackground.h"
#include "asset/TextureManager.h"
#include "game/ItemManager.h"
#include "Game.h"
#include <cstdio>
#include <cstring>

// File-static background texture — binary BSS 0x231500 (_ZL17backgroundTexture).
// Binary: Mortar::SmartPtr<Texture> at _ZL17backgroundTexture.
static Mortar::SmartPtr<Mortar::Texture> g_BackgroundTexture;

// IsFastHardware — binary: _Z14IsFastHardwarev @0x0011f394 (v1.6.1)
// Reads theGame's MortarGame::m_bFastHardware (+0xF4), set by
// Game::Init -> SetHardware("BADA", true) (Bada Wave is fast hardware).
// Binary: the slow branch appends "_sml" suffix (rodata 0x001bc7a5).
bool IsFastHardware() {
    return Game::GetInstance()->IsFastHardware();
}

// ChangeBackground (v1.6.1) @ 0x001cc938
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

// GetCurrentBackground (v1.6.1) — address TBD
// Reads back from the same file-static slot. Returns raw pointer (NULL if empty).
Mortar::Texture* GetCurrentBackground() {
    return g_BackgroundTexture.Get();
}

void UnloadBackground() {
    g_BackgroundTexture.SetNull();
}

// ASM-spec v1.6.1 UpdateBackground @ 0x001cc9f0
// Reads the equipped background item from ItemManager and calls ChangeBackground()
// to keep the rendered background in sync with the shop-equipped item.
void UpdateBackground() {
    ItemManager* im = ItemManager::GetInstance();
    if (!im) return;
    ItemInfo* bg = im->GetEquipped(ITEM_TYPE_BACKGROUND);  // slot 1
    if (!bg) return;
    ChangeBackground(bg->m_pTextureName);  // +0x30 char*
}
