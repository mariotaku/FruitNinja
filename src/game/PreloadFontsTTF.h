#ifndef FN_GAME_PRELOADFONTSTTF_H
#define FN_GAME_PRELOADFONTSTTF_H

// PreloadFontsTTF -- binary @ 0x0011c1fc
// Loads the shared localized TTF face into game_work.m_pTTFFontMain (+0x614).
// Picks "fontstruetype/arabic.ttf" when game_work.languageFlag (bM_LangId, +0x03)
// == 0x14, else "fontstruetype/gangofchinese.ttf".
// Called from GameInitialise (binary InitialiseData @ 0x0011c3f0), after the
// bitmap font block and before MenuButton::LoadContent.
void PreloadFontsTTF();

// Port specific: task #28 first-screen-open frame-spike mitigation. The binary
// lazy-bakes each TTF glyph on first use (FontCacheObjectTTF::GetGlyph ->
// FT_Load_Glyph, amplified 9x by kFontSupersample=3); with no warm pass, every
// glyph a screen's labels need gets rasterized + atlas-uploaded in a single
// frame the first time that screen opens. This has no binary counterpart --
// v1.6.1 has no equivalent bootstrap warm.
//
// Call once at boot, AFTER PreloadFontsTTF() and after the per-language
// InitialiseData(fontScale, globalSizeScale) override (the glyph cache key
// depends on both, so warming before the override bakes the wrong entries for
// languages that change globalSizeScale, e.g. russian/0x13 -> 0.9).
//
// Pre-rasterizes:
//   1. ASCII printable range (0x20-0x7E) at each distinct menu-label font size
//      actually used by MenuButton::SetText / BakedStringBox call sites
//      (9.9, 10, 12, 14, 22) -- covers latin UI text, digits, punctuation.
//   2. The specific localized label strings the boot-reachable menu screens
//      (MainScreen, GameModeScreen, DojoScreen, ShopScreen, GameOverScreen)
//      draw, at their real sizes -- covers CJK/other non-ASCII glyphs the
//      ASCII pass doesn't reach, for whatever language is active.
// Then flushes the atlas ONCE via FontInterface::BuildPendingTextures() so the
// GL upload happens here, not on first screen open.
//
// No-op if game_work.m_pTTFFontMain is null (font failed to load).
void WarmTTFGlyphCache();

#endif // FN_GAME_PRELOADFONTSTTF_H
