// PreloadFontsTTF -- binary @ 0x0011c1fc
// Picks "fontstruetype/arabic.ttf" when bM_LangId (game_work+0x03) == 0x14,
// else "fontstruetype/gangofchinese.ttf". Constructs the FontCacheObjectTTF
// and stores the raw pointer at game_work+0x614 (m_pTTFFontMain).
// Port: Font::Create + FontTTFRegistry::Lookup mirror the binary's direct ctor
// (binary signature FontCacheObjectTTF(path, FontInterface*, atlasW, atlasH) is
// replaced by the registry-based approach which matches the existing port pattern).

#include "PreloadFontsTTF.h"
#include "game/GameWork.h"
#include "render/Font.h"
#include "render/FontCacheObjectTTF.h"
#include "render/FontTTFRegistry.h"
#include "render/Utf8StringIterator.h"
#include "util/StringTable.h"
#include <cstdint>
#include <cstddef>

// Owning Font handle for the shared localized TTF face (game_work.m_pTTFFontMain).
// Kept alive for the process lifetime once PreloadFontsTTF populates it.
// Reassigned on each PreloadFontsTTF call (handles language-switch or re-init).
static Mortar::SmartPtr<Mortar::Font> s_TTFFontMain;

// ASM-spec v1.6.1 PreloadFontsTTF @0x0011c1fc:
// arabic.ttf if bM_LangId==0x14 else gangofchinese.ttf -> game_work+0x614
void PreloadFontsTTF() {
    const char* path = (game_work.languageFlag == 0x14)
        ? "fontstruetype/arabic.ttf"
        : "fontstruetype/gangofchinese.ttf";
    s_TTFFontMain = Mortar::Font::Create(path);
    if (!s_TTFFontMain.IsValid()) {
        game_work.m_pTTFFontMain = 0;
        return;
    }
    game_work.m_pTTFFontMain =
        Mortar::FontTTFRegistry::GetInstance().Lookup(s_TTFFontMain.Get());
}

// Port specific: task #28 boot-time glyph-cache warm. See PreloadFontsTTF.h for the
// full rationale. Font sizes pulled from the real call sites (not guessed):
//   MenuButton::SetText:  9.9 (ShopScreen back/buy), 10.0 (Back/Quit/About rings),
//                         12.0 (Zen/Arcade/Retry/Quit/Equip rings), 14.0 (Classic/Shop rings)
//   BakedStringBox ctor:  12.0 (GameModeScreen zen-sign lines), 22.0 (MODE SELECT/MULTIPLAYER)
static const float s_WarmSizes[] = { 9.9f, 10.0f, 12.0f, 14.0f, 22.0f };
static const int s_WarmSizeCount = sizeof(s_WarmSizes) / sizeof(s_WarmSizes[0]);

struct WarmLabel {
    LocalizedString id;
    float            fontSize;
};

// (LSTR id, fontSize) pairs pulled from the actual SetText/BakedStringBox call sites of
// the boot-reachable menu screens. Raw hex casts mirror the existing call-site style for
// LSTR ids that don't have a named enumerator (see src/screens/*.cpp comments for each).
static const WarmLabel s_WarmLabels[] = {
    { LSTR_NEW_GAME,             12.0f }, // MainScreen::CreateButtons @0x001961f8 (Play)
    { LSTR_DOJO_TITLE,           12.0f }, // MainScreen::CreateButtons @0x001961f8 (Store)
    { LSTR_QUIT,                 10.0f }, // MainScreen::CreateQuitButton
    { LSTR_QUIT,                 12.0f }, // GameOverScreen::CreateQuitButton @0x00186220
    { LSTR_DJ_BACK_BUTTON,       10.0f }, // GameModeScreen/DojoScreen back button
    { LSTR_DJ_BACK_BUTTON,        9.9f }, // ShopScreen::Update @0x001b321c back/buy button (same LSTR id)
    { LSTR_GM_CLASSIC,           14.0f }, // GameModeScreen::CreateMenuItems
    { LSTR_GM_ZEN,               12.0f }, // GameModeScreen::CreateMenuItems
    { LSTR_GM_ARCADE,            12.0f }, // GameModeScreen::CreateMenuItems
    { LSTR_DJ_SHOP_BUTTON,       14.0f }, // DojoScreen::CreateButtons @0x0016ad9c
    { LSTR_ABOUT_TITLE,          10.0f }, // DojoScreen::CreateButtons (About ring label)
    { (LocalizedString)0x3ba,   22.0f }, // GameModeScreen "MODE SELECT" box
    { (LocalizedString)0x39f,   22.0f }, // GameModeScreen "MULTIPLAYER" box
    { (LocalizedString)0x3be,   12.0f }, // GameModeScreen zen-sign line "NO BOMBS!"
    { (LocalizedString)0x3bf,   12.0f }, // GameModeScreen zen-sign line "NO LIVES!"
    { (LocalizedString)0x3c0,   12.0f }, // GameModeScreen zen-sign line "90 SECS!"
    { (LocalizedString)0x3b5,   12.0f }, // GameOverScreen::CreateRetryButton @0x00185f98
    { (LocalizedString)0xed,    12.0f }, // ShopScreen::SetSelected @0x001b24f0 equip (unlocked)
    { (LocalizedString)0x3c7,   12.0f }, // ShopScreen::SetSelected @0x001b24f0 equip (locked)
    { LSTR_MENU_TEXTURE_13,      9.0f }, // MainScreen ctor @0x0019811c "SLICE FRUIT TO BEGIN" plate
    { LSTR_GAME_TEXTURE_02,     20.0f }, // IngamePopup ctor @0x0016dbac type 0x0F "NEW BEST!"
    { LSTR_MENU_TEXTURE_09,     16.0f }, // IngamePopup ctor @0x0016dbac type 0x10 "NEW"
    { LSTR_MENU_TEXTURE_53,     17.0f }, // IngamePopup ctor @0x0016dbac type 0x11 "SELECTED"
};
static const int s_WarmLabelCount = sizeof(s_WarmLabels) / sizeof(s_WarmLabels[0]);

void WarmTTFGlyphCache() {
    Mortar::FontCacheObjectTTF* font = game_work.m_pTTFFontMain;
    if (!font) return;

    // Part 1: ASCII printable range at each distinct menu-label size -- language-
    // independent (latin UI text, digits, punctuation).
    for (int s = 0; s < s_WarmSizeCount; s++) {
        for (uint32_t cp = 0x20; cp <= 0x7E; cp++) {
            font->GetGlyph(cp, s_WarmSizes[s]);
        }
    }

    // Part 2: the actual localized label strings for the current language -- covers
    // CJK/other non-ASCII glyphs the ASCII pass above doesn't reach.
    for (int i = 0; i < s_WarmLabelCount; i++) {
        const char* str = GETSTRING_CAST_0(s_WarmLabels[i].id);
        if (!str) continue;
        Mortar::Utf8StringIterator it(str);
        while (!it.IsEmpty()) {
            font->GetGlyph(it.m_CurrentCodepoint, s_WarmLabels[i].fontSize);
            it++;
        }
    }

    // Flush the atlas ONCE so the GL upload happens at boot, not on first screen open.
    font->GetAtlas()->BuildPendingTextures();
}
