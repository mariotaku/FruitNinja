#ifndef FN_RESOURCE_BLOCKLOADER_H
#define FN_RESOURCE_BLOCKLOADER_H

// Task #36 Stage 2 -- synchronous block-scoped resource preload (V1).
// See tmp/wii/loader-blueprint.md sections 6/7 (Stage 2) for the design;
// this V1 deliberately simplifies it:
//   - SYNCHRONOUS only -- PreloadBlock() blocks the main thread while it
//     loads. No LWP thread, no mutex queue, no spinner (blueprint Stage 3).
//   - Idempotent -- every underlying Load call (TextureManager::
//     LoadLocalisedTexture, SoundManager::PreLoadSound, MeshManager::Load)
//     already cache-checks internally, so calling PreloadBlock twice for the
//     same block is a cheap no-op the second time.
//
// Stage 4 -- FreeBlock(): port-specific memory reclaim (no binary
// counterpart -- Bada never needed it; the whole game fits its address
// space). Drops this block's s_Held* strong refs and clears its preloaded
// latch. Because TextureManager's cache is weak-only (see the WHY note
// below), dropping the last SmartPtr ref actually evicts the pixels via
// TextureManager::OnTextureDestroyed -- so this is a REAL reclaim, not just
// bookkeeping. A texture still referenced elsewhere (e.g. a screen's own
// member SmartPtr) simply survives at a lower refcount; safe to call any
// time. If a preload of the same block is still mid-Step, FreeBlock cancels
// it first (Reset()-equivalent) so nothing is freed out from under an
// in-flight load. Re-entering the block afterwards re-preloads correctly
// (PreloadBlockBegin sees the cleared latch and rebuilds the queue).
//
// Scope: only the loose PreloadTexture() holders (s_HeldIngame/s_HeldShop)
// and their latches. FruitInfo's hud_*/zen_* textures and the MeshManager-
// resident coin mesh are NOT touched here -- they have their own strong-ref
// homes (FruitInfo array members / MeshManager's own list) outside
// BlockLoader's held vectors; freeing those would need FruitInfo/MeshManager
// eviction APIs of their own (out of scope for this pass).
//
// WHY a per-block texture/model vector at all, given TextureManager's cache
// is just a WeakPtr map (see ResBlock.h / loader-blueprint.md Risk R1)? The
// cache does NOT keep a texture resident by itself -- the strong SmartPtr
// returned by Load() must be held somewhere or the texture is evicted the
// instant that temporary SmartPtr goes out of scope. BlockLoader::s_Held
// is that holder. Meshes need no separate holder: MeshManager::Load already
// inserts the strong SmartPtr<Model> into its own m_Models list (see
// MeshManager.cpp:70-73), so the manager itself keeps it resident.
//
// Originally Wii-only (MEM1/MEM2 budget forced eager boot-load off);
// relocated out of src/platform/wii so any target can build+enable it via
// FN_BLOCK_PRELOAD (see root CMakeLists.txt) for testing the preload/loading
// UX path. Only compiled when FN_BLOCK_PRELOAD is set.
#ifdef FN_BLOCK_PRELOAD

#include "resource/ResBlock.h"

namespace fn {
namespace wii {

class BlockLoader {
public:
    // Force-loads every texture/SFX/mesh/font-size in the manifest for
    // `block` (INGAME's manifest includes the GAMEOVER deltas -- see file
    // comment in BlockLoader.cpp -- since gameover pops instantly over the
    // frozen game with no load-covering transition of its own). Synchronous:
    // does not return until every entry has been loaded (or failed) --
    // intended to be called at a block-entry hook that already sits behind a
    // fade/transition (GameModeScreen::SetupLevel, ShopScreen ctor).
    //
    // Safe to call every time the block is entered -- idempotent (see file
    // comment above).
    //
    // Implemented as PreloadBlockBegin() + a drain loop over PreloadBlockStep()
    // -- see those below (task #66 Phase 1). Callers that don't need the
    // cooperative per-frame path (ShopScreen ctor, any non-Wii/non-loading-UI
    // caller) keep calling this unchanged.
    static void PreloadBlock(ResBlockFlag block);

