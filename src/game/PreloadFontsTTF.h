#ifndef FN_GAME_PRELOADFONTSTTF_H
#define FN_GAME_PRELOADFONTSTTF_H

// PreloadFontsTTF -- binary @ 0x0011c1fc
// Loads the shared localized TTF face into game_work.m_pTTFFontMain (+0x614).
// Picks "fontstruetype/arabic.ttf" when game_work.languageFlag (bM_LangId, +0x03)
// == 0x14, else "fontstruetype/gangofchinese.ttf".
// Called from GameInitialise (binary InitialiseData @ 0x0011c3f0), after the
// bitmap font block and before MenuButton::LoadContent.
void PreloadFontsTTF();

// UnloadFontsTTF -- inverse of PreloadFontsTTF; counterpart of the binary's
// GameDestroy @0x0011d20c font-TTF block (~FontCacheObjectTTF + operator delete +
// slot = 0, then FontInterface::Shutdown).
//
// Releases the owning Mortar::SmartPtr<Font> that PreloadFontsTTF holds. That drop
// cascades: ~Font -> FontTTFRegistry::Unregister -> delete FontCacheObjectTTF ->
// delete its FontInterface atlas -> FontInterface::Clear() -> glDeleteTextures on
// every atlas page.
//
// MUST be called from GameDestroy, while the GL context is still current. If the
// handle is instead left to atexit, the whole chain above runs after
// SDL_GL_DeleteContext and every atlas page texture leaks.
//
// The caller is responsible for also nulling the non-owning raw pointer
// game_work.m_pTTFFontMain (+0x614), which dangles once this returns.
// Safe to call when nothing was ever loaded.
void UnloadFontsTTF();

// Port specific: task #28 first-screen-open frame-spike mitigation. The binary
// lazy-bakes each TTF glyph on first use (FontCacheObjectTTF::GetGlyph ->
// FT_Load_Glyph, amplified 9x in HD builds by kFontSupersample=3 -- 1x when
// FN_ENABLE_HD_ASSETS is off, e.g. Wii); with no warm pass, every
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

#if defined(FN_BLOCK_PRELOAD)
// Task #36 Stage 2 -- block-preload of the GAMEOVER-screen TTF
// glyph sizes (30, 56) that WarmTTFGlyphCache() above does NOT cover (its
// s_WarmSizes list is 9.9/10/12/14/22, all menu sizes). Without this, the
// first GameOverScreen/ScoreControl BakedStringBox at these sizes rasterizes
// its glyphs (BakedFontWii::LoadSizeIndex + EnsurePageTexture, task #51) at
// the moment gameover POPS over the frozen game -- with no fade/transition
// covering it (unlike SetupLevel's camera fade) -- so the hitch is worse
// there than the same lazy-load pattern elsewhere. Called from
// BlockLoader::PreloadBlock(RES_BLOCK_INGAME) (BlockLoader.cpp) alongside the
// GAMEOVER texture/mesh/SFX deltas, since GAMEOVER has no block-entry hook of
// its own that fires ahead of the pop (see ResBlock.h file comment: GAMEOVER
// is additive over INGAME, not a separate transition).
//
// Same two-part warm shape as WarmTTFGlyphCache: (1) ASCII printable range at
// both sizes -- covers the ScoreControl digit boxes (30.0f) and any latin
// title fallback; (2) the actual GameOverScreen title label strings (0x2db
// Classic / 0x2f9 Arcade&Zen, both size 56.0f, see GameOverScreen.cpp:391-397)
// and LSTR_SCORE (30.0f, see ScoreControl.cpp:151) for the active language's
// CJK/non-ASCII glyphs. Flushes the atlas once at the end, matching
// WarmTTFGlyphCache. No-op if game_work.m_pTTFFontMain is null.
void WarmTTFGlyphCacheGameOver();
#endif

#endif // FN_GAME_PRELOADFONTSTTF_H
