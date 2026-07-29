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
#include <string>
#include <cstdio>

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
        // Dedup by the underlying Texture* -- PreloadBlockBegin now rebuilds
        // the queue on every re-entry (see PreloadBlockBegin below), so a
        // resident block's textures get re-pushed here on every re-entry too.
        // Without this check s_HeldIngame/s_HeldShop would grow one entry per
        // re-entry forever even though every entry after the first refers to
        // the SAME already-resident Texture (a cache hit returns the same
        // object). Skipping the push keeps the vector at one ref per unique
        // texture while the texture stays resident (held->clear() in
        // FreeBlock still drops that single ref -> refcount 0 -> eviction).
        bool alreadyHeld = false;
        for (std::vector<Mortar::SmartPtr<Mortar::Texture> >::iterator it = held->begin();
             it != held->end(); ++it) {
            if (it->Get() == tex.Get()) {
                alreadyHeld = true;
                break;
            }
        }
        if (!alreadyHeld) {
            held->push_back(tex);
        }
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

// --- Task #66 Phase 1/3: cooperative per-frame work-queue -------------------
//
// Each atomic PreloadBlock() step (one texture, one SFX, the coin mesh, a
// LoadContent-style wrapper, or a bare fn) becomes a WorkItem.
//
// Phase 1 treated each LoadContent wrapper as ONE work item. Phase 3 refined
// the heavy wrappers (Fruit::LoadHudTextures, MissControl/GameOverScreen/
// DojoScreen/ShopScreen::LoadContent) further: a granular WORK_TEX item is
// pushed per texture the wrapper loads internally, immediately followed by
// the wrapper's own WORK_LOADCONTENT item. This spreads what used to be N
// file reads in a single frame across N frames (1 read/frame); the trailing
// LOADCONTENT item then re-requests the same paths and gets guaranteed cache
// hits (TextureManager::Load checks Find(hash) before touching disk -- see
// TextureManager.cpp), so it does zero IO but still performs the wrapper's
// non-texture setup (guard flags, non-preloadable fields). SuperFruitControl::
// LoadContent (2 textures) and WarmTTFGlyphCacheGameOver (font-glyph wrapper)
// have no natural per-asset split and stay one item each.
//
// std::vector<WorkItem> is port-only glue (queue book-keeping, not a binary
// structure) -- fine per the "layout-faithful std:: is ok for port-only
// containers" carve-out.
enum WorkItemKind {
    WORK_TEX,          // PreloadTexture(name, held)
    WORK_SFX,          // PreloadSfx(name)
    WORK_MESH,         // MeshManager::Load(name)
    WORK_LOADCONTENT,  // a LoadContent()-style wrapper, called with no args
    WORK_FN,           // a bare void(*)() call (log/heap-usage tail steps)
};

struct WorkItem {
    WorkItemKind kind;
    // Phase 3: stored BY VALUE (not const char*) -- several call sites now
    // push runtime-formatted names built in a stack buffer (e.g. "hud_%s.tex",
    // "combo_%d.tex"); a raw pointer would dangle once that buffer goes out
    // of scope. Literal-string call sites pay one extra copy; harmless.
    std::string name;                                          // TEX/SFX/MESH
    std::vector<Mortar::SmartPtr<Mortar::Texture> >* held;      // TEX only
    void (*fn)();                                                // LOADCONTENT/FN

    WorkItem(WorkItemKind k, const std::string& n,
             std::vector<Mortar::SmartPtr<Mortar::Texture> >* h, void (*f)())
        : kind(k), name(n), held(h), fn(f) {}
};

WorkItem MakeTexItem(const std::string& name, std::vector<Mortar::SmartPtr<Mortar::Texture> >* held) {
    return WorkItem(WORK_TEX, name, held, 0);
}
WorkItem MakeSfxItem(const char* name) {
    return WorkItem(WORK_SFX, name, 0, 0);
}
WorkItem MakeMeshItem(const char* name) {
    return WorkItem(WORK_MESH, name, 0, 0);
}
WorkItem MakeFnItem(WorkItemKind kind, void (*fn)()) {
    return WorkItem(kind, std::string(), 0, fn);
}