    // --- Task #66 Phase 1: cooperative (per-frame) preload stepper ---------
    // Builds the work-queue for `block` and returns immediately -- does NOT
    // load anything itself. ALWAYS rebuilds the queue, even if `block` is
    // already preloaded (s_IngamePreloaded/s_ShopPreloaded) -- those latches
    // no longer gate the rebuild (see BlockLoader.cpp fix note at
    // PreloadBlockBegin). On an already-resident block every WORK_TEX item
    // is a TextureManager cache hit (no disk IO), so re-running the manifest
    // is cheap, but it still steps 1 item/frame -- this is what makes the
    // loading spinner visible for the manifest's natural duration on
    // re-entry instead of the queue coming back empty and the spinner
    // vanishing one frame after being armed. Call once per block-entry, then
    // drive the queue with PreloadBlockStep() from the caller's per-frame
    // Update (e.g. GameModeScreen's LOADING sub-state).
    static void PreloadBlockBegin(ResBlockFlag block);

    // Executes up to `maxItems` queued work items (one LoadContent wrapper,
    // one texture, one SFX, or the coin mesh counts as one item each --
    // see BlockLoader.cpp file comment for the manifest -> work-item mapping).
    // Returns true once the queue is DRAINED (nothing left to do this call or
    // already-preloaded) -- the caller's contract is "keep calling with
    // !PreloadBlockStep(N) as the hold condition". Sets the per-block
    // preloaded guard TRUE only when the last item is popped, mirroring
    // PreloadBlock's own guard semantics. Item budget, not wall-clock --
    // deterministic across platforms/frame-rates.
    static bool PreloadBlockStep(int maxItems);

    // True if the work-queue for the block passed to the last
    // PreloadBlockBegin() call is empty (either drained by PreloadBlockStep,
    // or PreloadBlockBegin was called for a block with no V1 manifest --
    // MENU/GAMEOVER-alone/other masks -- and built no queue at all).
    static bool PreloadBlockDone();

    // Clears the pending work-queue without executing it -- teardown safety
    // for a caller destroyed mid-load (e.g. GameModeScreen torn down before
    // its LOADING sub-state finishes). Does NOT touch the per-block
    // preloaded guards -- a block left partially-loaded when Reset() is
    // called simply re-queues+re-hits-cache on the next PreloadBlockBegin,
    // same as re-entering a block twice today.
    static void Reset();

    // --- Stage 4: memory reclaim (port-specific, no binary counterpart) ----
    // Drops `block`'s held strong SmartPtr<Texture> refs (s_HeldIngame for
    // RES_BLOCK_INGAME, s_HeldShop for RES_BLOCK_SHOP) and clears its
    // preloaded latch, so the next PreloadBlockBegin(block) re-preloads from
    // disk instead of no-op'ing. If `block` is currently mid-Step (this
    // block's queue not yet drained), cancels that in-flight load first
    // (queue cleared, same as Reset()) so nothing is freed out from under a
    // load in progress. No-op for any other block value (RES_BLOCK_NONE,
    // RES_BLOCK_MENU, or a mask BlockLoader has no manifest for).
    //
    // TextureManager's cache is weak-only (see file comment above) -- once
    // the last strong ref here drops, a texture with no other owner is
    // actually evicted (OnTextureDestroyed), reclaiming its pixel memory.
    // Textures still held by another owner (a screen's own member SmartPtr)
    // just lose one refcount and stay resident -- safe either way.
    //
    // Caller contract: call only after the screen/control tree that was
    // using this block's assets has already dropped ITS refs (e.g. after
    // ~ShopScreen has run, or once gameplay has genuinely returned to the
    // menu) -- see the call sites in ShopScreen.cpp / GameOverScreen.cpp.
    static void FreeBlock(ResBlockFlag block);
};

// Task #36/#59 diagnostic -- on Wii, logs libogc's MEM1/MEM2 arena free size
// (bytes still allocatable, via SYS_GetArena1Size/SYS_GetArena2Size) as
// "[HeapUsage] <label>: MEM1 free=<n> KB, MEM2 free=<n> KB" at LOG_INFO.
// On non-Wii targets this is a no-op (no arena concept to report) -- see
// BlockLoader.cpp. Cheap on Wii (a couple of reads) -- call at low-frequency
// points only (boot, block-preload transitions), never per-frame. `label`
// identifies the call site (e.g. "boot-done", "INGAME+GAMEOVER", "SHOP") so a
// Dolphin log can be grepped into a before/after residency timeline.
void LogHeapUsage(const char* label);

} // namespace wii
} // namespace fn

#endif // FN_BLOCK_PRELOAD

#endif // FN_RESOURCE_BLOCKLOADER_H
