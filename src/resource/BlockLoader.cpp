#ifdef FN_BLOCK_PRELOAD

#include "resource/BlockLoader.h"
#include "asset/TextureManager.h"
#include "asset/Model.h"
#include "asset/MeshManager.h"
#include "audio/SoundManager.h"
#include "game/PreloadFontsTTF.h"
#include "entities/Fruit.h"
#include "entities/SuperFruitControl.h"
#include "hud/MissControl.h"
#include "screens/GameOverScreen.h"
#include "screens/DojoScreen.h"
#include "screens/ShopScreen.h"
#include "debug/Logger.h"
#include <vector>

#if defined(FRUIT_PLATFORM_WII)
#include "platform/wii/Mem2Alloc.h"  // Wii_MEM2FreeBytes (see LogHeapUsage)
#include <gccore.h>  // SYS_GetArena1Size / SYS_GetArena2Size (see LogHeapUsage)
#endif

namespace fn {
namespace wii {

namespace {

// Strong holders so a preloaded texture survives past the temporary
// SmartPtr Load() returns -- see BlockLoader.h file comment (Risk R1: the
// TextureManager cache is weak-only). One vector per block; never cleared in
// V1 (no eviction -- see BlockLoader.h). Meshes need no equivalent vector:
// MeshManager::Load already holds its own strong ref (MeshManager.cpp:70-73).
std::vector<Mortar::SmartPtr<Mortar::Texture> > s_HeldIngame;
std::vector<Mortar::SmartPtr<Mortar::Texture> > s_HeldShop;

bool s_IngamePreloaded = false;
bool s_ShopPreloaded   = false;

// --- INGAME + GAMEOVER (merged; see BlockLoader.h) ------------------------
// Ground truth: runtime [BlockLoad] logs from a Dolphin play-through (Stage
// 1 instrumentation) enumerating every asset that lazy-loaded mid-block
// instead of at boot. Everything else in these two blocks already
// eager-loads at boot (PreloadSounds.cpp / PreloadFontsTTF.cpp / etc) and
// needs no entry here. Crit-slice SFX ("Visceral-impact-1/2/3", see
// Fruit::AddSlice) are already preloaded elsewhere; no entry needed here (#58).
const char* const kIngameSfx[] = {
    "Bomb-Fuse",
    "Bonus-Banana-Freeze",
    "Bonus-Banana-X2",
    "Combo-Blitz-Backing-Light",
    "Time-tick",
    "Time-tock",
    "combo-1",
    "combo-2",
    "combo-blitz-1",
};
const int kIngameSfxCount = sizeof(kIngameSfx) / sizeof(kIngameSfx[0]);

const char* const kIngameTex[] = {
    "flash.tex",
};
const int kIngameTexCount = sizeof(kIngameTex) / sizeof(kIngameTex[0]);

const char* const kGameOverTex[] = {
    "arcade_bonus_number_border.tex",
    "arcade_diolog_box.tex",
    "blank_dialog_box.tex",
    "fact_board.tex",
    "big_fact_board.tex",
    "result_board_divider.tex",
    "sensei_head.tex",
    "sml_ap.tex",
    "sml_pl.tex",            // result-board small "PL" icon (fail-loud catch)
    "combo_description.tex", // fact-board combo description strip (fail-loud catch)
    // NOTE: combo-star icons (star_succulent/star_fruity/... ~40 in
    // FruitFactCombo kComboStars) are NOT preloaded -- they were made a no-op load
    // on Wii (FruitFactZenPage.cpp) because the binary stores the result in a
    // write-only member and never draws it (the star visual is font text). So they
    // never load at runtime; nothing to preload.
};
const int kGameOverTexCount = sizeof(kGameOverTex) / sizeof(kGameOverTex[0]);

const char* const kGameOverSfx[] = {
    "Bonus-Explosion-1",
    "Bonus-Explosion-3",
    "Bonus-Explosion-5",
    "Bonus-Firework-Explode",
    "Bonus-drum-roll",
    // Bonus-board reveal stings: the board plays a different popup-N per row
    // reveal (popup-1..8 exist), picked per-reveal -- preload the whole set.
    "popup-1", "popup-2", "popup-3", "popup-4",
    "popup-5", "popup-6", "popup-7", "popup-8",
};
const int kGameOverSfxCount = sizeof(kGameOverSfx) / sizeof(kGameOverSfx[0]);

const char* const kGameOverMesh = "models/Fruit/coin.mmd";

// --- SHOP -------------------------------------------------------------
// 17 item-icon textures (ShopListItem::Create @0x001b27f0 loads these
// per-item, lazily, only once that item's ShopListItem scrolls into view --
// see src/hud/ShopListItem.cpp:260-266). Names are the resolved
// "item_<ItemInfo::m_pTextureName>.tex" / "<name>.tex" strings observed at
// runtime, matching what LoadLocalisedTexture already produces for these
// items (data-driven from the shop item XML, not literal call-site strings
// -- so listed here verbatim rather than derived from a symbol).
const char* const kShopTex[] = {
    "item_originalblade.tex",
    "item_shiney_red_blade.tex",
    "item_discoblade.tex",
    "item_mr_sparkle.tex",
    "item_american_blade.tex",
    "item_butterfly_knife.tex",
    "item_flame_blade.tex",
    "item_ice_blade.tex",
    "item_pixel_blade.tex",
    "item_piano_blade.tex",
    "item_party_knife.tex",
    "item_bamboo_shoot.tex",
    "item_GB_game.tex",
    "item_BG_fruit_ninja.tex",
    "item_BG_i_heart_sensei.tex",
    "item_BG_greatwave.tex",
    "item_BG_YinYang.tex",
};
const int kShopTexCount = sizeof(kShopTex) / sizeof(kShopTex[0]);

// Loads `name` via the SAME call the real (lazy) call site uses --
// LoadLocalisedTexture, so the cache key/locale-fallback behaviour is
// identical and a preload followed by the real call site is a guaranteed
// cache hit. Holds the returned strong ref in `held` so it survives.
void PreloadTexture(const char* name, std::vector<Mortar::SmartPtr<Mortar::Texture> >* held) {
    Mortar::SmartPtr<Mortar::Texture> tex = Mortar::TextureManager::LoadLocalisedTexture(name);
    if (tex.IsValid()) {
        held->push_back(tex);
    } else {
        LOG_WARN("BlockLoader", "PreloadBlock: texture '%s' failed to load", name);
    }
}

void PreloadSfx(const char* name) {
    // SoundManager::PreLoadSound already cache-checks (m_SoundCache.count)
    // before touching disk -- see SoundManagerWii.cpp:357-363 -- so this is
    // safe to call unconditionally and idempotent across repeated
    // PreloadBlock calls.
    Mortar::SoundManager::GetInstance().PreLoadSound(name);
}

} // namespace

// See BlockLoader.h. SYS_GetArena1Size/SYS_GetArena2Size return the bytes
// still free in each arena (not the total heap) -- exactly the "how much
// headroom is left before OOM" figure #36/#59 need. Only meaningful on Wii
// (libogc arena concept); a no-op elsewhere.
void LogHeapUsage(const char* label) {
#if defined(FRUIT_PLATFORM_WII)
    u32 mem1Free = SYS_GetArena1Size();
    u32 mem2Free = SYS_GetArena2Size();
    // Task #61: SYS_GetArena2Size() now reads low once Wii_MEM2Init() has
    // carved most of MEM2 into our own allocator (see Mem2Alloc.h) -- that's
    // expected, not a regression. Wii_MEM2FreeBytes() reports the real
    // remaining headroom inside that carved heap.
    u32 mem2AllocFree = Wii_MEM2FreeBytes();
    LOG_INFO("HeapUsage", "%s: MEM1 free=%u KB, MEM2 arena free=%u KB, MEM2 alloc free=%u KB",
             label, (unsigned)(mem1Free / 1024), (unsigned)(mem2Free / 1024),
             (unsigned)(mem2AllocFree / 1024));
#else
    // Defunct: heap-usage logging -- no libogc arena concept off Wii; no-op stub.
    (void)label;
#endif
}

void BlockLoader::PreloadBlock(ResBlockFlag block) {
    if (block == RES_BLOCK_INGAME) {
        if (s_IngamePreloaded) return;
        s_IngamePreloaded = true;

        for (int i = 0; i < kIngameSfxCount; i++) {
            PreloadSfx(kIngameSfx[i]);
        }
        for (int i = 0; i < kIngameTexCount; i++) {
            PreloadTexture(kIngameTex[i], &s_HeldIngame);
        }

        // Task #59 boot trim -- gameplay-only chunks deferred out of
        // GameInitialise() on Wii (see the FN_BLOCK_PRELOAD guards at each
        // call site). Core slicing assets (Fruit/Bomb/Slash/Splat models,
        // particle textures, SFX) are needed at menu time -- the menu ring
        // buttons are real Fruit/Bomb entities you slice -- so those stayed
        // resident at boot; only these genuinely gameplay-only combo/HUD
        // pieces are deferred. Refs land in each class's own SmartPtr/mesh
        // members (their natural strong-ref home, matching the binary's
        // ownership) -- NOT s_HeldIngame, which is reserved for loose
        // PreloadTexture() calls (kIngameTex/kGameOverTex above) that have
        // no owning member.
        Fruit::LoadHudTextures();
        MissControl::LoadContent();
        SuperFruitControl::LoadContent();

        // GAMEOVER merged in here -- see BlockLoader.h / ResBlock.h file
        // comments: gameover pops instantly over the frozen game with no
        // fade of its own to cover a load, so its deltas must land during
        // the pre-level fade alongside INGAME's.
        //
        // GameOverScreen::LoadContent() is idempotent (g_LoadContentGuard,
        // GameOverScreen.cpp:178) and owns its own texture refs (file-static
        // SmartPtrs) -- no s_HeldIngame entry needed, same pattern as
        // Fruit/MissControl/SuperFruit above. Its texture set (arcade_time_up,
        // gameover, time_up, retry, quit, leaderboards, gc_leaderboards,
        // sensei_head_0N/sensei_body_0N) does NOT overlap kGameOverTex below --
        // that literal list covers a disjoint set (fact board, dialog boxes,
        // sml_ap/pl, combo_description, and the base sensei_head.tex which is
        // a different asset from the sensei_head_0N variants here) -- so both
        // stay.
        GameOverScreen::LoadContent();
        for (int i = 0; i < kGameOverTexCount; i++) {
            PreloadTexture(kGameOverTex[i], &s_HeldIngame);
        }
        for (int i = 0; i < kGameOverSfxCount; i++) {
            PreloadSfx(kGameOverSfx[i]);
        }
        Mortar::MeshManager* mm = Mortar::MeshManager::GetInstance();
        if (mm) {
            Mortar::SmartPtr<Mortar::Model> coin = mm->Load(kGameOverMesh);
            if (!coin.IsValid()) {
                LOG_WARN("BlockLoader", "PreloadBlock: mesh '%s' failed to load", kGameOverMesh);
            }
        }
        WarmTTFGlyphCacheGameOver();

        LOG_INFO("BlockLoader", "PreloadBlock: INGAME+GAMEOVER done "
                 "(%d sfx, %d tex, %d mesh, font sizes 30/50/56)",
                 kIngameSfxCount + kGameOverSfxCount,
                 kIngameTexCount + kGameOverTexCount, 1);
        LogHeapUsage("INGAME+GAMEOVER");
        return;
    }

    if (block == RES_BLOCK_SHOP) {
        if (s_ShopPreloaded) return;
        s_ShopPreloaded = true;

        // Task #59 boot trim -- Dojo/Shop screen chrome (BG_store, dojo bg,
        // etc, 16 tex total) deferred out of GameInitialise() on Wii (see the
        // FN_BLOCK_PRELOAD guard at the call site in GameInitialise.cpp).
        // Port-only #28 screens, no binary counterpart, only reachable via
        // menu -> dojo -> shop, so no fidelity constraint. Refs land in the
        // screens' own members (same pattern as GameOverScreen above) --
        // distinct from kShopTex[] below, which is the scroll-in item-icon
        // set (lazy-loaded per ShopListItem, not screen chrome).
        DojoScreen::LoadContent();
        ShopScreen::LoadContent();

        for (int i = 0; i < kShopTexCount; i++) {
            PreloadTexture(kShopTex[i], &s_HeldShop);
        }

        LOG_INFO("BlockLoader", "PreloadBlock: SHOP done (%d tex)", kShopTexCount);
        LogHeapUsage("SHOP");
        return;
    }

    // MENU / GAMEOVER-alone / other masks: no V1 manifest (menu textures
    // already eager-load at boot per the Stage 1 [BlockLoad] audit; GAMEOVER
    // is only ever entered additively over INGAME -- see ResBlock.h -- so it
    // has no standalone preload point).
}

} // namespace wii
} // namespace fn

#endif // FN_BLOCK_PRELOAD