void RunWorkItem(const WorkItem& item) {
    switch (item.kind) {
    case WORK_TEX:
        PreloadTexture(item.name.c_str(), item.held);
        break;
    case WORK_SFX:
        PreloadSfx(item.name.c_str());
        break;
    case WORK_MESH: {
        Mortar::MeshManager* mm = Mortar::MeshManager::GetInstance();
        if (mm) {
            Mortar::SmartPtr<Mortar::Model> coin = mm->Load(item.name.c_str());
            if (!coin.IsValid()) {
                LOG_WARN("BlockLoader", "PreloadBlockStep: mesh '%s' failed to load", item.name.c_str());
            }
        }
        break;
    }
    case WORK_LOADCONTENT:
    case WORK_FN:
        item.fn();
        break;
    }
}

// Wrapper trampolines -- WorkItem::fn is a bare void(*)() (no captures allowed
// per the cross-build's no-capture-lambda rule anyway), so anything needing
// extra tail-logging (the two LOG_INFO/LogHeapUsage calls that used to run at
// the very end of each PreloadBlock branch) becomes its own zero-arg function.
void LogIngameGameoverDone() {
    LOG_INFO("BlockLoader", "PreloadBlock: INGAME+GAMEOVER done "
             "(%d sfx, %d tex, %d mesh, font sizes 30/50/56)",
             kIngameSfxCount + kGameOverSfxCount,
             kIngameTexCount + kGameOverTexCount, 1);
    LogHeapUsage("INGAME+GAMEOVER");
}

void LogShopDone() {
    LOG_INFO("BlockLoader", "PreloadBlock: SHOP done (%d tex)", kShopTexCount);
    LogHeapUsage("SHOP");
}

std::vector<WorkItem> s_Queue;
ResBlockFlag s_QueueBlock = RES_BLOCK_NONE;

