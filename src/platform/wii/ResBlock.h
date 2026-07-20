#ifndef FN_PLATFORM_WII_RESBLOCK_H
#define FN_PLATFORM_WII_RESBLOCK_H

// Task #36 Stage 1 -- current-block tracking (log-only, no preload/eviction
// yet). See tmp/wii/loader-blueprint.md sections 2 and 7 (Stage 1).
//
// A single process-wide "which screen/block is active" flag, set at the
// block-enter hooks the blueprint identifies (GameModeScreen::SetupLevel for
// INGAME, ShopScreen ctor for SHOP, MainScreen menu-buttons-created / the
// mode-select and game-over "back to menu" callbacks for MENU, GameOver() for
// GAMEOVER). GAMEOVER is ADDITIVE: it does NOT clear INGAME (blueprint Risk
// R4 -- the game-over screen draws over the frozen, still-resident gameplay
// state). Nothing here allocates, frees, or preloads; it only labels the
// [BlockLoad] log lines emitted by the resource Load funnels so a Dolphin run
// can be grepped into a per-block resource manifest.
//
// Only compiled when FRUIT_PLATFORM_WII is set.
#ifdef FRUIT_PLATFORM_WII

namespace fn {
namespace wii {

// Bit flags, not a plain enum: GAMEOVER coexists with INGAME (see file
// header). MENU/SHOP/INGAME are mutually exclusive in practice (the screens
// that set them are mutually exclusive), but modelling all four as
// independent bits keeps GetCurrentBlockMask() honest about the additive
// GAMEOVER case without a separate "previous block" stack.
enum ResBlockFlag {
    RES_BLOCK_NONE     = 0,
    RES_BLOCK_MENU     = 1 << 0,
    RES_BLOCK_SHOP     = 1 << 1,
    RES_BLOCK_INGAME   = 1 << 2,
    RES_BLOCK_GAMEOVER = 1 << 3,
};

// Sets the current block to exactly `block` (clears any other bits) --
// MENU/SHOP/INGAME entry points use this.
void SetCurrentBlock(ResBlockFlag block);

// Adds `block` to the current mask without clearing existing bits -- used
// only by GAMEOVER entry (additive over INGAME per Risk R4).
void AddCurrentBlock(ResBlockFlag block);

// Current block mask, e.g. for [BlockLoad] log lines.
int GetCurrentBlockMask();

// Human-readable name of the current mask for log lines, e.g. "INGAME",
// "INGAME|GAMEOVER", or "NONE" before the first SetCurrentBlock call.
// Returns a pointer to a static buffer; not reentrant-safe across threads
// (Stage 1 is single-threaded main-loop code only).
const char* GetCurrentBlockName();

} // namespace wii
} // namespace fn

#endif // FRUIT_PLATFORM_WII

#endif // FN_PLATFORM_WII_RESBLOCK_H
