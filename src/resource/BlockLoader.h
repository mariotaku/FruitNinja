#ifndef FN_RESOURCE_BLOCKLOADER_H
#define FN_RESOURCE_BLOCKLOADER_H

// Task #36 Stage 2 -- synchronous block-scoped resource preload (V1).
// See tmp/wii/loader-blueprint.md sections 6/7 (Stage 2) for the design;
// this V1 deliberately simplifies it:
//   - SYNCHRONOUS only -- PreloadBlock() blocks the main thread while it
//     loads. No LWP thread, no mutex queue, no spinner (blueprint Stage 3).
//   - No eviction/FreeBlock -- everything preloaded here stays resident for
//     the process lifetime (V1 "union fits budget" per the task; Stage 4
//     would add FreeBlock()).
//   - Idempotent -- every underlying Load call (TextureManager::
//     LoadLocalisedTexture, SoundManager::PreLoadSound, MeshManager::Load)
//     already cache-checks internally, so calling PreloadBlock twice for the
//     same block is a cheap no-op the second time.
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
    // load anything itself. No-op (leaves the queue empty) if `block` is
    // already preloaded (s_IngamePreloaded/s_ShopPreloaded), matching
    // PreloadBlock's own idempotency guard. Call once per block-entry, then
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
    // or PreloadBlockBegin found the block already preloaded and built no
    // queue at all).
    static bool PreloadBlockDone();

    // Clears the pending work-queue without executing it -- teardown safety
    // for a caller destroyed mid-load (e.g. GameModeScreen torn down before
    // its LOADING sub-state finishes). Does NOT touch the per-block
    // preloaded guards -- a block left partially-loaded when Reset() is
    // called simply re-queues+re-hits-cache on the next PreloadBlockBegin,
    // same as re-entering a block twice today.
    static void Reset();
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