// Builds the INGAME+GAMEOVER work-queue in the SAME order PreloadBlock's
// synchronous body executes it (see that function below) -- this is the
// single source of truth for the manifest; PreloadBlock's sync path now just
// drains this queue in one gulp instead of duplicating the sequence.
void BuildIngameQueue(std::vector<WorkItem>* q) {
    for (int i = 0; i < kIngameSfxCount; i++) q->push_back(MakeSfxItem(kIngameSfx[i]));
    for (int i = 0; i < kIngameTexCount; i++) q->push_back(MakeTexItem(kIngameTex[i], &s_HeldIngame));

    // Phase 3: warm Fruit::LoadHudTextures textures granularly (1 read/frame);
    // the LOADCONTENT finalizer below then runs cache-hit-cheap. Keep in sync
    // with FruitInfo.cpp FruitInfo_LoadHudTextures (hud_%s.tex/zen_%s.tex per
    // fruit-info entry). Warmed into s_HeldIngame purely to keep them resident
    // until the finalizer's own FruitInfo array members take over ownership --
    // FruitInfo_LoadHudTextures reloads (cache hit) straight into
    // fi.m_HudTexture/m_ZenTexture, which is the real strong-ref home.
    for (int i = 0; i < g_FruitInfoCount; i++) {
        const FruitInfo* fi = FruitInfo_Get(i);
        if (!fi->m_Name[0]) continue;
        char texName[64];
        snprintf(texName, sizeof(texName), "hud_%s.tex", fi->m_Name);
        q->push_back(MakeTexItem(texName, &s_HeldIngame));
        snprintf(texName, sizeof(texName), "zen_%s.tex", fi->m_Name);
        q->push_back(MakeTexItem(texName, &s_HeldIngame));
    }
    q->push_back(MakeFnItem(WORK_LOADCONTENT, &Fruit::LoadHudTextures));

    // Phase 3: warm MissControl::LoadContent textures granularly (1 read/frame);
    // the LOADCONTENT finalizer below then runs cache-hit-cheap. Keep in sync
    // with MissControl.cpp MissControl::LoadContent.
    q->push_back(MakeTexItem("critical.tex", &s_HeldIngame));
    q->push_back(MakeTexItem("ultra_rare_plus_50.tex", &s_HeldIngame));
    q->push_back(MakeTexItem("hud_cross.tex", &s_HeldIngame));
    for (int i = 3; i <= 10; i++) {
        char name[32];
        snprintf(name, sizeof(name), "combo_%d.tex", i);
        q->push_back(MakeTexItem(name, &s_HeldIngame));
    }
    q->push_back(MakeFnItem(WORK_LOADCONTENT, &MissControl::LoadContent));

    q->push_back(MakeFnItem(WORK_LOADCONTENT, &SuperFruitControl::LoadContent));

    // Phase 3: warm GameOverScreen::LoadContent textures granularly (1 read/frame);
    // the LOADCONTENT finalizer below then runs cache-hit-cheap. Keep in sync
    // with GameOverScreen.cpp GameOverScreen::LoadContent (binary @ 0x00130bb0).
    q->push_back(MakeTexItem("arcade_time_up.tex", &s_HeldIngame));
    q->push_back(MakeTexItem("gameover.tex", &s_HeldIngame));
    q->push_back(MakeTexItem("time_up.tex", &s_HeldIngame));
    q->push_back(MakeTexItem("retry.tex", &s_HeldIngame));
    q->push_back(MakeTexItem("quit.tex", &s_HeldIngame));
    q->push_back(MakeTexItem("leaderboards.tex", &s_HeldIngame));
    q->push_back(MakeTexItem("gc_leaderboards.tex", &s_HeldIngame));
    for (int i = 1; i <= 3; i++) {
        char name[32];
        snprintf(name, sizeof(name), "sensei_head_0%d.tex", i);
        q->push_back(MakeTexItem(name, &s_HeldIngame));
        snprintf(name, sizeof(name), "sensei_body_0%d.tex", i);
        q->push_back(MakeTexItem(name, &s_HeldIngame));
    }
    q->push_back(MakeFnItem(WORK_LOADCONTENT, &GameOverScreen::LoadContent));
    for (int i = 0; i < kGameOverTexCount; i++) q->push_back(MakeTexItem(kGameOverTex[i], &s_HeldIngame));
    for (int i = 0; i < kGameOverSfxCount; i++) q->push_back(MakeSfxItem(kGameOverSfx[i]));
    q->push_back(MakeMeshItem(kGameOverMesh));
    q->push_back(MakeFnItem(WORK_LOADCONTENT, &WarmTTFGlyphCacheGameOver));

    q->push_back(MakeFnItem(WORK_FN, &LogIngameGameoverDone));
}

