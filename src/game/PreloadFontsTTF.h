#ifndef FN_GAME_PRELOADFONTSTTF_H
#define FN_GAME_PRELOADFONTSTTF_H

// PreloadFontsTTF -- binary @ 0x0011c1fc
// Loads the shared localized TTF face into game_work.m_pTTFFontMain (+0x614).
// Picks "fontstruetype/arabic.ttf" when game_work.languageFlag (bM_LangId, +0x03)
// == 0x14, else "fontstruetype/gangofchinese.ttf".
// Called from GameInitialise (binary InitialiseData @ 0x0011c3f0), after the
// bitmap font block and before MenuButton::LoadContent.
void PreloadFontsTTF();

#endif // FN_GAME_PRELOADFONTSTTF_H
