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