// Builds the SHOP work-queue, same ordering as PreloadBlock's SHOP branch.
void BuildShopQueue(std::vector<WorkItem>* q) {
    // Phase 3: warm DojoScreen::LoadContent textures granularly (1 read/frame);
    // the LOADCONTENT finalizer below then runs cache-hit-cheap. Keep in sync
    // with DojoScreen.cpp DojoScreen::LoadContent (v1.6.1 @ 0x0016a554), which
    // also chains BaseScreen::LoadContent (sml_title.tex / blurry_backing.tex).
    q->push_back(MakeTexItem("dojo.tex", &s_HeldShop));
    q->push_back(MakeTexItem("dojo_sensei.tex", &s_HeldShop));
    q->push_back(MakeTexItem("sml_title.tex", &s_HeldShop));
    q->push_back(MakeTexItem("blurry_backing.tex", &s_HeldShop));
    q->push_back(MakeTexItem("senseis_swag.tex", &s_HeldShop));
    q->push_back(MakeTexItem("about.tex", &s_HeldShop));
    q->push_back(MakeFnItem(WORK_LOADCONTENT, &DojoScreen::LoadContent));

    // Phase 3: warm ShopScreen::LoadContent textures granularly (1 read/frame);
    // the LOADCONTENT finalizer below then runs cache-hit-cheap. Keep in sync
    // with ShopScreen.cpp ShopScreen::LoadContent (binary @ 0x001b2a20).
    q->push_back(MakeTexItem("locked.tex", &s_HeldShop));
    q->push_back(MakeTexItem("select_item.tex", &s_HeldShop));
    q->push_back(MakeTexItem("loading.tex", &s_HeldShop));
    q->push_back(MakeTexItem("scratch_deviders.tex", &s_HeldShop));
    q->push_back(MakeTexItem("dialog_box_shop.tex", &s_HeldShop));
    q->push_back(MakeTexItem("selected.tex", &s_HeldShop));
    q->push_back(MakeTexItem("selected_sml.tex", &s_HeldShop));
    q->push_back(MakeTexItem("locked_stroke.tex", &s_HeldShop));
    q->push_back(MakeTexItem("new_item_sml.tex", &s_HeldShop));
    // ShopScreen::LoadContent's LowResBackgrounds() conditional is stubbed
    // always-false in the port (see ShopScreen.cpp), so BG_store.tex is the
    // only reachable path -- warm that one, matching the finalizer exactly.
    q->push_back(MakeTexItem("BG_store.tex", &s_HeldShop));
    q->push_back(MakeFnItem(WORK_LOADCONTENT, &ShopScreen::LoadContent));

    for (int i = 0; i < kShopTexCount; i++) q->push_back(MakeTexItem(kShopTex[i], &s_HeldShop));

    q->push_back(MakeFnItem(WORK_FN, &LogShopDone));
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

// Task #59 boot trim -- gameplay-only chunks deferred out of
// GameInitialise() on Wii (see the FN_BLOCK_PRELOAD guards at each call
// site). Core slicing assets (Fruit/Bomb/Slash/Splat models, particle
// textures, SFX) are needed at menu time -- the menu ring buttons are real
// Fruit/Bomb entities you slice -- so those stayed resident at boot; only
// these genuinely gameplay-only combo/HUD pieces are deferred. Refs land in
// each class's own SmartPtr/mesh members (their natural strong-ref home,
// matching the binary's ownership) -- NOT s_HeldIngame, which is reserved
// for loose PreloadTexture() calls (kIngameTex/kGameOverTex) that have no
// owning member.
//
// GAMEOVER merged into INGAME's queue -- see BlockLoader.h / ResBlock.h file
// comments: gameover pops instantly over the frozen game with no fade of its
// own to cover a load, so its deltas must land during the pre-level fade
// alongside INGAME's. GameOverScreen::LoadContent() is idempotent
// (g_LoadContentGuard, GameOverScreen.cpp:178) and owns its own texture refs
// (file-static SmartPtrs) -- no s_HeldIngame entry needed, same pattern as
// Fruit/MissControl/SuperFruit above. Its texture set (arcade_time_up,
// gameover, time_up, retry, quit, leaderboards, gc_leaderboards,
// sensei_head_0N/sensei_body_0N) does NOT overlap kGameOverTex -- that
// literal list covers a disjoint set (fact board, dialog boxes, sml_ap/pl,
// combo_description, and the base sensei_head.tex which is a different
// asset from the sensei_head_0N variants here) -- so both stay.
//
// SHOP: Task #59 boot trim -- Dojo/Shop screen chrome (BG_store, dojo bg,
// etc, 16 tex total) deferred out of GameInitialise() on Wii (see the
// FN_BLOCK_PRELOAD guard at the call site in GameInitialise.cpp). Port-only
// #28 screens, no binary counterpart, only reachable via menu -> dojo ->
// shop, so no fidelity constraint. Refs land in the screens' own members
// (same pattern as GameOverScreen above) -- distinct from kShopTex[], which
// is the scroll-in item-icon set (lazy-loaded per ShopListItem, not screen
// chrome).

void BlockLoader::PreloadBlockBegin(ResBlockFlag block) {
    s_Queue.clear();
    s_QueueBlock = block;

    // The queue is ALWAYS rebuilt here, regardless of s_IngamePreloaded/
    // s_ShopPreloaded -- those latches are informational for FreeBlock only
    // (see below), not a gate on whether Begin queues work. PreloadBlockBegin
    // is called ONCE per block-entry transition (the caller arms the loading
    // spinner on that same trigger frame, then calls PreloadBlockStep(1) once
    // per Update thereafter -- see GameModeScreen.cpp/DojoScreen.cpp), so
    // rebuilding here costs one rebuild per transition, not per frame.
    //
    // A resident block still benefits: every WORK_TEX item resolves via
    // TextureManager's Find(hash) cache check before touching disk (see
    // TextureManager.cpp), so re-running the full manifest on a preloaded
    // block is cheap (no IO) but still steps 1 item/frame, giving the loading
    // spinner its natural on-screen duration instead of draining in a single
    // frame. Previously the two `if (s_*Preloaded) return;` early-outs left
    // the queue empty here, so PreloadBlockStep saw an empty queue and
    // reported "done" on its very first call -- the spinner (armed on the
    // trigger frame) got torn down again one frame later, i.e. invisible.
    if (block == RES_BLOCK_INGAME) {
        BuildIngameQueue(&s_Queue);
    } else if (block == RES_BLOCK_SHOP) {
        BuildShopQueue(&s_Queue);
    }
    // MENU / GAMEOVER-alone / other masks: no V1 manifest (menu textures
    // already eager-load at boot per the Stage 1 [BlockLoad] audit; GAMEOVER
    // is only ever entered additively over INGAME -- see ResBlock.h -- so it
    // has no standalone preload point). Queue stays empty -> PreloadBlockStep
    // reports drained immediately.
}

bool BlockLoader::PreloadBlockStep(int maxItems) {
    if (s_Queue.empty()) return true;

    for (int i = 0; i < maxItems && !s_Queue.empty(); i++) {
        RunWorkItem(s_Queue.front());
        s_Queue.erase(s_Queue.begin());
    }

    if (s_Queue.empty()) {
        if (s_QueueBlock == RES_BLOCK_INGAME) s_IngamePreloaded = true;
        else if (s_QueueBlock == RES_BLOCK_SHOP) s_ShopPreloaded = true;
        return true;
    }
    return false;
}

bool BlockLoader::PreloadBlockDone() {
    return s_Queue.empty();
}

void BlockLoader::Reset() {
    s_Queue.clear();
}

// Task #36 Stage 4 -- see BlockLoader.h. Cancels an in-flight load of `block`
// first (queue cleared, mirroring Reset()) so nothing is freed under a load
// in progress, then drops the held strong SmartPtr<Texture> refs and clears
// the preloaded latch so the block re-preloads correctly next entry.
void BlockLoader::FreeBlock(ResBlockFlag block) {
    if (block == RES_BLOCK_INGAME) {
        LogHeapUsage("before FreeBlock INGAME");
        if (s_QueueBlock == RES_BLOCK_INGAME) s_Queue.clear();
        s_HeldIngame.clear();
        s_IngamePreloaded = false;
        LogHeapUsage("after FreeBlock INGAME");
    } else if (block == RES_BLOCK_SHOP) {
        LogHeapUsage("before FreeBlock SHOP");
        if (s_QueueBlock == RES_BLOCK_SHOP) s_Queue.clear();
        s_HeldShop.clear();
        s_ShopPreloaded = false;
        LogHeapUsage("after FreeBlock SHOP");
    }
}

void BlockLoader::PreloadBlock(ResBlockFlag block) {
    PreloadBlockBegin(block);
    while (!PreloadBlockStep(999)) {
        // drain synchronously -- see PreloadBlockBegin/PreloadBlockStep above.
    }
}

} // namespace wii
} // namespace fn

#endif // FN_BLOCK_PRELOAD
